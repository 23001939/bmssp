#include "core/frontier_interface.hpp"
#include "core/graph.hpp"
#include "core/primitives.hpp"
#include "core/types.hpp"
#include "baseline/bmssp_seq.hpp"
#include "data_structure/concurrent_frontier_d.hpp"
#include "data_structure/d0_deque.hpp"
#include "data_structure/d1_skiplist.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

using namespace bmssp;

namespace {

std::atomic<uint64_t> g_checks_passed{0};
std::atomic<uint64_t> g_checks_total{0};

#define QA_ASSERT(cond, msg)                                                         \
    do {                                                                             \
        g_checks_total.fetch_add(1, std::memory_order_relaxed);                      \
        if (!(cond)) {                                                               \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl;   \
            std::abort();                                                            \
        }                                                                            \
        g_checks_passed.fetch_add(1, std::memory_order_relaxed);                     \
    } while (0)

// Reusable spin-first barrier for persistent worker threads (avoids OS thread-spawn
// overhead per BMSSP phase and prevents spin-starvation when T > hardware cores).
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
// SUITE 1: Edge Cases for D0Deque & ConcurrentFrontierD
// ============================================================================
void test_suite_1_edge_cases() {
    std::cout << ">>> [Suite 1] Edge cases for D0Deque & ConcurrentFrontierD..." << std::endl;

    // 1A. D0Deque with M = 1 (and M = 0 clamped to 1)
    {
        D0Deque d0;
        d0.init(0, 100.0);
        QA_ASSERT(d0.empty(), "D0Deque empty after init");
        QA_ASSERT(d0.block_count() == 0, "D0Deque block_count == 0");
        QA_ASSERT(d0.approx_size() == 0, "D0Deque approx_size == 0");
        QA_ASSERT(std::isinf(d0.peek_front_upper_bound()), "Empty D0 peek_front_upper_bound is INF");
        QA_ASSERT(std::isinf(d0.peek_front_min_dist()), "Empty D0 peek_front_min_dist is INF");

        // All invalid batch (NaN, > B_, +INF)
        const double qnan = std::numeric_limits<double>::quiet_NaN();
        std::vector<VertexDist> all_bad = {
            {1, qnan},
            {2, 100.0001},
            {3, INF_DIST},
        };
        d0.batch_prepend(all_bad);
        QA_ASSERT(d0.empty(), "D0Deque ignores all-NaN / out-of-bound batch");

        // Fast sorted path with M = 1
        std::vector<VertexDist> sorted_batch = {{10, 10.0}, {11, 20.0}, {12, 30.0}, {13, 100.0}};
        d0.batch_prepend(sorted_batch);
        QA_ASSERT(d0.block_count() == 4, "M=1 creates 1 block per item");
        QA_ASSERT(d0.approx_size() == 4, "approx_size matches");
        QA_ASSERT(d0.peek_front_min_dist() == 10.0, "front min dist == 10.0");
        QA_ASSERT(d0.peek_front_upper_bound() == 10.0, "front ub == 10.0");

        // Adversarial unsorted + overlapping + NaN + out-of-bound batch
        std::vector<VertexDist> adv_batch = {
            {20, 25.0}, {21, qnan}, {22, 5.0}, {23, 150.0}, {24, 15.0}
        };
        d0.batch_prepend(adv_batch);
        QA_ASSERT(d0.peek_front_min_dist() == 5.0, "Adversarial merge places 5.0 at front");

        std::vector<VertexDist> drained;
        uint64_t stale = 0;
        size_t got = d0.drain_valid_up_to(100, nullptr, 0, drained, stale);
        QA_ASSERT(got == 7 && drained.size() == 7, "All 7 valid elements drained");
        QA_ASSERT(stale == 0, "No stale dropped without state_arr");
        const std::vector<double> expected_dists = {5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 100.0};
        for (size_t i = 0; i < expected_dists.size(); ++i) {
            QA_ASSERT(drained[i].dist == expected_dists[i], "D0 drained in strict non-decreasing order");
        }
        QA_ASSERT(d0.empty(), "D0 empty after full drain");
    }

    // 1B. D0Deque with M = 16: stale filtering, push_front_block, push_front_sorted, equal ties
    {
        std::vector<AtomicVertexState> states(64);
        for (uint32_t i = 0; i < 64; ++i) {
            states[i].store(VertexState{10.0, NO_PRED, 1});
        }
        states[0].store(VertexState{2.0, NO_PRED, 2});
        states[5].store(VertexState{1.0, NO_PRED, 2});

        D0Deque d0;
        d0.init(16, 500.0);

        D0Block blk;
        for (uint32_t i = 0; i < 20; ++i) {
            blk.items.push_back(VertexDist{i, 10.0});
        }
        // Reverse order inside blk to test sorting + splitting into <= M blocks
        std::reverse(blk.items.begin(), blk.items.end());
        d0.push_front_block(std::move(blk));
        QA_ASSERT(d0.block_count() == 2, "20 items split into 2 blocks for M=16");

        std::vector<VertexDist> overlap = {{20, 5.0}, {21, 10.0}};
        d0.push_front_sorted(overlap);

        std::vector<VertexDist> out;
        uint64_t stale = 0;
        d0.drain_valid_up_to(30, states.data(), states.size(), out, stale);
        QA_ASSERT(stale == 2, "Exactly 2 stale entries (v=0 and v=5) dropped in D0 drain");
        QA_ASSERT(out.size() == 20, "20 valid items remain");
        QA_ASSERT(d0.empty(), "D0 empty after drain");

        d0.clear();
        QA_ASSERT(d0.empty() && d0.block_count() == 0, "D0 clear resets cleanly");
    }

    // 1C. ConcurrentFrontierD edge cases across M = 1 and M = 16
    for (size_t M : {size_t{1}, size_t{16}}) {
        constexpr uint32_t N = 128;
        std::vector<AtomicVertexState> states(N);
        auto reset_states = [&](double val) {
            for (uint32_t v = 0; v < N; ++v) {
                states[v].store(VertexState{val, NO_PRED, 0});
            }
        };

        ConcurrentFrontierD frontier(states.data(), N);
        const double B = 100.0;
        frontier.init(M, B);
        frontier.reset_stats();

        // Empty pull
        std::vector<uint32_t> S;
        double Bi = 0.0;
        QA_ASSERT(!frontier.pull(S, Bi), "Empty pull returns false");
        QA_ASSERT(S.empty() && Bi == B, "Empty pull returns Bi == B_");
        QA_ASSERT(frontier.last_separator_bound() == B, "last_separator_bound == B_");

        // NaN and out-of-bound inserts
        const double qnan = std::numeric_limits<double>::quiet_NaN();
        frontier.insert(0, qnan);
        frontier.insert(1, -qnan);
        frontier.insert(2, B + 1e-6);
        frontier.insert(3, INF_DIST);
        QA_ASSERT(frontier.empty(), "Frontier ignores NaN and > B_ inserts");

        // Exact boundary dist == B_
        reset_states(INF_DIST);
        states[7].store(VertexState{B, 0, 1});
        frontier.insert(7, B);
        QA_ASSERT(!frontier.empty(), "Exact dist == B_ accepted");
        QA_ASSERT(frontier.pull(S, Bi), "Exact boundary vertex pulled");
        QA_ASSERT(S.size() == 1 && S[0] == 7 && Bi == B, "Pulled boundary vertex with Bi == B");
        QA_ASSERT(frontier.empty(), "Empty after boundary pull");

        // Duplicate keys with decreasing updates (stale reject on insert + deduplication)
        frontier.init(M, B);
        frontier.reset_stats();
        reset_states(INF_DIST);

        states[10].store(VertexState{50.0, 0, 1});
        frontier.insert(10, 50.0);
        states[10].store(VertexState{20.0, 0, 2});
        frontier.insert(10, 20.0);
        // Stale on insert (30.0 > 20.0)
        frontier.insert(10, 30.0);
        // Batch prepend also has duplicate for v=10 and invalid NaN / > B_
        std::vector<VertexDist> k_dup = {
            {10, 25.0}, {10, 20.0}, {11, 20.0}, {12, qnan}, {13, 105.0}
        };
        states[11].store(VertexState{20.0, 0, 1});
        frontier.batch_prepend(k_dup);

        auto stats_dup = frontier.get_stats();
        QA_ASSERT(stats_dup.stale_dropped_on_insert >= 1, "Stale insert rejected");

        std::vector<uint32_t> pulled_dup;
        double prev_b = -INF_DIST;
        while (frontier.pull(S, Bi)) {
            QA_ASSERT(S.size() <= M, "Pull size <= M");
            QA_ASSERT(Bi >= prev_b && Bi <= B, "Monotonic Bi <= B");
            prev_b = Bi;
            pulled_dup.insert(pulled_dup.end(), S.begin(), S.end());
        }
        std::sort(pulled_dup.begin(), pulled_dup.end());
        QA_ASSERT(pulled_dup == (std::vector<uint32_t>{10, 11}),
                  "Duplicate keys collapsed so v=10 and v=11 extracted cleanly once each");
        QA_ASSERT(Bi == B, "Final pull bound equals B_");

        // Equal-distance ties spanning multiple blocks of size M
        frontier.init(M, B);
        frontier.reset_stats();
        reset_states(INF_DIST);
        const uint32_t tie_count = static_cast<uint32_t>(3 * M + 5);
        std::vector<VertexDist> prep_ties;
        for (uint32_t v = 0; v < tie_count; ++v) {
            states[v].store(VertexState{42.0, 0, 1});
            if (v % 3 == 0) {
                prep_ties.push_back(VertexDist{v, 42.0});
            } else {
                frontier.insert(v, 42.0);
            }
        }
        frontier.batch_prepend(prep_ties);

        std::vector<bool> seen(tie_count, false);
        size_t total_ties_pulled = 0;
        prev_b = -INF_DIST;
        while (frontier.pull(S, Bi)) {
            QA_ASSERT(!S.empty() && S.size() <= M, "Tie pull batch non-empty and <= M");
            QA_ASSERT(Bi >= prev_b && Bi <= B, "Tie pull Bi monotonic");
            prev_b = Bi;
            for (uint32_t v : S) {
                QA_ASSERT(v < tie_count && !seen[v], "Each tied vertex pulled once");
                seen[v] = true;
                ++total_ties_pulled;
            }
        }
        QA_ASSERT(total_ties_pulled == tie_count, "All equal-distance tied vertices extracted");
        QA_ASSERT(Bi == B, "Tie drain ends with Bi == B_");

        // All-stale pull returning false with B_i == B_
        frontier.init(M, B);
        frontier.reset_stats();
        reset_states(INF_DIST);
        const uint32_t stale_n = 48;
        std::vector<VertexDist> prep_stale;
        for (uint32_t v = 0; v < stale_n; ++v) {
            states[v].store(VertexState{80.0, 0, 1});
            if (v < stale_n / 2) {
                frontier.insert(v, 80.0);
            } else {
                prep_stale.push_back(VertexDist{v, 80.0});
            }
        }
        frontier.batch_prepend(prep_stale);
        // Make EVERY entry in both D_0 and D_1 stale without inserting new distances
        for (uint32_t v = 0; v < stale_n; ++v) {
            states[v].store(VertexState{5.0, 0, 2});
        }
        Bi = -1.0;
        bool ok_stale = frontier.pull(S, Bi);
        QA_ASSERT(!ok_stale, "All-stale pull must return false");
        QA_ASSERT(S.empty(), "All-stale pull must leave S_i empty");
        QA_ASSERT(Bi == B, "All-stale pull must set B_i == B_");
        QA_ASSERT(frontier.empty(), "Frontier must be empty after all-stale pull");
        QA_ASSERT(frontier.get_stats().stale_dropped_on_pull >= stale_n,
                  "All stale entries accounted for on pull");
    }

    // 1D. Repeated init/clear cycles
    {
        std::vector<AtomicVertexState> states(64);
        ConcurrentFrontierD frontier(states.data(), 64);
        for (int cycle = 0; cycle < 20; ++cycle) {
            size_t m_val = (cycle % 2 == 0) ? 1 : 16;
            double b_val = 50.0 + cycle * 10.0;
            frontier.init(m_val, b_val);
            for (uint32_t v = 0; v < 32; ++v) {
                double d = 10.0 + (v % 10);
                states[v].store(VertexState{d, 0, static_cast<uint32_t>(cycle + 1)});
                frontier.insert(v, d);
            }
            frontier.clear();
            QA_ASSERT(frontier.empty(), "Frontier empty immediately after clear()");
            std::vector<uint32_t> S;
            double Bi = 0.0;
            QA_ASSERT(!frontier.pull(S, Bi) && Bi == b_val,
                      "Pull after clear returns false with current B_");
        }
    }

    std::cout << "    [PASS] Suite 1 passed." << std::endl;
}

// ============================================================================
// SUITE 2: Phased BMSSP-Style Frontier Simulation (8 & 16 Threads)
// ============================================================================
void run_suite2_layered_exact_pull(size_t num_threads) {
    constexpr uint32_t LAYERS = 40;
    constexpr uint32_t WIDTH = 200;
    constexpr uint32_t N = LAYERS * WIDTH; // 8,000 vertices
    constexpr size_t M = 32;

    std::mt19937_64 rng(20261118ULL + num_threads);
    std::uniform_real_distribution<double> w_jitter(0.10, 0.95);
    std::vector<Edge> edges;
    edges.reserve(N * 12);

    // Connect source 0 to all vertices in layer 1 (200 > M=32) so D starts non-trivial
    for (uint32_t c = 0; c < WIDTH; ++c) {
        edges.push_back({0, WIDTH + c, 100.0 + w_jitter(rng)});
    }
    // Connect layer l -> layer l+1 (guaranteed min_dist(l+1) > max_dist(l))
    for (uint32_t l = 1; l + 1 < LAYERS; ++l) {
        for (uint32_t c = 0; c < WIDTH; ++c) {
            uint32_t u = l * WIDTH + c;
            uint32_t next_base = (l + 1) * WIDTH;
            // Deterministic backbone edge + 9 random edges to next layer
            edges.push_back({u, next_base + c, 100.0 + w_jitter(rng)});
            for (int e = 0; e < 9; ++e) {
                uint32_t v = next_base + static_cast<uint32_t>(rng() % WIDTH);
                edges.push_back({u, v, 100.0 + w_jitter(rng)});
            }
        }
    }

    CSRGraph graph = CSRGraph::from_edge_list(N, edges);
    SSSPResult oracle = dijkstra_sequential(graph, 0);

    std::vector<AtomicVertexState> states(N);
    for (uint32_t i = 0; i < N; ++i) {
        states[i].store(VertexState{INF_DIST, NO_PRED, 0});
    }
    states[0].store(VertexState{0.0, 0, 1});

    ConcurrentFrontierD frontier(states.data(), N);
    frontier.init(M, INF_DIST);
    frontier.reset_stats();

    PhaseWorkerPool pool(num_threads);
    std::vector<std::vector<VertexDist>> local_prepend(num_threads);
    std::vector<VertexDist> k_prepend;
    std::vector<uint32_t> extract_count(N, 0);

    // Seed source 0 -> layer 1 (200 > M=32 vertices) so D does not prematurely exhaust on pull #1
    extract_count[0] = 1;
    for (uint64_t e = graph.edge_begin(0); e < graph.edge_end(0); ++e) {
        const uint32_t v = graph.edge_dest(e);
        const double nd = graph.edge_weight(e);
        if (relax_edge_atomic(states[v], nd, 0, 1)) {
            frontier.insert(v, nd);
        }
    }

    std::vector<uint32_t> S_i;
    double B_i = -INF_DIST;
    double prev_Bi = -INF_DIST;
    size_t pull_calls = 0;

    while (frontier.pull(S_i, B_i)) {
        ++pull_calls;
        QA_ASSERT(!S_i.empty() && S_i.size() <= M, "Layered Suite 2A pull size in [1, M]");
        QA_ASSERT(B_i >= prev_Bi, "Strict monotonicity B_1 <= B_2 <= ... <= B");
        prev_Bi = B_i;

        for (uint32_t u : S_i) {
            QA_ASSERT(u < N, "Pulled vertex in bounds");
            ++extract_count[u];
        }

        const double prepend_cutoff = std::isfinite(B_i) ? (B_i + 100.25) : INF_DIST;
        pool.parallel_run([&](size_t tid, size_t nthreads) {
            local_prepend[tid].clear();
            const size_t total = S_i.size();
            const size_t start = (total * tid) / nthreads;
            const size_t end = (total * (tid + 1)) / nthreads;
            for (size_t idx = start; idx < end; ++idx) {
                const uint32_t u = S_i[idx];
                const double du = states[u].load(std::memory_order_acquire).dist;
                for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
                    const uint32_t v = graph.edge_dest(e);
                    const double nd = du + graph.edge_weight(e);
                    QA_ASSERT(nd >= B_i || !std::isfinite(B_i), "Layered graph preserves nd >= B_i");
                    if (relax_edge_atomic(states[v], nd, u, static_cast<uint32_t>(pull_calls))) {
                        if (nd < prepend_cutoff && std::isfinite(B_i)) {
                            local_prepend[tid].push_back(VertexDist{v, nd});
                        } else {
                            frontier.insert(v, nd);
                        }
                    }
                }
            }
        });

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
    }

