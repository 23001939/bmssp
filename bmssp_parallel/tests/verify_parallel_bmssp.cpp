#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace bmssp {

void verify_predecessor_tree(
    const CSRGraph& g,
    uint32_t source,
    const SSSPResult& res,
    double tol,
    const std::string& test_name
) {
    if (res.dist.size() != g.n || res.pred.size() != g.n) {
        std::cerr << "[-] FAILED " << test_name << ": dist/pred vector size mismatch (n="
                  << g.n << ", dist=" << res.dist.size() << ", pred=" << res.pred.size() << ")" << std::endl;
        std::exit(1);
    }
    if (g.n == 0 || source >= g.n) {
        return;
    }

    if (res.pred[source] != NO_PRED) {
        std::cerr << "[-] FAILED " << test_name << ": pred[source=" << source
                  << "] expected NO_PRED, got " << res.pred[source] << std::endl;
        std::exit(1);
    }
    if (!std::isfinite(res.dist[source]) || std::abs(res.dist[source]) > tol) {
        std::cerr << "[-] FAILED " << test_name << ": dist[source=" << source
                  << "] expected 0.0, got " << res.dist[source] << std::endl;
        std::exit(1);
    }

    for (uint32_t v = 0; v < g.n; ++v) {
        if (std::isinf(res.dist[v])) {
            if (res.pred[v] != NO_PRED) {
                std::cerr << "[-] FAILED " << test_name << ": unreachable vertex " << v
                          << " has pred=" << res.pred[v] << " (expected NO_PRED)" << std::endl;
                std::exit(1);
            }
        } else if (v != source) {
            uint32_t u = res.pred[v];
            if (u == NO_PRED || u >= g.n) {
                std::cerr << "[-] FAILED " << test_name << ": reachable vertex " << v
                          << " has invalid pred=" << u << std::endl;
                std::exit(1);
            }
            if (u == v) {
                std::cerr << "[-] FAILED " << test_name << ": self-loop predecessor at vertex " << v << std::endl;
                std::exit(1);
            }
            if (!std::isfinite(res.dist[u])) {
                std::cerr << "[-] FAILED " << test_name << ": vertex " << v
                          << " points to unreachable pred " << u << std::endl;
                std::exit(1);
            }
        }
    }

    // Match every reachable vertex v != source to an edge (pred[v], v, w) in O(n + m)
    std::vector<uint8_t> edge_matched(g.n, 0);
    std::vector<double> best_err(g.n, INF_DIST);
    std::vector<double> matched_w(g.n, 0.0);

    for (uint32_t u = 0; u < g.n; ++u) {
        if (!std::isfinite(res.dist[u])) continue;
        for (uint64_t e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            uint32_t v = g.edge_dest(e);
            if (v >= g.n || v == source || !std::isfinite(res.dist[v])) continue;
            if (res.pred[v] != u) continue;
            double w = g.edge_weight(e);
            double err = std::abs(res.dist[u] + w - res.dist[v]);
            if (!edge_matched[v] || err < best_err[v]) {
                edge_matched[v] = 1;
                best_err[v] = err;
                matched_w[v] = w;
            }
        }
    }

    for (uint32_t v = 0; v < g.n; ++v) {
        if (v != source && std::isfinite(res.dist[v])) {
            if (!edge_matched[v] || best_err[v] > tol) {
                std::cerr << "[-] FAILED " << test_name << ": missing/inconsistent edge (pred="
                          << res.pred[v] << " -> " << v << "), best_err=" << best_err[v] << std::endl;
                std::exit(1);
            }
        }
    }

    // Verify no cycles and that summed edge weights match dist in O(n)
    std::vector<uint8_t> color(g.n, 0); // 0 = unvisited, 1 = visiting, 2 = verified
    std::vector<double> path_dist(g.n, INF_DIST);
    std::vector<uint32_t> depth(g.n, 0);
    color[source] = 2;
    path_dist[source] = 0.0;
    depth[source] = 0;

    std::vector<uint32_t> chain;
    for (uint32_t v = 0; v < g.n; ++v) {
        if (!std::isfinite(res.dist[v]) || color[v] == 2) continue;

        chain.clear();
        uint32_t cur = v;
        while (color[cur] != 2) {
            if (color[cur] == 1) {
                std::cerr << "[-] FAILED " << test_name << ": cycle detected in pred chain at vertex "
                          << cur << std::endl;
                std::exit(1);
            }
            if (chain.size() >= g.n) {
                std::cerr << "[-] FAILED " << test_name << ": pred chain length exceeds n at vertex "
                          << cur << std::endl;
                std::exit(1);
            }
            color[cur] = 1;
            chain.push_back(cur);
            cur = res.pred[cur];
            if (cur >= g.n || !std::isfinite(res.dist[cur])) {
                std::cerr << "[-] FAILED " << test_name << ": invalid pred " << cur
                          << " encountered while tracing vertex " << v << std::endl;
                std::exit(1);
            }
        }

        for (size_t idx = chain.size(); idx-- > 0;) {
            uint32_t x = chain[idx];
            uint32_t p = res.pred[x];
            depth[x] = depth[p] + 1;
            if (depth[x] >= g.n) {
                std::cerr << "[-] FAILED " << test_name << ": path depth >= n at vertex " << x << std::endl;
                std::exit(1);
            }
            path_dist[x] = path_dist[p] + matched_w[x];
            double path_tol = tol * static_cast<double>(depth[x]);
            if (std::abs(path_dist[x] - res.dist[x]) > path_tol) {
                std::cerr << "[-] FAILED " << test_name << ": path weight sum mismatch at vertex " << x
                          << " (sum=" << path_dist[x] << ", dist=" << res.dist[x]
                          << ", tol=" << path_tol << ")" << std::endl;
                std::exit(1);
            }
            color[x] = 2;
        }
    }
}

