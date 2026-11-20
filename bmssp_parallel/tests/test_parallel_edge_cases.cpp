#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace bmssp {

namespace {

const char* frontier_label(FrontierType ft) {
    return (ft == FrontierType::Concurrent) ? "Concurrent" : "Mutex";
}

void fail_test(const std::string& name, const std::string& detail) {
    std::cerr << "[-] FAILED [" << name << "]: " << detail << std::endl;
    std::exit(1);
}

void verify_sssp_full_impl(
    const CSRGraph& g,
    uint32_t source,
    const SSSPResult& oracle,
    const SSSPResult& res,
    double tol,
    const std::string& name,
    bool verbose
) {
    if (oracle.dist.size() != g.n || res.dist.size() != g.n || res.pred.size() != g.n) {
        std::ostringstream oss;
        oss << "vector size mismatch (g.n=" << g.n
            << ", oracle.dist=" << oracle.dist.size()
            << ", res.dist=" << res.dist.size()
            << ", res.pred=" << res.pred.size() << ")";
        fail_test(name, oss.str());
    }

    if (g.n == 0) {
        if (verbose) {
            std::cout << "    [PASS] " << name << " (empty graph)" << std::endl;
        }
        return;
    }

    double max_diff = 0.0;

    // (a) Check distances for every vertex (including INF_DIST unreachable)
    for (uint32_t v = 0; v < g.n; ++v) {
        double d_exp = oracle.dist[v];
        double d_got = res.dist[v];

        if (std::isinf(d_exp)) {
            if (!std::isinf(d_got) || d_got < 0.0) {
                std::ostringstream oss;
                oss << "vertex " << v << " expected INF_DIST, got " << d_got;
                fail_test(name, oss.str());
            }
        } else {
            if (!std::isfinite(d_got)) {
                std::ostringstream oss;
                oss << "vertex " << v << " expected finite dist " << d_exp << ", got " << d_got;
                fail_test(name, oss.str());
            }
            double diff = std::abs(d_exp - d_got);
            max_diff = std::max(max_diff, diff);
            if (diff > tol) {
                std::ostringstream oss;
                oss << "distance mismatch at vertex " << v
                    << " (expected=" << d_exp << ", got=" << d_got
                    << ", diff=" << diff << ", tol=" << tol << ")";
                fail_test(name, oss.str());
            }
        }
    }

    if (source >= g.n) {
        for (uint32_t v = 0; v < g.n; ++v) {
            if (res.pred[v] != NO_PRED) {
                std::ostringstream oss;
                oss << "out-of-range source expected NO_PRED at vertex " << v
                    << ", got " << res.pred[v];
                fail_test(name, oss.str());
            }
        }
        if (verbose) {
            std::cout << "    [PASS] " << name << " (MaxDiff: " << max_diff << ")" << std::endl;
        }
        return;
    }

    // (b) Check predecessor invariants & edge consistency in O(n + m)
    if (res.pred[source] != NO_PRED) {
        std::ostringstream oss;
        oss << "pred[source=" << source << "] expected NO_PRED, got " << res.pred[source];
        fail_test(name, oss.str());
    }

    for (uint32_t v = 0; v < g.n; ++v) {
        if (res.pred[v] == v) {
            std::ostringstream oss;
            oss << "self-loop predecessor pred[" << v << "] == " << v;
            fail_test(name, oss.str());
        }
        if (std::isinf(res.dist[v])) {
            if (res.pred[v] != NO_PRED) {
                std::ostringstream oss;
                oss << "unreachable vertex " << v << " has pred=" << res.pred[v] << " (expected NO_PRED)";
                fail_test(name, oss.str());
            }
        } else if (v != source) {
            uint32_t u = res.pred[v];
            if (u == NO_PRED || u >= g.n) {
                std::ostringstream oss;
                oss << "reachable vertex " << v << " has invalid pred=" << u;
                fail_test(name, oss.str());
            }
            if (!std::isfinite(res.dist[u])) {
                std::ostringstream oss;
                oss << "reachable vertex " << v << " has unreachable pred=" << u;
                fail_test(name, oss.str());
            }
        }
    }

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
                std::ostringstream oss;
                oss << "no valid edge (u=" << res.pred[v] << " -> v=" << v
                    << ") matching dists (matched=" << static_cast<int>(edge_matched[v])
                    << ", best_err=" << best_err[v] << ", tol=" << tol << ")";
                fail_test(name, oss.str());
            }
        }
    }

    // Trace every reachable vertex back to source in <= n steps without cycles (O(n) total)
    std::vector<uint8_t> color(g.n, 0); // 0=unvisited, 1=visiting, 2=verified
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
        uint32_t steps = 0;
        while (color[cur] != 2) {
            if (color[cur] == 1) {
                std::ostringstream oss;
                oss << "cycle detected in pred chain starting from " << v << " at vertex " << cur;
                fail_test(name, oss.str());
            }
            if (steps >= g.n) {
                std::ostringstream oss;
                oss << "pred chain from " << v << " exceeded n=" << g.n << " steps";
                fail_test(name, oss.str());
            }
            color[cur] = 1;
            chain.push_back(cur);
            cur = res.pred[cur];
            ++steps;
            if (cur >= g.n || !std::isfinite(res.dist[cur])) {
                std::ostringstream oss;
                oss << "broken pred chain from " << v << " at pred=" << cur;
                fail_test(name, oss.str());
            }
        }

        for (size_t idx = chain.size(); idx-- > 0;) {
            uint32_t x = chain[idx];
            uint32_t p = res.pred[x];
            depth[x] = depth[p] + 1;
            if (depth[x] >= g.n) {
                std::ostringstream oss;
                oss << "path depth >= n at vertex " << x;
                fail_test(name, oss.str());
            }
            path_dist[x] = path_dist[p] + matched_w[x];
            double path_tol = tol * static_cast<double>(depth[x]);
            if (std::abs(path_dist[x] - res.dist[x]) > path_tol) {
                std::ostringstream oss;
                oss << "path weight sum mismatch at vertex " << x
                    << " (sum=" << path_dist[x] << ", dist=" << res.dist[x]
                    << ", tol=" << path_tol << ")";
                fail_test(name, oss.str());
            }
            color[x] = 2;
        }
    }

    if (verbose) {
        std::cout << "    [PASS] " << name << " (MaxDiff: " << max_diff << ")" << std::endl;
    }
}