    QA_ASSERT(B_i == INF_DIST, "Final Bi must equal B_ (INF_DIST)");
    QA_ASSERT(frontier.empty(), "Frontier empty at completion");

    // Verify 100% vertex extraction coverage and 100% distance accuracy
    uint32_t reachable = 0;
    for (uint32_t v = 0; v < N; ++v) {
        if (std::isinf(oracle.dist[v])) {
            QA_ASSERT(std::isinf(states[v].load().dist), "Unreachable vertex stays INF");
            QA_ASSERT(extract_count[v] == 0, "Unreachable vertex never extracted");
        } else {
            ++reachable;
            QA_ASSERT(extract_count[v] == 1, "Every reachable vertex extracted exactly once via D.pull()");
            QA_ASSERT(std::abs(states[v].load().dist - oracle.dist[v]) < 1e-9,
                      "Layered Suite 2A exact distance match");
        }
    }
    std::cout << "    [PASS] Suite 2A (" << num_threads << "T layered exact pull): "
              << pull_calls << " pulls, " << reachable << " vertices 100% exact & extracted once."
              << std::endl;
}

void run_suite2_general_bmssp_phased(size_t num_threads) {
    constexpr uint32_t N = 10000;
    constexpr uint64_t TARGET_EDGES = 80000;
    constexpr size_t M = 64;

    std::mt19937_64 rng(20261119ULL + num_threads);
    std::uniform_real_distribution<double> w_dist(0.5, 50.0);
    std::vector<Edge> edges;
    edges.reserve(TARGET_EDGES + 500);

    // Directed Hamiltonian backbone to guarantee reachability
    for (uint32_t i = 0; i + 1 < N; ++i) {
        edges.push_back({i, i + 1, w_dist(rng)});
    }
    // Broad initial out-degree from source 0 so initial D has > M vertices
    for (uint32_t i = 1; i <= 256; ++i) {
        edges.push_back({0, static_cast<uint32_t>((i * 37) % N), w_dist(rng)});
    }
    while (edges.size() < TARGET_EDGES) {
        uint32_t u = static_cast<uint32_t>(rng() % N);
        uint32_t v = static_cast<uint32_t>(rng() % N);
        if (u == v) continue;
        // 5% zero-weight edges to exercise equal-distance ties and cycles
        double w = (rng() % 20 == 0) ? 0.0 : w_dist(rng);
        edges.push_back({u, v, w});
    }

    CSRGraph graph = CSRGraph::from_edge_list(N, edges);
    SSSPResult oracle = dijkstra_sequential(graph, 0);

    std::vector<AtomicVertexState> states(N);
    for (uint32_t i = 0; i < N; ++i) {
        states[i].store(VertexState{INF_DIST, NO_PRED, 0});
    }
    states[0].store(VertexState{0.0, 0, 1});

    ConcurrentFrontierD frontier(states.data(), N);
    frontier.init(M, INF_DIST);
    frontier.reset_stats();

    // Initial seed: relax source 0 so > 256 vertices populate D before phased loop
    for (uint64_t e = graph.edge_begin(0); e < graph.edge_end(0); ++e) {
        uint32_t v = graph.edge_dest(e);
        double nd = graph.edge_weight(e);
        if (relax_edge_atomic(states[v], nd, 0, 1)) {
            frontier.insert(v, nd);
        }
    }

    PhaseWorkerPool pool(num_threads);
    EpochDeduplicator wave_dedup(N);
    std::vector<std::vector<uint32_t>> local_subwave(num_threads);
    std::vector<std::vector<VertexDist>> local_prepend(num_threads);
    std::vector<uint32_t> active_wave;
    std::vector<VertexDist> k_prepend;
    std::vector<bool> settled_any(N, false);
    settled_any[0] = true;

    std::vector<uint32_t> S_i;
    double B_i = -INF_DIST;
    double prev_Bi = -INF_DIST;
    uint32_t global_epoch = 2;
    size_t pull_calls = 0;

    while (frontier.pull(S_i, B_i)) {
        ++pull_calls;
        QA_ASSERT(!S_i.empty() && S_i.size() <= M, "General Suite 2B pull size in [1, M]");
        QA_ASSERT(B_i >= prev_Bi, "Strict pull bound monotonicity B_1 <= B_2 <= ... <= B");
        prev_Bi = B_i;

        active_wave = S_i;
        for (uint32_t u : active_wave) {
            settled_any[u] = true;
        }

        const double prepend_band = std::isfinite(B_i) ? (B_i + 5.0) : INF_DIST;
        for (size_t t = 0; t < num_threads; ++t) {
            local_prepend[t].clear();
        }

        // Parallel sub-waves settle all relaxed paths strictly < B_i before next pull
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
                for (uint32_t v : local_subwave[t]) {
                    settled_any[v] = true;
                    active_wave.push_back(v);
                }
            }
        }

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
    }

    QA_ASSERT(B_i == INF_DIST, "Final pull bound equals B_");
    QA_ASSERT(frontier.empty(), "Frontier empty after draining");

    for (uint32_t v = 0; v < N; ++v) {
        const double got = states[v].load().dist;
        const double want = oracle.dist[v];
        if (std::isinf(want)) {
            QA_ASSERT(std::isinf(got) && !settled_any[v], "Unreachable vertex matches INF");
        } else {
            QA_ASSERT(settled_any[v], "Every reachable vertex settled/extracted");
            QA_ASSERT(std::abs(got - want) < 1e-9, "Exact SSSP distance vs Dijkstra oracle");
        }
    }

    auto st = frontier.get_stats();
    std::cout << "    [PASS] Suite 2B (" << num_threads << "T cyclic BMSSP sim): "
              << pull_calls << " pulls, overflows=" << st.overflow_events
              << ", splits=" << st.actual_splits
              << ", saved=" << st.eager_cleanup_saved_splits
              << ", stale_ins=" << st.stale_dropped_on_insert
              << ", stale_pull=" << st.stale_dropped_on_pull << std::endl;
}

