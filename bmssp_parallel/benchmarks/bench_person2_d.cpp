#include "core/frontier_interface.hpp"
#include "core/graph.hpp"
#include "core/primitives.hpp"
#include "core/types.hpp"
#include "baseline/bmssp_seq.hpp"
#include "data_structure/concurrent_frontier_d.hpp"
#include "data_structure/d1_skiplist.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace bmssp;

namespace {

// Reusable spin-first barrier pool for Benchmark B phased runs
class PhaseWorkerPool {
private:
    size_t num_threads_;
    std::vector<std::thread> workers_;
    std::atomic<uint64_t> phase_gen_{0};
    std::atomic<size_t> active_done_{0};
    std::atomic<bool> stop_{false};
    const std::function<void(size_t, size_t)>* current_task_{nullptr};

    void worker_loop(size_t tid) {
        uint64_t seen_gen = 0;
        while (true) {
            uint64_t g = phase_gen_.load(std::memory_order_acquire);
            int spins = 0;
            while (g == seen_gen) {
                if (stop_.load(std::memory_order_acquire)) {
                    return;
                }
                if (spins < 512) {
                    cpu_pause();
                    ++spins;
                } else {
                    std::this_thread::yield();
                }
                g = phase_gen_.load(std::memory_order_acquire);
            }
            seen_gen = g;
            if (stop_.load(std::memory_order_acquire)) {
                return;
            }
            (*current_task_)(tid, num_threads_);
            active_done_.fetch_add(1, std::memory_order_release);
        }
    }

public:
    explicit PhaseWorkerPool(size_t num_threads)
        : num_threads_(std::max<size_t>(1, num_threads)) {
        workers_.reserve(num_threads_ - 1);
        for (size_t t = 1; t < num_threads_; ++t) {
            workers_.emplace_back([this, t]() { worker_loop(t); });
        }
    }