CSRGraph make_random_graph(
    uint32_t n,
    uint64_t m,
    uint64_t seed,
    double zero_prob,
    double min_w,
    double max_w
) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist_v(0, n - 1);
    std::uniform_real_distribution<double> dist_u01(0.0, 1.0);
    std::uniform_real_distribution<double> dist_w(min_w, max_w);

    std::vector<Edge> edges;
    edges.reserve(m);
    for (uint64_t i = 0; i < m; ++i) {
        uint32_t u = dist_v(rng);
        uint32_t v = dist_v(rng);
        if (u == v) {
            v = (u + 1) % n;
        }
        double w = (dist_u01(rng) < zero_prob) ? 0.0 : dist_w(rng);
        edges.push_back({u, v, w});
    }
    return CSRGraph::from_edge_list(n, edges);
}

} // namespace

void verify_sssp_full(
    const CSRGraph& g,
    uint32_t source,
    const SSSPResult& oracle,
    const SSSPResult& res,
    double tol,
    const std::string& name
) {
    verify_sssp_full_impl(g, source, oracle, res, tol, name, true);
}

// ============================================================================
// Suite 1: Zero-weight cycles & all-zero graphs
// ============================================================================
void run_suite_1() {
    std::cout << "\n>>> Suite 1: Zero-weight cycles & all-zero graphs..." << std::endl;
    const FrontierType frontiers[] = {FrontierType::Concurrent, FrontierType::Mutex};

    // 1a. 2-vertex zero-weight cycle: 0 <-> 1 (w = 0)
    {
        CSRGraph g = CSRGraph::from_edge_list(2, {
            {0, 1, 0.0},
            {1, 0, 0.0}
        });
        SSSPResult oracle = dijkstra_sequential(g, 0);
        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("S1a 2-vertex 0-cycle [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 0.0, label);
            }
        }
    }

    // 1b. 5-vertex zero-weight cycle 0->1->2->3->4->0 (w=0) connected to vertex 5 (w=3.0)
    {
        CSRGraph g = CSRGraph::from_edge_list(6, {
            {0, 1, 0.0},
            {1, 2, 0.0},
            {2, 3, 0.0},
            {3, 4, 0.0},
            {4, 0, 0.0},
            {4, 5, 3.0}
        });
        SSSPResult oracle = dijkstra_sequential(g, 0);
        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("S1b 5-vertex 0-cycle + exit [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 0.0, label);
                if (res.dist[5] != 3.0) {
                    fail_test(label, "expected dist[5] == 3.0");
                }
            }
        }
    }

    // 1c. Zero-weight self-loops: 0->0 (0), 0->1 (2), 1->1 (0)
    {
        CSRGraph g = CSRGraph::from_edge_list(2, {
            {0, 0, 0.0},
            {0, 1, 2.0},
            {1, 1, 0.0}
        });
        SSSPResult oracle = dijkstra_sequential(g, 0);
        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("S1c Zero self-loops [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 0.0, label);
            }
        }
    }

    // 1d. All-zero graph (n=50, m=200)
    {
        std::mt19937_64 rng(1001);
        std::uniform_int_distribution<uint32_t> dist_v(0, 49);
        std::vector<Edge> edges;
        edges.reserve(200);
        // Ensure a rich spanning structure plus random zero-weight edges
        for (uint32_t i = 0; i < 49; ++i) {
            edges.push_back({i, i + 1, 0.0});
        }
        edges.push_back({49, 0, 0.0});
        while (edges.size() < 200) {
            edges.push_back({dist_v(rng), dist_v(rng), 0.0});
        }
        CSRGraph g = CSRGraph::from_edge_list(50, edges);
        SSSPResult oracle = dijkstra_sequential(g, 0);

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}, size_t{8}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("S1d All-zero graph n=50, m=200 [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 0.0, label);
            }
        }
    }
}

