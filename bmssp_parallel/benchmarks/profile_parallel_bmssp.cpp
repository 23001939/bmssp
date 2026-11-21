#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"
#include "core/profiler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace bmssp {
namespace {

CSRGraph make_sparse_random(uint32_t n, uint64_t m, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist_v(0, n - 1);
    std::uniform_real_distribution<double> dist_w(1.0, 100.0);

    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(m));
    for (uint64_t i = 0; i < m; ++i) {
        uint32_t u = dist_v(rng);
        uint32_t v = dist_v(rng);
        if (u == v) v = (u + 1U) % n;
        edges.push_back({u, v, dist_w(rng)});
    }
    return CSRGraph::from_edge_list(n, edges);
}

CSRGraph make_grid_2d(uint32_t rows, uint32_t cols, uint64_t seed) {
    const uint32_t n = rows * cols;
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> dist_w(1.0, 50.0);

    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * 4ULL);
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            uint32_t u = r * cols + c;
            if (c + 1 < cols) edges.push_back({u, u + 1, dist_w(rng)});
            if (c > 0)        edges.push_back({u, u - 1, dist_w(rng)});
            if (r + 1 < rows) edges.push_back({u, u + cols, dist_w(rng)});
            if (r > 0)        edges.push_back({u, u - cols, dist_w(rng)});
        }
    }
    return CSRGraph::from_edge_list(n, edges);
}

CSRGraph make_hub_graph(uint32_t n, uint32_t hub_deg, uint64_t bg_edges, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist_v(0, n - 1);
    std::uniform_real_distribution<double> dist_w(1.0, 100.0);

    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(hub_deg) + static_cast<size_t>(bg_edges));
    for (uint32_t v = 1; v <= hub_deg && v < n; ++v) {
        edges.push_back({0, v, dist_w(rng)});
    }
    for (uint64_t i = 0; i < bg_edges; ++i) {
        uint32_t u = dist_v(rng);
        uint32_t v = dist_v(rng);
        if (u == v) v = (u + 1U) % n;
        edges.push_back({u, v, dist_w(rng)});
    }
    return CSRGraph::from_edge_list(n, edges);
}

bool verify_dist(const SSSPResult& oracle, const SSSPResult& res, double tol) {
    if (oracle.dist.size() != res.dist.size()) return false;
    for (size_t i = 0; i < oracle.dist.size(); ++i) {
        double a = oracle.dist[i], b = res.dist[i];
        if (std::isinf(a) || std::isinf(b)) {
            if (std::isinf(a) != std::isinf(b)) return false;
        } else if (std::abs(a - b) > tol) {
            return false;
        }
    }
    return true;
}

double ns_to_ms(uint64_t ns, int reps) {
    return (static_cast<double>(ns) / 1e6) / static_cast<double>(reps);
}

double pct(uint64_t part, uint64_t total) {
    return (total > 0) ? (100.0 * static_cast<double>(part) / static_cast<double>(total)) : 0.0;
}

} // namespace
} // namespace bmssp