    ~PhaseWorkerPool() {
        stop_.store(true, std::memory_order_release);
        phase_gen_.fetch_add(1, std::memory_order_release);
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    PhaseWorkerPool(const PhaseWorkerPool&) = delete;
    PhaseWorkerPool& operator=(const PhaseWorkerPool&) = delete;

    template <typename Fn>
    void parallel_run(Fn&& fn) {
        if (num_threads_ == 1) {
            fn(size_t{0}, size_t{1});
            return;
        }
        std::function<void(size_t, size_t)> wrapped(std::forward<Fn>(fn));
        current_task_ = &wrapped;
        active_done_.store(0, std::memory_order_release);
        phase_gen_.fetch_add(1, std::memory_order_release);

        wrapped(0, num_threads_);

        int spins = 0;
        while (active_done_.load(std::memory_order_acquire) < num_threads_ - 1) {
            if (spins < 512) {
                cpu_pause();
                ++spins;
            } else {
                std::this_thread::yield();
            }
        }
        current_task_ = nullptr;
    }
};

// ============================================================================
// BENCHMARK A: Concurrent insert+relax scaling (MutexFrontierD vs ConcurrentFrontierD)
// ============================================================================
struct BenchATrial {
    double elapsed_ms{0.0};
    uint64_t successful_inserts{0};
    ConcurrentFrontierD::FrontierStats stats{};
};

template <typename FrontierT>
BenchATrial run_bench_a_trial(PhaseWorkerPool& pool,
                              size_t num_threads,
                              uint32_t num_vertices,
                              size_t block_M,
                              double bound_B,
                              const std::vector<VertexDist>& all_ops,
                              size_t num_waves,
                              size_t ops_per_wave,
                              bool is_concurrent) {
    std::vector<AtomicVertexState> states(num_vertices);
    for (uint32_t v = 0; v < num_vertices; ++v) {
        states[v].store(VertexState{INF_DIST, NO_PRED, 0}, std::memory_order_relaxed);
    }

    FrontierT frontier;
    if constexpr (std::is_same_v<FrontierT, ConcurrentFrontierD>) {
        frontier.set_state_array(states.data(), num_vertices);
        frontier.set_eager_cleanup(true);
        frontier.reset_stats();
    }
    frontier.init(block_M, bound_B);

    std::vector<uint64_t> thread_ins(num_threads, 0);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t w = 0; w < num_waves; ++w) {
        const size_t wave_offset = w * ops_per_wave;
        pool.parallel_run([&](size_t tid, size_t nthreads) {
            const size_t start = wave_offset + (ops_per_wave * tid) / nthreads;
            const size_t end = wave_offset + (ops_per_wave * (tid + 1)) / nthreads;
            uint64_t local_ins = 0;
            const uint32_t epoch = static_cast<uint32_t>(w + 1);
            for (size_t i = start; i < end; ++i) {
                const uint32_t v = all_ops[i].v;
                const double d = all_ops[i].dist;
                if (relax_edge_atomic(states[v], d, static_cast<uint32_t>(tid), epoch)) {
                    frontier.insert(v, d);
                    ++local_ins;
                }
            }
            thread_ins[tid] += local_ins;
        });
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    uint64_t total_ins = 0;
    for (uint64_t c : thread_ins) {
        total_ins += c;
    }

    BenchATrial res;
    res.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    res.successful_inserts = total_ins;
    if constexpr (std::is_same_v<FrontierT, ConcurrentFrontierD>) {
        if (is_concurrent) {
            res.stats = frontier.get_stats();
        }
    }
    frontier.clear();
    return res;
}

void run_benchmark_a() {
    constexpr uint32_t N = 50000;
    constexpr size_t M = 64;
    constexpr double B = 100000.0;
    constexpr size_t NUM_WAVES = 20;
    constexpr size_t OPS_PER_WAVE = 50000;
    constexpr size_t TOTAL_RELAXATIONS = NUM_WAVES * OPS_PER_WAVE; // 1,000,000
    constexpr uint64_t SEED = 20261118ULL;
    const std::array<size_t, 6> thread_counts = {1, 2, 4, 8, 12, 32};

    std::cout << "\n==========================================================================================" << std::endl;
    std::cout << "  BENCHMARK A: Concurrent Insert+Relax Scaling (MutexFrontierD vs ConcurrentFrontierD)" << std::endl;
    std::cout << "  Workload: " << TOTAL_RELAXATIONS << " relaxations (" << NUM_WAVES << " waves x "
              << OPS_PER_WAVE << ") into N=" << N << " vertices, M=" << M << ", B=" << B
              << ", Seed=" << SEED << std::endl;
    std::cout << "  Protocol: 1 warmup + 3 timed trials per config (median reported); PhaseWorkerPool." << std::endl;
    std::cout << "==========================================================================================" << std::endl;

    // Pre-generate deterministic 20-wave overlapping relaxation workload once from SEED
    std::vector<VertexDist> all_ops;
    all_ops.reserve(TOTAL_RELAXATIONS);
    std::mt19937_64 rng(SEED);
    for (size_t w = 0; w < NUM_WAVES; ++w) {
        const double base = 50000.0 - static_cast<double>(w) * 2000.0;
        for (size_t i = 0; i < OPS_PER_WAVE; ++i) {
            const uint32_t v = static_cast<uint32_t>(rng() % N);
            const double jitter = static_cast<double>(rng() % 800000) * 0.01; // [0, 8000)
            all_ops.push_back(VertexDist{v, base + jitter});
        }
    }

    struct RowA {
        size_t threads;
        BenchATrial mutex_med;
        BenchATrial conc_med;
    };
    std::vector<RowA> rows;

    for (size_t T : thread_counts) {
        PhaseWorkerPool pool(T);

        // BEFORE: MutexFrontierD (1 warmup + 3 timed)
        (void)run_bench_a_trial<MutexFrontierD>(pool, T, N, M, B, all_ops, NUM_WAVES, OPS_PER_WAVE, false);
        std::vector<BenchATrial> mtx_trials(3);
        for (int r = 0; r < 3; ++r) {
            mtx_trials[r] = run_bench_a_trial<MutexFrontierD>(pool, T, N, M, B, all_ops, NUM_WAVES, OPS_PER_WAVE, false);
        }
        std::sort(mtx_trials.begin(), mtx_trials.end(),
                  [](const BenchATrial& a, const BenchATrial& b) { return a.elapsed_ms < b.elapsed_ms; });

        // AFTER: ConcurrentFrontierD (1 warmup + 3 timed)
        (void)run_bench_a_trial<ConcurrentFrontierD>(pool, T, N, M, B, all_ops, NUM_WAVES, OPS_PER_WAVE, true);
        std::vector<BenchATrial> conc_trials(3);
        for (int r = 0; r < 3; ++r) {
            conc_trials[r] = run_bench_a_trial<ConcurrentFrontierD>(pool, T, N, M, B, all_ops, NUM_WAVES, OPS_PER_WAVE, true);
        }
        std::sort(conc_trials.begin(), conc_trials.end(),
                  [](const BenchATrial& a, const BenchATrial& b) { return a.elapsed_ms < b.elapsed_ms; });

        rows.push_back(RowA{T, mtx_trials[1], conc_trials[1]});
    }

    const double mtx_1t_ms = rows[0].mutex_med.elapsed_ms;
    const double conc_1t_ms = rows[0].conc_med.elapsed_ms;

    std::cout << std::left
              << std::setw(8) << "Threads"
              << std::setw(12) << "Mutex(ms)"
              << std::setw(12) << "Mtx(Mop/s)"
              << std::setw(10) << "MtxScale"
              << std::setw(12) << "Conc(ms)"
              << std::setw(12) << "Conc(Mop/s)"
              << std::setw(10) << "ConcScale"
              << std::setw(10) << "Speedup"
              << "MtxIns / ConcIns / Overflows / Splits / Saved" << std::endl;
    std::cout << std::string(134, '-') << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    for (const auto& row : rows) {
        const double mtx_mops = static_cast<double>(TOTAL_RELAXATIONS) / (row.mutex_med.elapsed_ms * 1000.0);
        const double conc_mops = static_cast<double>(TOTAL_RELAXATIONS) / (row.conc_med.elapsed_ms * 1000.0);
        const double mtx_scale = mtx_1t_ms / row.mutex_med.elapsed_ms;
        const double conc_scale = conc_1t_ms / row.conc_med.elapsed_ms;
        const double speedup = row.mutex_med.elapsed_ms / row.conc_med.elapsed_ms;

        std::ostringstream extra;
        extra << row.mutex_med.successful_inserts
              << " / " << row.conc_med.successful_inserts
              << " / " << row.conc_med.stats.overflow_events
              << " / " << row.conc_med.stats.actual_splits
              << " / " << row.conc_med.stats.eager_cleanup_saved_splits;

        std::cout << std::left
                  << std::setw(8) << row.threads
                  << std::setw(12) << row.mutex_med.elapsed_ms
                  << std::setw(12) << mtx_mops
                  << std::setw(10) << mtx_scale
                  << std::setw(12) << row.conc_med.elapsed_ms
                  << std::setw(12) << conc_mops
                  << std::setw(10) << conc_scale
                  << std::setw(10) << speedup
                  << extra.str() << std::endl;
    }
}

// ============================================================================
// BENCHMARK B: Full Distance-Sliced Phased Loop (N=50,000, ~500,000 edges, M=64)
// ============================================================================
struct BenchBTrial {
    double total_ms{0.0};
    double insert_phase_ms{0.0};
    double pull_phase_ms{0.0};
    size_t pull_calls{0};
    ConcurrentFrontierD::FrontierStats stats{};
    std::vector<double> final_dists;
};

template <typename FrontierT>
BenchBTrial run_bench_b_trial(const CSRGraph& graph,
                              PhaseWorkerPool& pool,
                              size_t num_threads,
                              size_t block_M) {
    const uint32_t N = graph.n;
    std::vector<AtomicVertexState> states(N);
    for (uint32_t v = 0; v < N; ++v) {
        states[v].store(VertexState{INF_DIST, NO_PRED, 0}, std::memory_order_relaxed);
    }
    states[0].store(VertexState{0.0, 0, 1}, std::memory_order_relaxed);

    FrontierT frontier;
    if constexpr (std::is_same_v<FrontierT, ConcurrentFrontierD>) {
        frontier.set_state_array(states.data(), N);
        frontier.set_eager_cleanup(true);
        frontier.reset_stats();
    }
    frontier.init(block_M, INF_DIST);

    EpochDeduplicator wave_dedup(N);
    std::vector<std::vector<uint32_t>> local_subwave(num_threads);
    std::vector<std::vector<VertexDist>> local_prepend(num_threads);
    std::vector<uint32_t> active_wave;
    std::vector<VertexDist> k_prepend;
    std::vector<uint32_t> S_i;
    S_i.reserve(block_M);

    int64_t pull_ns = 0;
    int64_t insert_ns = 0;
    uint32_t global_epoch = 1;
    size_t pull_calls = 0;

    auto t_total_start = std::chrono::high_resolution_clock::now();

    // Seed initial frontier from source 0 (> M vertices)
    {
        auto ti0 = std::chrono::high_resolution_clock::now();
        for (uint64_t e = graph.edge_begin(0); e < graph.edge_end(0); ++e) {
            const uint32_t v = graph.edge_dest(e);
            const double nd = graph.edge_weight(e);
            if (relax_edge_atomic(states[v], nd, 0, 1)) {
                frontier.insert(v, nd);
            }
        }
        auto ti1 = std::chrono::high_resolution_clock::now();
        insert_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(ti1 - ti0).count();
    }

    double B_i = -INF_DIST;
    double prev_Bi = -INF_DIST;

    while (true) {
        auto tp0 = std::chrono::high_resolution_clock::now();
        const bool ok = frontier.pull(S_i, B_i);
        auto tp1 = std::chrono::high_resolution_clock::now();
        pull_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(tp1 - tp0).count();
        if (!ok) {
            break;
        }
        ++pull_calls;
        if (B_i < prev_Bi) {
            std::cerr << "[ERROR] Non-monotonic pull bound in Benchmark B: "
                      << B_i << " < " << prev_Bi << std::endl;
            std::abort();
        }
        prev_Bi = B_i;

        auto ti0 = std::chrono::high_resolution_clock::now();
        active_wave = S_i;
        const double prepend_band = std::isfinite(B_i) ? (B_i + 3.0) : INF_DIST;
        for (size_t t = 0; t < num_threads; ++t) {
            local_prepend[t].clear();
        }

        while (!active_wave.empty()) {
            const uint32_t epoch = ++global_epoch;
            pool.parallel_run([&](size_t tid, size_t nthreads) {
                local_subwave[tid].clear();
                const size_t total = active_wave.size();
                const size_t start = (total * tid) / nthreads;
                const size_t end = (total * (tid + 1)) / nthreads;
                for (size_t idx = start; idx < end; ++idx) {
                    const uint32_t u = active_wave[idx];
                    const double du = states[u].load(std::memory_order_acquire).dist;
                    for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
                        const uint32_t v = graph.edge_dest(e);
                        const double nd = du + graph.edge_weight(e);
                        if (relax_edge_atomic(states[v], nd, u, epoch)) {
                            if (nd < B_i) {
                                if (wave_dedup.try_claim(v, epoch)) {
                                    local_subwave[tid].push_back(v);
                                }
                            } else if (nd < prepend_band) {
                                local_prepend[tid].push_back(VertexDist{v, nd});
                            } else {
                                frontier.insert(v, nd);
                            }
                        }
                    }
                }
            });

            active_wave.clear();
            for (size_t t = 0; t < num_threads; ++t) {
                active_wave.insert(active_wave.end(), local_subwave[t].begin(), local_subwave[t].end());
            }
        }
        auto ti1 = std::chrono::high_resolution_clock::now();
        insert_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(ti1 - ti0).count();

        auto tp2 = std::chrono::high_resolution_clock::now();
        k_prepend.clear();
        for (size_t t = 0; t < num_threads; ++t) {
            k_prepend.insert(k_prepend.end(), local_prepend[t].begin(), local_prepend[t].end());
        }
        if (!k_prepend.empty()) {
            std::sort(k_prepend.begin(), k_prepend.end(), [](const VertexDist& a, const VertexDist& b) {
                if (a.v != b.v) return a.v < b.v;
                return a.dist < b.dist;
            });
            size_t w = 0;
            for (size_t i = 0; i < k_prepend.size(); ++i) {
                if (w == 0 || k_prepend[i].v != k_prepend[w - 1].v) {
                    const double best = states[k_prepend[i].v].load(std::memory_order_acquire).dist;
                    if (k_prepend[i].dist <= best) {
                        k_prepend[w++] = k_prepend[i];
                    }
                }
            }
            k_prepend.resize(w);
            std::sort(k_prepend.begin(), k_prepend.end(), [](const VertexDist& a, const VertexDist& b) {
                if (a.dist != b.dist) return a.dist < b.dist;
                return a.v < b.v;
            });
            frontier.batch_prepend(k_prepend);
        }
        auto tp3 = std::chrono::high_resolution_clock::now();
        pull_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(tp3 - tp2).count();
    }