// ============================================================================
// Suite 2: Exact ties & Near-ties
// ============================================================================
void run_suite_2() {
    std::cout << "\n>>> Suite 2: Exact ties & Near-ties..." << std::endl;
    const FrontierType frontiers[] = {FrontierType::Concurrent, FrontierType::Mutex};

    // 2a. Famous near-tie: 0->1 (4e-11), 0->2 (0.0), 2->1 (0.0)
    {
        CSRGraph g = CSRGraph::from_edge_list(3, {
            {0, 1, 4e-11},
            {0, 2, 0.0},
            {2, 1, 0.0}
        });
        SSSPResult oracle = dijkstra_sequential(g, 0);
        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("S2a Near-tie 4e-11 vs 0+0 [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 0.0, label);
                if (res.dist[0] != 0.0 || res.dist[1] != 0.0 || res.dist[2] != 0.0) {
                    fail_test(label, "expected exact dist = [0, 0, 0]");
                }
                if (res.pred[1] != 2 || res.pred[2] != 0) {
                    std::ostringstream oss;
                    oss << "expected pred[1]==2 and pred[2]==0, got pred[1]="
                        << res.pred[1] << ", pred[2]=" << res.pred[2];
                    fail_test(label, oss.str());
                }
            }
        }
    }

    // 2b. Multi-layer integer-weight diamond graph with massive equal-distance ties
    {
        constexpr uint32_t layers = 12;
        constexpr uint32_t width = 4;
        const uint32_t sink = 1 + layers * width;
        const uint32_t n = sink + 1;

        std::vector<Edge> edges;
        for (uint32_t j = 0; j < width; ++j) {
            edges.push_back({0, 1 + j, 3.0});
        }
        for (uint32_t L = 0; L + 1 < layers; ++L) {
            double layer_w = (L % 3 == 0) ? 0.0 : static_cast<double>((L % 4) + 2);
            for (uint32_t a = 0; a < width; ++a) {
                uint32_t u = 1 + L * width + a;
                for (uint32_t b = 0; b < width; ++b) {
                    uint32_t v = 1 + (L + 1) * width + b;
                    edges.push_back({u, v, layer_w});
                }
            }
        }
        for (uint32_t a = 0; a < width; ++a) {
            uint32_t u = 1 + (layers - 1) * width + a;
            edges.push_back({u, sink, 5.0});
        }
        // Equal-cost bypass from layer 0 to layer 3 (layers 0->1: 0, 1->2: 3, 2->3: 4 => sum=7)
        for (uint32_t a = 0; a < width; ++a) {
            edges.push_back({1 + a, 1 + 3 * width + a, 7.0});
        }

        CSRGraph g = CSRGraph::from_edge_list(n, edges);
        SSSPResult oracle = dijkstra_sequential(g, 0);

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("S2b Multi-layer integer diamond [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 0.0, label);
            }
        }
    }
}