int main(int argc, char** argv) {
    using namespace bmssp;
#ifndef BMSSP_ENABLE_PROFILING
    std::cerr << "Error: compile with -DBMSSP_ENABLE_PROFILING\n";
    return 1;
#else
    int only_case = -1;
    if (argc >= 2) {
        only_case = std::atoi(argv[1]);
    }
    struct Case {
        std::string name;
        CSRGraph g;
        int reps;
    };

    std::vector<Case> cases;
    if (only_case < 0 || only_case == 0)
        cases.push_back({"Sparse Random 200K (m=1M)", make_sparse_random(200000, 1000000, 200000), 3});
    if (only_case < 0 || only_case == 1)
        cases.push_back({"Dense-20 Random 200K (m=4M)", make_sparse_random(200000, 4000000, 200013), 3});
    if (only_case < 0 || only_case == 2)
        cases.push_back({"Hub 100K (deg0=50K, m=450K)", make_hub_graph(100000, 50000, 400000, 100011), 3});
    if (only_case < 0 || only_case == 3)
        cases.push_back({"2D Grid 300x300 (n=90K, m=359K)", make_grid_2d(300, 300, 90000), 3});

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=== BMSSP PHASE-LEVEL PROFILING (ConcurrentFrontierD, HW threads="
              << std::thread::hardware_concurrency() << ") ===\n";

    for (const auto& tc : cases) {
        SSSPResult oracle = dijkstra_sequential(tc.g, 0);
        std::cout << "\n## " << tc.name << " (n=" << tc.g.n << ", m=" << tc.g.m << ")\n";

        for (size_t threads : {size_t{1}, size_t{4}, size_t{8}}) {
            // 1 warmup
            auto w = bmssp_parallel(tc.g, 0, threads, false, FrontierType::Concurrent);
            if (!verify_dist(oracle, w, 1e-9)) {
                std::cerr << "[FAIL] Oracle mismatch on " << tc.name << " (" << threads << "T)\n";
                return 1;
            }

            auto& p = get_global_profile_stats();
            p.reset();
            for (int r = 0; r < tc.reps; ++r) {
                auto res = bmssp_parallel(tc.g, 0, threads, false, FrontierType::Concurrent);
                if (!verify_dist(oracle, res, 1e-9)) {
                    std::cerr << "[FAIL] Oracle mismatch on " << tc.name << " (" << threads << "T)\n";
                    return 1;
                }
            }

            const uint64_t total = p.total_solve_ns;
            const uint64_t relax_total = p.relax_par_est_wall_ns + p.relax_seq_ns;
            const uint64_t ins_total = p.insert_par_est_wall_ns + p.insert_batch_seq_ns;
            const uint64_t accounted =
                p.find_pivots_total_ns + p.base_case_ns + p.d_init_ns + p.d_pull_ns +
                p.k_sort_ns + p.d_batch_prepend_ns + p.relax_par_wall_ns +
                p.relax_seq_ns + p.insert_batch_seq_ns;
            const uint64_t other = (total > accounted) ? (total - accounted) : 0;

            const double fp_imb = (p.fp_par_thread_sum_ns > 0 && threads > 1)
                ? (static_cast<double>(p.fp_par_thread_max_ns) * static_cast<double>(threads) /
                   static_cast<double>(p.fp_par_thread_sum_ns))
                : 1.0;
            const double rel_imb = (p.relax_par_thread_sum_ns > 0 && threads > 1)
                ? (static_cast<double>(p.relax_par_thread_max_ns) * static_cast<double>(threads) /
                   static_cast<double>(p.relax_par_thread_sum_ns))
                : 1.0;
            const double ins_imb = (p.insert_par_thread_sum_ns > 0 && threads > 1)
                ? (static_cast<double>(p.insert_par_thread_max_ns) * static_cast<double>(threads) /
                   static_cast<double>(p.insert_par_thread_sum_ns))
                : 1.0;

            const uint64_t total_fp_edges = p.fp_par_edges + p.fp_seq_edges;
            const uint64_t total_rel_edges = p.relax_par_edges + p.relax_seq_edges;

            std::cout << "\n### Threads = " << threads << " (avg of " << tc.reps
                      << " runs, total_solve = " << ns_to_ms(total, tc.reps) << " ms)\n";
            std::cout << "| Phase | Avg (ms) | Share (%) | Calls/run | Notes |\n";
            std::cout << "|---|---:|---:|---:|---|\n";
            std::cout << "| FindPivots (total) | " << ns_to_ms(p.find_pivots_total_ns, tc.reps)
                      << " | " << pct(p.find_pivots_total_ns, total) << "% | " << (p.fp_calls / tc.reps)
                      << " | par=" << ns_to_ms(p.fp_relax_par_ns, tc.reps)
                      << "ms, seq=" << ns_to_ms(p.fp_relax_seq_ns, tc.reps)
                      << "ms, forest=" << ns_to_ms(p.fp_forest_ns, tc.reps)
                      << "ms, par_edges=" << pct(p.fp_par_edges, total_fp_edges) << "% (imb=" << fp_imb << "x) |\n";
            std::cout << "| BaseCase (serial) | " << ns_to_ms(p.base_case_ns, tc.reps)
                      << " | " << pct(p.base_case_ns, total) << "% | " << (p.base_case_calls / tc.reps)
                      << " | level=0 local Dijkstra |\n";
            std::cout << "| D.init (serial) | " << ns_to_ms(p.d_init_ns, tc.reps)
                      << " | " << pct(p.d_init_ns, total) << "% | " << (p.d_init_calls / tc.reps)
                      << " | reset per-level D |\n";
            std::cout << "| D.Pull (serial) | " << ns_to_ms(p.d_pull_ns, tc.reps)
                      << " | " << pct(p.d_pull_ns, total) << "% | " << (p.d_pull_calls / tc.reps)
                      << " | drain D0/D1 + dedup + sort |\n";
            std::cout << "| K sort (sample_sort) | " << ns_to_ms(p.k_sort_ns, tc.reps)
                      << " | " << pct(p.k_sort_ns, total) << "% | " << (p.d_batch_prepend_calls / tc.reps)
                      << " | sort batch K before prepend |\n";
            std::cout << "| D.BatchPrepend (serial) | " << ns_to_ms(p.d_batch_prepend_ns, tc.reps)
                      << " | " << pct(p.d_batch_prepend_ns, total) << "% | " << (p.d_batch_prepend_calls / tc.reps)
                      << " | prepend K to D0 |\n";
            std::cout << "| Edge Relaxation (pure) | " << ns_to_ms(relax_total, tc.reps)
                      << " | " << pct(relax_total, total) << "% | " << ((p.relax_par_calls + p.relax_seq_calls) / tc.reps)
                      << " | par_calls=" << (p.relax_par_calls / tc.reps)
                      << ", seq_calls=" << (p.relax_seq_calls / tc.reps)
                      << ", par_edges=" << pct(p.relax_par_edges, total_rel_edges)
                      << "% (imb=" << rel_imb << "x) |\n";
            std::cout << "| insert_batch (flush D) | " << ns_to_ms(ins_total, tc.reps)
                      << " | " << pct(ins_total, total) << "% | " << (p.insert_batch_items / tc.reps)
                      << " items | par_est=" << ns_to_ms(p.insert_par_est_wall_ns, tc.reps)
                      << "ms, seq=" << ns_to_ms(p.insert_batch_seq_ns, tc.reps)
                      << "ms (imb=" << ins_imb << "x) |\n";
            std::cout << "| Other (recursion/glue) | " << ns_to_ms(other, tc.reps)
                      << " | " << pct(other, total) << "% | - | gather K, complete bookkeep, pivot insert |\n";
        }
    }
    return 0;
#endif
}