    auto t_total_end = std::chrono::high_resolution_clock::now();

    BenchBTrial res;
    res.total_ms = std::chrono::duration<double, std::milli>(t_total_end - t_total_start).count();
    res.insert_phase_ms = static_cast<double>(insert_ns) * 1e-6;
    res.pull_phase_ms = static_cast<double>(pull_ns) * 1e-6;
    res.pull_calls = pull_calls;
    if constexpr (std::is_same_v<FrontierT, ConcurrentFrontierD>) {
        res.stats = frontier.get_stats();
    }
    res.final_dists.resize(N);
    for (uint32_t v = 0; v < N; ++v) {
        res.final_dists[v] = states[v].load(std::memory_order_acquire).dist;
    }
    frontier.clear();
    return res;
}

void verify_exact_distances(const std::vector<double>& oracle_dist,
                            const std::vector<double>& mtx_dist,
                            const std::vector<double>& conc_dist,
                            size_t threads) {
    if (oracle_dist.size() != mtx_dist.size() || oracle_dist.size() != conc_dist.size()) {
        std::cerr << "[FAIL] Distance vector size mismatch at T=" << threads << std::endl;
        std::abort();
    }
    for (size_t v = 0; v < oracle_dist.size(); ++v) {
        const double want = oracle_dist[v];
        const double gm = mtx_dist[v];
        const double gc = conc_dist[v];
        if (std::isinf(want)) {
            if (!std::isinf(gm) || !std::isinf(gc)) {
                std::cerr << "[FAIL] Unreachable vertex mismatch at v=" << v << std::endl;
                std::abort();
            }
        } else {
            if (std::abs(gm - want) > 1e-9 || std::abs(gc - want) > 1e-9 || std::abs(gm - gc) > 1e-9) {
                std::cerr << "[FAIL] Distance mismatch at T=" << threads << ", v=" << v
                          << " oracle=" << want << " mutex=" << gm << " conc=" << gc << std::endl;
                std::abort();
            }
        }
    }
}

void run_benchmark_b() {
    constexpr uint32_t N = 50000;
    constexpr uint64_t TARGET_EDGES = 500000;
    constexpr size_t M = 64;
    constexpr uint64_t SEED = 20261119ULL;
    const std::array<size_t, 5> thread_counts = {1, 2, 4, 8, 12};

    std::mt19937_64 rng(SEED);
    std::uniform_real_distribution<double> w_dist(1.0, 100.0);
    std::vector<Edge> edges;
    edges.reserve(TARGET_EDGES + 512);

    // Directed Hamiltonian cycle backbone for 100% reachability
    for (uint32_t i = 0; i + 1 < N; ++i) {
        edges.push_back({i, i + 1, w_dist(rng)});
    }
    // Broad initial fanout from source 0 (> M vertices)
    for (uint32_t i = 1; i <= 256; ++i) {
        edges.push_back({0, static_cast<uint32_t>((i * 193) % N), w_dist(rng)});
    }
    while (edges.size() < TARGET_EDGES) {
        uint32_t u = static_cast<uint32_t>(rng() % N);
        uint32_t v = static_cast<uint32_t>(rng() % N);
        if (u == v) continue;
        edges.push_back({u, v, w_dist(rng)});
    }

    CSRGraph graph = CSRGraph::from_edge_list(N, edges);
    SSSPResult oracle = dijkstra_sequential(graph, 0);

    std::cout << "\n==========================================================================================" << std::endl;
    std::cout << "  BENCHMARK B: Full Distance-Sliced Phased Loop (MutexFrontierD vs ConcurrentFrontierD)" << std::endl;
    std::cout << "  Graph: N=" << graph.n << ", E=" << graph.m << ", M=" << M
              << ", weights=[1.0, 100.0], Seed=" << SEED << std::endl;
    std::cout << "  Protocol: 1 warmup + 3 timed trials per config (median total_ms trial reported)." << std::endl;
    std::cout << "==========================================================================================" << std::endl;

    struct RowB {
        size_t threads;
        BenchBTrial mtx;
        BenchBTrial conc;
    };
    std::vector<RowB> rows;

    for (size_t T : thread_counts) {
        PhaseWorkerPool pool(T);

        // BEFORE: MutexFrontierD
        auto warmup_m = run_bench_b_trial<MutexFrontierD>(graph, pool, T, M);
        std::vector<BenchBTrial> mtx_trials(3);
        for (int r = 0; r < 3; ++r) {
            mtx_trials[r] = run_bench_b_trial<MutexFrontierD>(graph, pool, T, M);
        }
        std::sort(mtx_trials.begin(), mtx_trials.end(),
                  [](const BenchBTrial& a, const BenchBTrial& b) { return a.total_ms < b.total_ms; });

        // AFTER: ConcurrentFrontierD
        auto warmup_c = run_bench_b_trial<ConcurrentFrontierD>(graph, pool, T, M);
        std::vector<BenchBTrial> conc_trials(3);
        for (int r = 0; r < 3; ++r) {
            conc_trials[r] = run_bench_b_trial<ConcurrentFrontierD>(graph, pool, T, M);
        }
        std::sort(conc_trials.begin(), conc_trials.end(),
                  [](const BenchBTrial& a, const BenchBTrial& b) { return a.total_ms < b.total_ms; });

        verify_exact_distances(oracle.dist, warmup_m.final_dists, warmup_c.final_dists, T);
        for (int r = 0; r < 3; ++r) {
            verify_exact_distances(oracle.dist, mtx_trials[r].final_dists, conc_trials[r].final_dists, T);
        }

        rows.push_back(RowB{T, std::move(mtx_trials[1]), std::move(conc_trials[1])});
    }

    const double conc_1t_ms = rows[0].conc.total_ms;

    std::cout << std::left
              << std::setw(8) << "Threads"
              << std::setw(12) << "MtxTot(ms)"
              << std::setw(12) << "MtxIns(ms)"
              << std::setw(12) << "MtxPull(ms)"
              << std::setw(13) << "ConcTot(ms)"
              << std::setw(13) << "ConcIns(ms)"
              << std::setw(13) << "ConcPull(ms)"
              << std::setw(10) << "Speedup"
              << "ConcScale" << std::endl;
    std::cout << std::string(106, '-') << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    for (const auto& row : rows) {
        const double speedup = row.mtx.total_ms / row.conc.total_ms;
        const double conc_scale = conc_1t_ms / row.conc.total_ms;
        std::cout << std::left
                  << std::setw(8) << row.threads
                  << std::setw(12) << row.mtx.total_ms
                  << std::setw(12) << row.mtx.insert_phase_ms
                  << std::setw(12) << row.mtx.pull_phase_ms
                  << std::setw(13) << row.conc.total_ms
                  << std::setw(13) << row.conc.insert_phase_ms
                  << std::setw(13) << row.conc.pull_phase_ms
                  << std::setw(10) << speedup
                  << conc_scale << std::endl;
    }

    std::cout << "\n--- ConcurrentFrontierD FrontierStats (Benchmark B median trials) ---" << std::endl;
    for (const auto& row : rows) {
        const auto& st = row.conc.stats;
        std::cout << "  T=" << std::setw(2) << row.threads
                  << " | pulls=" << row.conc.pull_calls
                  << " | overflows=" << st.overflow_events
                  << " | splits=" << st.actual_splits
                  << " | saved_splits=" << st.eager_cleanup_saved_splits
                  << " | stale_ins=" << st.stale_dropped_on_insert
                  << " | stale_pull=" << st.stale_dropped_on_pull << std::endl;
    }
    std::cout << "  [VERIFIED] 100% identical final vertex distances between Dijkstra oracle, "
              << "MutexFrontierD, and ConcurrentFrontierD across all thread counts." << std::endl;
}

} // namespace

int main() {
    std::cout << "==========================================================================================" << std::endl;
    std::cout << "  BMSSP Parallel - Person 2 A/B Benchmarks (MutexFrontierD vs ConcurrentFrontierD)" << std::endl;
    std::cout << "  Hardware concurrency reported by std::thread: "
              << std::thread::hardware_concurrency() << " threads" << std::endl;
    std::cout << "==========================================================================================" << std::endl;

    run_benchmark_a();
    run_benchmark_b();

    std::cout << "\n==========================================================================================" << std::endl;
    std::cout << "  BENCHMARK RUN COMPLETE" << std::endl;
    std::cout << "==========================================================================================" << std::endl;

    return 0;
}