// ============================================================================
// Suite 3: Disconnected, Isolated, Edge sizes & Non-zero Source
// ============================================================================
void run_suite_3() {
    std::cout << "\n>>> Suite 3: Disconnected, Isolated, Edge sizes & Non-zero Source..." << std::endl;
    const FrontierType frontiers[] = {FrontierType::Concurrent, FrontierType::Mutex};

    // 3a. Single-vertex graphs (n=1, source=0) with m=0 and m=1 self-loop
    {
        CSRGraph g_empty = CSRGraph::from_edge_list(1, {});
        SSSPResult oracle_empty = dijkstra_sequential(g_empty, 0);
        CSRGraph g_loop = CSRGraph::from_edge_list(1, {{0, 0, 0.0}});
        SSSPResult oracle_loop = dijkstra_sequential(g_loop, 0);

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                verify_sssp_full(g_empty, 0, oracle_empty,
                                 bmssp_parallel(g_empty, 0, threads, false, ft), 0.0,
                                 std::string("S3a n=1 no edges [") + frontier_label(ft) + ", " + std::to_string(threads) + "T]");
                verify_sssp_full(g_loop, 0, oracle_loop,
                                 bmssp_parallel(g_loop, 0, threads, false, ft), 0.0,
                                 std::string("S3a n=1 self-loop [") + frontier_label(ft) + ", " + std::to_string(threads) + "T]");
            }
        }
    }

    // 3b. 100 vertices with 30 isolated vertices, tested with source=17 and source=99
    {
        std::mt19937_64 rng(3001);
        std::uniform_int_distribution<uint32_t> dist_active(0, 69);
        std::uniform_real_distribution<double> dist_w(1.0, 25.0);

        // Component [0..69] connected, vertices [70..99] are 30 isolated vertices
        std::vector<Edge> edges1;
        for (uint32_t i = 0; i < 69; ++i) {
            edges1.push_back({i, i + 1, dist_w(rng)});
        }
        edges1.push_back({69, 0, dist_w(rng)});
        for (int k = 0; k < 300; ++k) {
            uint32_t u = dist_active(rng);
            uint32_t v = dist_active(rng);
            if (u != v) {
                edges1.push_back({u, v, (k % 10 == 0) ? 0.0 : dist_w(rng)});
            }
        }
        CSRGraph g1 = CSRGraph::from_edge_list(100, edges1);

        // Component [30..99] connected, vertices [0..29] are 30 isolated vertices (so source=99 is non-trivial too)
        std::vector<Edge> edges2;
        for (uint32_t i = 30; i < 99; ++i) {
            edges2.push_back({i, i + 1, dist_w(rng)});
        }
        edges2.push_back({99, 30, dist_w(rng)});
        for (int k = 0; k < 300; ++k) {
            uint32_t u = 30 + dist_active(rng);
            uint32_t v = 30 + dist_active(rng);
            if (u != v) {
                edges2.push_back({u, v, (k % 10 == 0) ? 0.0 : dist_w(rng)});
            }
        }
        CSRGraph g2 = CSRGraph::from_edge_list(100, edges2);

        SSSPResult oracle_17 = dijkstra_sequential(g1, 17);
        SSSPResult oracle_99_iso = dijkstra_sequential(g1, 99);
        SSSPResult oracle_99_comp = dijkstra_sequential(g2, 99);

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                verify_sssp_full(g1, 17, oracle_17,
                                 bmssp_parallel(g1, 17, threads, false, ft), 1e-9,
                                 std::string("S3b n=100 (30 isolated) src=17 [") + frontier_label(ft) + ", " + std::to_string(threads) + "T]");
                verify_sssp_full(g1, 99, oracle_99_iso,
                                 bmssp_parallel(g1, 99, threads, false, ft), 1e-9,
                                 std::string("S3b n=100 (isolated src=99) [") + frontier_label(ft) + ", " + std::to_string(threads) + "T]");
                verify_sssp_full(g2, 99, oracle_99_comp,
                                 bmssp_parallel(g2, 99, threads, false, ft), 1e-9,
                                 std::string("S3b n=100 (30 isolated) connected src=99 [") + frontier_label(ft) + ", " + std::to_string(threads) + "T]");
            }
        }
    }
}

