#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <chrono>
#include <iomanip>
#include <cmath>

namespace bmssp {

void assert_results_match(const SSSPResult& oracle, const SSSPResult& test_res, const std::string& test_name) {
    assert(oracle.dist.size() == test_res.dist.size());
    size_t mismatch_count = 0;
    double max_err = 0.0;

    for (size_t i = 0; i < oracle.dist.size(); ++i) {
        double d_oracle = oracle.dist[i];
        double d_test = test_res.dist[i];

        if (std::isinf(d_oracle)) {
            if (!std::isinf(d_test)) {
                mismatch_count++;
            }
        } else {
            if (std::isinf(d_test)) {
                mismatch_count++;
            } else {
                double diff = std::abs(d_oracle - d_test);
                max_err = std::max(max_err, diff);
                if (diff > 1e-7) {
                    mismatch_count++;
                }
            }
        }
    }

    if (mismatch_count > 0) {
        std::cerr << "[-] FAILED " << test_name << ": " << mismatch_count 
                  << " mismatches! Max error: " << max_err << std::endl;
        std::exit(1);
    } else {
        std::cout << "    [PASS] " << test_name << " (Max diff: " << max_err << ")" << std::endl;
    }
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
    g.n = n;
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

} // namespace bmssp

int main() {
    using namespace bmssp;

    std::cout << "==================================================================" << std::endl;
    std::cout << "  PARALLEL BMSSP VERIFICATION & GROUND TRUTH ORACLE TEST SUITE   " << std::endl;
    std::cout << "==================================================================" << std::endl;

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
        SSSPResult par_res = bmssp_parallel(g, 0, 4, false);
        assert_results_match(oracle, par_res, "Toy Graph 6-vertex Direct (4 threads)");

        SSSPResult par_1t = bmssp_parallel(g, 0, 1, false);
        assert_results_match(oracle, par_1t, "Toy Graph 6-vertex Direct (1 thread)");
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
        SSSPResult par_res = bmssp_parallel(g, 0, 4, false);
        assert_results_match(oracle, par_res, "Graph with Zero-Weight Edges");
    }

    // Test 3: Random graph n=200, m=1200
    {
        std::cout << ">>> Test 3: Random Sparse Graph (n=200, m=1200)..." << std::endl;
        CSRGraph g = generate_random_graph(200, 1200, 42);
        SSSPResult oracle = dijkstra_sequential(g, 0);

        SSSPResult par_res = bmssp_parallel(g, 0, 4, false);
        assert_results_match(oracle, par_res, "Random Graph (n=200, m=1200, 4 threads)");

        SSSPResult par_1t = bmssp_parallel(g, 0, 1, false);
        assert_results_match(oracle, par_1t, "Random Graph (n=200, m=1200, 1 thread)");
    }

    // Test 4: Random graph n=2000, m=16000
    {
        std::cout << ">>> Test 4: Medium Scale Graph (n=2000, m=16000)..." << std::endl;
        CSRGraph g = generate_random_graph(2000, 16000, 1337);

        auto t0 = std::chrono::high_resolution_clock::now();
        SSSPResult oracle = dijkstra_sequential(g, 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        double dijkstra_time = std::chrono::duration<double, std::milli>(t1 - t0).count();

        t0 = std::chrono::high_resolution_clock::now();
        SSSPResult par_res = bmssp_parallel(g, 0, 4, false);
        t1 = std::chrono::high_resolution_clock::now();
        double par_time = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "    [TIMING] Dijkstra: " << dijkstra_time << " ms | Parallel BMSSP (4T): " 
                  << par_time << " ms" << std::endl;
        assert_results_match(oracle, par_res, "Medium Graph (n=2000, m=16000, 4 threads)");
    }

    // Test 5: Multi-thread scaling check (1, 2, 4, 8 threads)
    {
        std::cout << ">>> Test 5: Multi-threaded scaling consistency (n=1000, m=8000)..." << std::endl;
        CSRGraph g = generate_random_graph(1000, 8000, 2026);
        SSSPResult oracle = dijkstra_sequential(g, 0);

        for (size_t threads : {1, 2, 4, 8}) {
            auto t0 = std::chrono::high_resolution_clock::now();
            SSSPResult par_res = bmssp_parallel(g, 0, threads, false);
            auto t1 = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::string label = "Scale Check (" + std::to_string(threads) + " threads, " + 
                                std::to_string(elapsed) + " ms)";
            assert_results_match(oracle, par_res, label);
        }
    }

    std::cout << "==================================================================" << std::endl;
    std::cout << "  ALL PARALLEL BMSSP VERIFICATION TESTS PASSED SUCCESSFULLY!     " << std::endl;
    std::cout << "==================================================================" << std::endl;

    return 0;
}