void test_suite_2_phased_bmssp() {
    std::cout << ">>> [Suite 2] Phased BMSSP-style frontier simulation (8 & 16 threads)..." << std::endl;
    run_suite2_layered_exact_pull(8);
    run_suite2_layered_exact_pull(16);
    run_suite2_general_bmssp_phased(8);
    run_suite2_general_bmssp_phased(16);
    std::cout << "    [PASS] Suite 2 passed." << std::endl;
}

// ============================================================================
// SUITE 3: Eager Cleanup Split-Reduction Verification (>= 50% target)
// ============================================================================
void test_suite_3_eager_cleanup() {
    std::cout << ">>> [Suite 3] Eager Cleanup split-reduction verification..." << std::endl;

    constexpr uint32_t N = 2048;
    constexpr uint64_t TARGET_EDGES = 200000;
    constexpr size_t M = 32;
    constexpr size_t NUM_THREADS = 8;

    // Build an identical dense directed graph (N=2048, ~200,000 edges) with non-negative weights
    std::mt19937_64 rng(20261120ULL);
    std::uniform_real_distribution<double> w_dist(1.0, 100.0);
    std::vector<Edge> edges;
    edges.reserve(TARGET_EDGES + 64);
    for (uint32_t i = 0; i + 1 < N; ++i) {
        edges.push_back({i, i + 1, w_dist(rng)});
    }
    while (edges.size() < TARGET_EDGES) {
        uint32_t u = static_cast<uint32_t>(rng() % N);
        uint32_t v = static_cast<uint32_t>(rng() % N);
        if (u == v) continue;
        edges.push_back({u, v, w_dist(rng)});
    }
    CSRGraph graph = CSRGraph::from_edge_list(N, edges);
    SSSPResult oracle = dijkstra_sequential(graph, 0);

    auto run_mode = [&](bool eager_enabled,
                        ConcurrentFrontierD::FrontierStats& out_stats,
                        std::vector<double>& out_final_dists,
                        std::vector<uint32_t>& out_pulled_vertices) {
        std::vector<AtomicVertexState> states(N);
        for (uint32_t v = 0; v < N; ++v) {
            states[v].store(VertexState{INF_DIST, NO_PRED, 0});
        }
        states[0].store(VertexState{0.0, 0, 1});

        ConcurrentFrontierD frontier(states.data(), N);
        frontier.set_eager_cleanup(eager_enabled);
        frontier.init(M, INF_DIST);
        frontier.reset_stats();
        frontier.insert(0, 0.0);

        PhaseWorkerPool pool(NUM_THREADS);
        EpochDeduplicator dedup(N);
        std::vector<std::vector<uint32_t>> local_next(NUM_THREADS);
        std::vector<uint32_t> active = {0};
        uint32_t epoch = 1;

        // Multi-wave parallel dense graph edge relaxation inserting every improvement into D
        while (!active.empty()) {
            const uint32_t ep = ++epoch;
            pool.parallel_run([&](size_t tid, size_t nthreads) {
                local_next[tid].clear();
                const size_t total = active.size();
                const size_t start = (total * tid) / nthreads;
                const size_t end = (total * (tid + 1)) / nthreads;
                for (size_t idx = start; idx < end; ++idx) {
                    const uint32_t u = active[idx];
                    const double du = states[u].load(std::memory_order_acquire).dist;
                    for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
                        const uint32_t v = graph.edge_dest(e);
                        const double nd = du + graph.edge_weight(e);
                        if (relax_edge_atomic(states[v], nd, u, ep)) {
                            frontier.insert(v, nd);
                            if (dedup.try_claim(v, ep)) {
                                local_next[tid].push_back(v);
                            }
                        }
                    }
                }
            });

            active.clear();
            for (size_t t = 0; t < NUM_THREADS; ++t) {
                active.insert(active.end(), local_next[t].begin(), local_next[t].end());
            }
        }

        out_stats = frontier.get_stats();

        out_final_dists.resize(N);
        for (uint32_t v = 0; v < N; ++v) {
            out_final_dists[v] = states[v].load(std::memory_order_acquire).dist;
            QA_ASSERT(std::abs(out_final_dists[v] - oracle.dist[v]) < 1e-9,
                      "Suite 3 dense graph relaxation matches Dijkstra oracle");
        }

        std::vector<uint32_t> S;
        double Bi = -INF_DIST;
        double prev_Bi = -INF_DIST;
        out_pulled_vertices.clear();
        while (frontier.pull(S, Bi)) {
            QA_ASSERT(Bi >= prev_Bi, "Suite 3 pull Bi monotonic");
            prev_Bi = Bi;
            out_pulled_vertices.insert(out_pulled_vertices.end(), S.begin(), S.end());
        }
        std::sort(out_pulled_vertices.begin(), out_pulled_vertices.end());
    };

    ConcurrentFrontierD::FrontierStats stats_off{}, stats_on{};
    std::vector<double> dists_off, dists_on;
    std::vector<uint32_t> pulled_off, pulled_on;

    run_mode(false, stats_off, dists_off, pulled_off);
    run_mode(true, stats_on, dists_on, pulled_on);

    QA_ASSERT(stats_off.actual_splits > 0, "Eager-OFF must trigger splits");
    const double split_reduction_pct =
        100.0 * static_cast<double>(stats_off.actual_splits - stats_on.actual_splits) /
        static_cast<double>(stats_off.actual_splits);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "    [Eager Cleanup OFF] overflow_events=" << stats_off.overflow_events
              << ", actual_splits=" << stats_off.actual_splits
              << ", eager_cleanup_saved_splits=" << stats_off.eager_cleanup_saved_splits
              << ", stale_dropped_on_insert=" << stats_off.stale_dropped_on_insert << std::endl;
    std::cout << "    [Eager Cleanup ON ] overflow_events=" << stats_on.overflow_events
              << ", actual_splits=" << stats_on.actual_splits
              << ", eager_cleanup_saved_splits=" << stats_on.eager_cleanup_saved_splits
              << ", stale_dropped_on_insert=" << stats_on.stale_dropped_on_insert << std::endl;
    std::cout << "    [Split Reduction  ] " << split_reduction_pct
              << "% (" << stats_off.actual_splits << " -> " << stats_on.actual_splits << ")"
              << std::endl;

    QA_ASSERT(pulled_off.size() == N && pulled_on.size() == N,
              "Both modes must drain all N active vertices exactly once");
    QA_ASSERT(pulled_off == pulled_on, "Both modes must extract identical vertex set");
    QA_ASSERT(split_reduction_pct >= 50.0, "Eager Cleanup split reduction must be >= 50.0%");

    std::cout << "    [PASS] Suite 3 passed (>= 50% split reduction verified)." << std::endl;
}