// ============================================================================
// Suite 4: High-degree Hub / Power-law Skew
// ============================================================================
void run_suite_4() {
    std::cout << "\n>>> Suite 4: High-degree Hub / Power-law Skew (out-degree 20,000)..." << std::endl;
    const FrontierType frontiers[] = {FrontierType::Concurrent, FrontierType::Mutex};

    constexpr uint32_t fanout = 20000;
    constexpr uint32_t target = 20001;
    constexpr uint32_t n = 20002;

    std::vector<Edge> edges;
    edges.reserve(fanout * 2);
    for (uint32_t v = 1; v <= fanout; ++v) {
        double w1 = static_cast<double>(1 + ((v * 17U) % 50U));
        double w2 = static_cast<double>(1 + ((v * 31U) % 25U));
        edges.push_back({0, v, w1});
        edges.push_back({v, target, w2});
    }

    CSRGraph g = CSRGraph::from_edge_list(n, edges);
    SSSPResult oracle = dijkstra_sequential(g, 0);

    for (FrontierType ft : frontiers) {
        for (size_t threads : {size_t{1}, size_t{4}, size_t{8}}) {
            SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
            std::string label = std::string("S4 Hub 20000-fanout -> 20001 [") +
                                frontier_label(ft) + ", " + std::to_string(threads) + "T]";
            verify_sssp_full(g, 0, oracle, res, 0.0, label);
        }
    }
}

// ============================================================================
// Suite 5: Gadget Constant-Degree Transformation (use_gadget_transform = true)
// ============================================================================
void run_suite_5() {
    std::cout << "\n>>> Suite 5: Gadget Constant-Degree Transformation (use_gadget_transform=true)..." << std::endl;
    const FrontierType frontiers[] = {FrontierType::Concurrent, FrontierType::Mutex};

    // 5a. High-degree graph (60 vertices, out-degree >= 50)
    {
        constexpr uint32_t n = 60;
        std::mt19937_64 rng(5001);
        std::uniform_real_distribution<double> dist_w(1.0, 20.0);
        std::vector<Edge> edges;

        // Vertices 0..4 each have out-degree >= 55
        for (uint32_t u = 0; u < 5; ++u) {
            for (uint32_t v = 0; v < n; ++v) {
                if (u != v && v != 59) {
                    edges.push_back({u, v, dist_w(rng)});
                }
            }
            edges.push_back({u, 59, dist_w(rng)});
        }
        // Additional cross and return edges to exercise high in-degree + out-degree gadgets
        for (uint32_t u = 5; u < n; ++u) {
            edges.push_back({u, (u + 1) % n, dist_w(rng)});
            edges.push_back({u, u % 5, dist_w(rng)});
        }

        CSRGraph g = CSRGraph::from_edge_list(n, edges);
        if (g.max_out_degree() < 50) {
            fail_test("S5a Setup", "expected max_out_degree >= 50");
        }
        SSSPResult oracle = dijkstra_sequential(g, 0);

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, true, ft);
                std::string label = std::string("S5a Gadget High-Degree n=60 [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 1e-9, label);
            }
        }
    }

    // 5b. Random graph n=300, m=2000
    {
        CSRGraph g = make_random_graph(300, 2000, 5002, 0.0, 0.5, 50.0);
        SSSPResult oracle = dijkstra_sequential(g, 0);

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, true, ft);
                std::string label = std::string("S5b Gadget Random n=300, m=2000 [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 1e-9, label);
            }
        }
    }

    // 5c. Graph with zero-weight cycles + high-degree vertices
    {
        std::vector<Edge> edges;
        // 0-weight cycle 0->1->2->3->4->0
        for (uint32_t i = 0; i < 5; ++i) {
            edges.push_back({i, (i + 1) % 5, 0.0});
        }
        // High-degree node 2 fans out to 5..19 with mix of 0 and positive weights
        for (uint32_t v = 5; v < 20; ++v) {
            edges.push_back({2, v, (v % 3 == 0) ? 0.0 : static_cast<double>(v - 4)});
        }
        // Second 0-weight cycle 10->11->12->10 and exits to 20..23
        edges.push_back({10, 11, 0.0});
        edges.push_back({11, 12, 0.0});
        edges.push_back({12, 10, 0.0});
        edges.push_back({12, 20, 4.0});
        edges.push_back({20, 21, 0.0});
        edges.push_back({21, 22, 0.0});
        edges.push_back({22, 20, 0.0});
        edges.push_back({22, 23, 7.0});

        CSRGraph g = CSRGraph::from_edge_list(24, edges);
        SSSPResult oracle = dijkstra_sequential(g, 0);

        for (FrontierType ft : frontiers) {
            for (size_t threads : {size_t{1}, size_t{4}}) {
                SSSPResult res = bmssp_parallel(g, 0, threads, true, ft);
                std::string label = std::string("S5c Gadget 0-Weight Cycles [") +
                                    frontier_label(ft) + ", " + std::to_string(threads) + "T]";
                verify_sssp_full(g, 0, oracle, res, 0.0, label);
            }
        }
    }
}