void assert_results_match(
    const CSRGraph& g,
    uint32_t source,
    const SSSPResult& oracle,
    const SSSPResult& test_res,
    const std::string& test_name,
    double tol = 1e-7
) {
    if (oracle.dist.size() != g.n || test_res.dist.size() != g.n || test_res.pred.size() != g.n) {
        std::cerr << "[-] FAILED " << test_name << ": size mismatch against graph n=" << g.n << std::endl;
        std::exit(1);
    }

    size_t mismatch_count = 0;
    double max_err = 0.0;

    for (size_t i = 0; i < oracle.dist.size(); ++i) {
        double d_oracle = oracle.dist[i];
        double d_test = test_res.dist[i];

        if (std::isinf(d_oracle)) {
            if (!std::isinf(d_test) || d_test < 0.0) {
                mismatch_count++;
            }
        } else {
            if (!std::isfinite(d_test)) {
                mismatch_count++;
            } else {
                double diff = std::abs(d_oracle - d_test);
                max_err = std::max(max_err, diff);
                if (diff > tol) {
                    mismatch_count++;
                }
            }
        }
    }

    if (mismatch_count > 0) {
        std::cerr << "[-] FAILED " << test_name << ": " << mismatch_count
                  << " distance mismatches! Max error: " << max_err << std::endl;
        std::exit(1);
    }

    verify_predecessor_tree(g, source, test_res, tol, test_name);
    std::cout << "    [PASS] " << test_name << " (Max diff: " << max_err << ", pred tree OK)" << std::endl;
}

CSRGraph generate_random_graph(size_t n, size_t m, unsigned int seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist_v(0, static_cast<uint32_t>(n - 1));
    std::uniform_real_distribution<double> dist_w(1.0, 50.0);

    std::vector<std::vector<std::pair<uint32_t, double>>> adj(n);
    for (size_t i = 0; i < m; ++i) {
        uint32_t u = dist_v(rng);
        uint32_t v = dist_v(rng);
        if (u != v) {
            adj[u].emplace_back(v, dist_w(rng));
        }
    }

    CSRGraph g;
    g.n = static_cast<uint32_t>(n);
    g.row_ptr.assign(n + 1, 0);

    for (size_t i = 0; i < n; ++i) {
        g.row_ptr[i + 1] = g.row_ptr[i] + adj[i].size();
    }

    g.m = g.row_ptr[n];
    g.col_idx.resize(g.m);
    g.weight.resize(g.m);

    size_t edge_idx = 0;
    for (size_t i = 0; i < n; ++i) {
        for (const auto& [v, w] : adj[i]) {
            g.col_idx[edge_idx] = v;
            g.weight[edge_idx] = w;
            edge_idx++;
        }
    }

    return g;
}

const char* frontier_name(FrontierType ft) {
    return (ft == FrontierType::Concurrent) ? "Concurrent" : "Mutex";
}

} // namespace bmssp