// ============================================================================
// SUITE 4: 32-Thread 10,000,000-Operation Concurrent Stress Test
// ============================================================================
void test_suite_4_32thread_10m_stress() {
    std::cout << ">>> [Suite 4] Full 32-thread 10,000,000-operation concurrent stress test..." << std::endl;

    constexpr size_t NUM_THREADS = 32;
    constexpr size_t NUM_ROUNDS = 10;
    constexpr size_t OPS_PER_THREAD_PER_ROUND = 31250; // 32 * 31,250 * 10 = 10,000,000
    constexpr uint32_t N = 16384;
    constexpr size_t M = 64;
    constexpr double B = 100000.0;

    std::vector<AtomicVertexState> states(N);
    ConcurrentFrontierD frontier(states.data(), N);
    frontier.set_eager_cleanup(true);

    std::atomic<uint64_t> total_ops_executed{0};
    std::atomic<uint64_t> total_inserts{0};
    std::atomic<uint64_t> total_pulls{0};
    std::atomic<uint64_t> total_prepends{0};

    auto t0 = std::chrono::high_resolution_clock::now();

    for (size_t round = 0; round < NUM_ROUNDS; ++round) {
        for (uint32_t v = 0; v < N; ++v) {
            states[v].store(VertexState{INF_DIST, NO_PRED, 0});
        }
        frontier.init(M, B);

        std::atomic<size_t> ready{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> workers;
        workers.reserve(NUM_THREADS);

        for (size_t tid = 0; tid < NUM_THREADS; ++tid) {
            workers.emplace_back([&, tid, round]() {
                uint64_t rng_s = 0x9E3779B97F4A7C15ULL ^ (tid * 1000003ULL + round * 131071ULL + 17ULL);
                auto next_u64 = [&rng_s]() noexcept -> uint64_t {
                    rng_s ^= rng_s >> 12;
                    rng_s ^= rng_s << 25;
                    rng_s ^= rng_s >> 27;
                    return rng_s * 0x2545F4914F6CDD1DULL;
                };

                std::vector<uint32_t> local_S;
                std::vector<VertexDist> local_K;
                local_S.reserve(M);
                local_K.reserve(16);

                ready.fetch_add(1, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) {
                    cpu_pause();
                }

                uint64_t ops_ins = 0;
                uint64_t ops_pull = 0;
                uint64_t ops_prep = 0;

                for (size_t op = 0; op < OPS_PER_THREAD_PER_ROUND; ++op) {
                    const uint64_t r = next_u64();
                    // Threads 0..3 interleave pull() + batch_prepend() + insert() concurrently
                    // while Threads 4..31 hammer concurrent relax + insert().
                    const uint64_t selector = (tid < 4) ? (op % 10) : 0;

                    if (selector == 7) {
                        double Bi = -1.0;
                        bool pulled = frontier.pull(local_S, Bi);
                        QA_ASSERT(local_S.size() <= M, "Concurrent pull size <= M");
                        QA_ASSERT(!std::isnan(Bi) && Bi <= B, "Concurrent pull Bi finite/bounded");
                        if (pulled) {
                            for (uint32_t v : local_S) {
                                QA_ASSERT(v < N, "Pulled vertex in range");
                            }
                        } else {
                            QA_ASSERT(local_S.empty() && Bi == B, "Empty concurrent pull sets Bi=B");
                        }
                        ++ops_pull;
                    } else if (selector == 9) {
                        local_K.clear();
                        const size_t k_cnt = 1 + (r % 8);
                        for (size_t j = 0; j < k_cnt; ++j) {
                            uint32_t v = static_cast<uint32_t>(next_u64() % N);
                            double d = 1.0 + static_cast<double>(next_u64() % 900000) * 0.1;
                            if (relax_edge_atomic(states[v], d, static_cast<uint32_t>(tid),
                                                  static_cast<uint32_t>(round + 1))) {
                                local_K.push_back(VertexDist{v, d});
                            }
                        }
                        if (!local_K.empty()) {
                            frontier.batch_prepend(local_K);
                        }
                        ++ops_prep;
                    } else {
                        uint32_t v = static_cast<uint32_t>(r % N);
                        double progress = static_cast<double>(op) / static_cast<double>(OPS_PER_THREAD_PER_ROUND);
                        double d = 1.0 + (1.0 - 0.92 * progress) * 90000.0 +
                                   static_cast<double>((r >> 16) % 5000) * 0.01;
                        if (relax_edge_atomic(states[v], d, static_cast<uint32_t>(tid),
                                              static_cast<uint32_t>(round + 1))) {
                            frontier.insert(v, d);
                        }
                        ++ops_ins;
                    }
                }

                total_inserts.fetch_add(ops_ins, std::memory_order_relaxed);
                total_pulls.fetch_add(ops_pull, std::memory_order_relaxed);
                total_prepends.fetch_add(ops_prep, std::memory_order_relaxed);
                total_ops_executed.fetch_add(ops_ins + ops_pull + ops_prep, std::memory_order_relaxed);
            });
        }

        while (ready.load(std::memory_order_acquire) < NUM_THREADS) {
            cpu_pause();
        }
        go.store(true, std::memory_order_release);
        for (auto& w : workers) {
            w.join();
        }

        // Verify finite monotonic state consistency across all N vertices
        for (uint32_t v = 0; v < N; ++v) {
            const VertexState st = states[v].load(std::memory_order_acquire);
            QA_ASSERT(!std::isnan(st.dist), "Vertex state dist must never be NaN");
            if (std::isfinite(st.dist)) {
                QA_ASSERT(st.dist >= 0.0 && st.dist <= B, "Relaxed dist within [0, B]");
                QA_ASSERT(st.pred < NUM_THREADS, "Consistent pred from worker");
            }
        }

        // Post-round sequential drain: strict monotonicity B_1 <= B_2 <= ... <= B must hold
        std::vector<uint32_t> drain_S;
        double drain_Bi = -INF_DIST;
        double prev_drain_Bi = -INF_DIST;
        while (frontier.pull(drain_S, drain_Bi)) {
            QA_ASSERT(!drain_S.empty() && drain_S.size() <= M, "Drain batch size in [1, M]");
            QA_ASSERT(drain_Bi >= prev_drain_Bi && drain_Bi <= B,
                      "Post-round drain Bi strictly non-decreasing");
            prev_drain_Bi = drain_Bi;
        }
        QA_ASSERT(drain_Bi == B, "Post-round drain finishes at Bi == B");
        QA_ASSERT(frontier.empty(), "Frontier empty after post-round drain");

        frontier.clear();
        QA_ASSERT(frontier.empty(), "Frontier empty after clear()");
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const uint64_t expected_total = NUM_THREADS * NUM_ROUNDS * OPS_PER_THREAD_PER_ROUND;
    QA_ASSERT(total_ops_executed.load() == expected_total, "Exact 10,000,000 operations completed");

    auto stats = frontier.get_stats();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "    Total ops: " << total_ops_executed.load()
              << " (inserts=" << total_inserts.load()
              << ", pulls=" << total_pulls.load()
              << ", batch_prepends=" << total_prepends.load() << ")" << std::endl;
    std::cout << "    Rounds: " << NUM_ROUNDS << " | Threads: " << NUM_THREADS
              << " | Elapsed: " << elapsed_ms << " ms ("
              << (static_cast<double>(expected_total) / (elapsed_ms * 1000.0)) << " Mops/s)"
              << std::endl;
    std::cout << "    Stats: overflow_events=" << stats.overflow_events
              << ", actual_splits=" << stats.actual_splits
              << ", saved_splits=" << stats.eager_cleanup_saved_splits
              << ", stale_ins=" << stats.stale_dropped_on_insert
              << ", stale_pull=" << stats.stale_dropped_on_pull << std::endl;
    std::cout << "    [PASS] Suite 4 passed (zero deadlocks, zero crashes, full cleanup)." << std::endl;
}

} // namespace

int main() {
    std::cout << "==================================================================" << std::endl;
    std::cout << "  BMSSP Parallel - Person 2 ConcurrentFrontierD QA Stress Suite   " << std::endl;
    std::cout << "==================================================================" << std::endl;

    auto t_start = std::chrono::high_resolution_clock::now();

    test_suite_1_edge_cases();
    test_suite_2_phased_bmssp();
    test_suite_3_eager_cleanup();
    test_suite_4_32thread_10m_stress();

    auto t_end = std::chrono::high_resolution_clock::now();
    const double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "==================================================================" << std::endl;
    std::cout << "  ALL PERSON 2 STRESS TESTS PASSED! (" << g_checks_passed.load() << "/"
              << g_checks_total.load() << " checks)" << std::endl;
    std::cout << "  Total execution time: " << std::fixed << std::setprecision(2)
              << total_ms << " ms" << std::endl;
    std::cout << "==================================================================" << std::endl;

    return 0;
}