// ============================================================================
// Suite 6: Multi-threaded Determinism & Concurrency Stress (240 runs)
// ============================================================================
void run_suite_6() {
    std::cout << "\n>>> Suite 6: Concurrency Stress (n=1000, m=8000, 30 reps x 4T x 2F = 240 runs)..."
              << std::endl;

    CSRGraph g = make_random_graph(1000, 8000, 6001, 0.15, 0.1, 100.0);
    SSSPResult oracle = dijkstra_sequential(g, 0);

    const FrontierType frontiers[] = {FrontierType::Concurrent, FrontierType::Mutex};
    const size_t thread_counts[] = {1, 2, 4, 8};
    constexpr int kReps = 30;
    size_t total_runs = 0;

    for (FrontierType ft : frontiers) {
        for (size_t threads : thread_counts) {
            for (int rep = 0; rep < kReps; ++rep) {
                SSSPResult res = bmssp_parallel(g, 0, threads, false, ft);
                std::string label = std::string("S6 Stress [") + frontier_label(ft) +
                                    ", " + std::to_string(threads) + "T, rep=" +
                                    std::to_string(rep + 1) + "/30]";
                verify_sssp_full_impl(g, 0, oracle, res, 1e-9, label, false);
                ++total_runs;
            }
            std::cout << "    [PASS] S6 [" << frontier_label(ft) << ", " << threads
                      << "T]: 30/30 consecutive runs matched Oracle (dist + pred)" << std::endl;
        }
    }

    std::cout << "    [SUMMARY] Total stress runs verified: " << total_runs << "/240" << std::endl;
}

} // namespace bmssp

int main() {
    using namespace bmssp;

    std::cout << "==================================================================" << std::endl;
    std::cout << "  PARALLEL BMSSP COMPREHENSIVE EDGE-CASE & STRESS TEST SUITE      " << std::endl;
    std::cout << "==================================================================" << std::endl;

    run_suite_1();
    run_suite_2();
    run_suite_3();
    run_suite_4();
    run_suite_5();
    run_suite_6();

    std::cout << "\n==================================================================" << std::endl;
    std::cout << "  ALL 6 EDGE-CASE & CONCURRENCY STRESS SUITES PASSED 100%!        " << std::endl;
    std::cout << "==================================================================" << std::endl;

    return 0;
}