int main() {
    using namespace bmssp;

    std::cout << "==================================================================" << std::endl;
    std::cout << "  PARALLEL BMSSP VERIFICATION & GROUND TRUTH ORACLE TEST SUITE   " << std::endl;
    std::cout << "==================================================================" << std::endl;

    const FrontierType frontiers[] = {FrontierType::Concurrent, FrontierType::Mutex};

    // Test 1: Tiny toy graph with known shortest paths
    {
        std::cout << ">>> Test 1: Hand-crafted 6-vertex directed graph..." << std::endl;
        CSRGraph g;
        g.n = 6;
        g.row_ptr = {0, 2, 4, 6, 8, 9, 9};
        g.m = 9;
        g.col_idx = {1, 2, 2, 3, 3, 4, 4, 5, 5};
        g.weight  = {7.0, 9.0, 10.0, 15.0, 11.0, 2.0, 6.0, 9.0, 1.0};

        SSSPResult oracle = dijkstra_sequential(g, 0);
        verify_predecessor_tree(g, 0, oracle, 1e-7, "Oracle Toy Graph");

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{4}, size_t{1}}) {
                SSSPResult par_res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("Toy Graph 6-vertex [") + frontier_name(ft) +
                                    ", " + std::to_string(threads) + "T]";
                assert_results_match(g, 0, oracle, par_res, label);
            }
        }
    }

    // Test 2: Graph with zero-weight edges
    {
        std::cout << ">>> Test 2: Graph with 0-weight edges and near-ties..." << std::endl;
        CSRGraph g;
        g.n = 5;
        g.row_ptr = {0, 2, 4, 6, 7, 7};
        g.m = 7;
        g.col_idx = {1, 2, 2, 3, 3, 4, 4};
        g.weight  = {0.0, 5.0, 0.0, 2.0, 0.0, 1.0, 0.0};

        SSSPResult oracle = dijkstra_sequential(g, 0);
        verify_predecessor_tree(g, 0, oracle, 1e-7, "Oracle Zero-Weight Graph");

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{4}, size_t{1}}) {
                SSSPResult par_res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("Graph with Zero-Weight Edges [") +
                                    frontier_name(ft) + ", " + std::to_string(threads) + "T]";
                assert_results_match(g, 0, oracle, par_res, label);
            }
        }
    }

    // Test 3: Random graph n=200, m=1200
    {
        std::cout << ">>> Test 3: Random Sparse Graph (n=200, m=1200)..." << std::endl;
        CSRGraph g = generate_random_graph(200, 1200, 42);
        SSSPResult oracle = dijkstra_sequential(g, 0);
        verify_predecessor_tree(g, 0, oracle, 1e-7, "Oracle Random n=200");

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{4}, size_t{1}}) {
                SSSPResult par_res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("Random Graph (n=200, m=1200) [") +
                                    frontier_name(ft) + ", " + std::to_string(threads) + "T]";
                assert_results_match(g, 0, oracle, par_res, label);
            }
        }
    }

    // Test 4: Random graph n=2000, m=16000
    {
        std::cout << ">>> Test 4: Medium Scale Graph (n=2000, m=16000)..." << std::endl;
        CSRGraph g = generate_random_graph(2000, 16000, 1337);

        auto t0 = std::chrono::high_resolution_clock::now();
        SSSPResult oracle = dijkstra_sequential(g, 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        double dijkstra_time = std::chrono::duration<double, std::milli>(t1 - t0).count();
        verify_predecessor_tree(g, 0, oracle, 1e-7, "Oracle Medium n=2000");

        for (FrontierType ft : frontiers) {
            t0 = std::chrono::high_resolution_clock::now();
            SSSPResult par_res = bmssp_parallel(g, 0, 4, false, ft);
            t1 = std::chrono::high_resolution_clock::now();
            double par_time = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::cout << "    [TIMING] Dijkstra: " << dijkstra_time << " ms | Parallel BMSSP ("
                      << frontier_name(ft) << ", 4T): " << par_time << " ms" << std::endl;
            std::string label = std::string("Medium Graph (n=2000, m=16000) [") +
                                frontier_name(ft) + ", 4T]";
            assert_results_match(g, 0, oracle, par_res, label);
        }
    }

    // Test 5: Multi-thread scaling check (1, 2, 4, 8 threads)
    {
        std::cout << ">>> Test 5: Multi-threaded scaling consistency (n=1000, m=8000)..." << std::endl;
        CSRGraph g = generate_random_graph(1000, 8000, 2026);
        SSSPResult oracle = dijkstra_sequential(g, 0);
        verify_predecessor_tree(g, 0, oracle, 1e-7, "Oracle Scale n=1000");

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
                auto t0 = std::chrono::high_resolution_clock::now();
                SSSPResult par_res = bmssp_parallel(g, 0, threads, false, ft);
                auto t1 = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();

                std::string label = std::string("Scale Check [") + frontier_name(ft) + ", " +
                                    std::to_string(threads) + "T, " + std::to_string(elapsed) + " ms]";
                assert_results_match(g, 0, oracle, par_res, label);
            }
        }
    }

    std::cout << "==================================================================" << std::endl;
    std::cout << "  ALL PARALLEL BMSSP VERIFICATION TESTS PASSED SUCCESSFULLY!     " << std::endl;
    std::cout << "==================================================================" << std::endl;

    return 0;
}
