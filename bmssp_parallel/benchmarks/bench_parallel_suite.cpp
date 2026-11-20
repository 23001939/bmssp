#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bmssp {
namespace {

struct GraphCase {
    std::string id;
    std::string name;
    CSRGraph graph;
    uint32_t source{0};
    int warmup{2};
    int reps{7};
};

CSRGraph load_txt_graph(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "[ERROR] Cannot open graph file: " << path << std::endl;
        std::exit(1);
    }
    uint32_t n = 0;
    uint64_t m = 0;
    in >> n >> m;
    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(m));
    for (uint64_t i = 0; i < m; ++i) {
        uint32_t u = 0, v = 0;
        double w = 0.0;
        in >> u >> v >> w;
        edges.push_back({u, v, w});
    }
    return CSRGraph::from_edge_list(n, edges);
}

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

bool verify_distances(const SSSPResult& oracle, const SSSPResult& res, double tol, double& max_diff) {
    max_diff = 0.0;
    if (oracle.dist.size() != res.dist.size()) return false;
    for (size_t i = 0; i < oracle.dist.size(); ++i) {
        double a = oracle.dist[i];
        double b = res.dist[i];
        if (std::isinf(a) || std::isinf(b)) {
            if (std::isinf(a) != std::isinf(b)) return false;
            continue;
        }
        double d = std::abs(a - b);
        if (d > max_diff) max_diff = d;
        if (d > tol) return false;
    }
    return true;
}

template <typename Fn>
double run_median_ms(Fn&& fn, int warmup, int reps, const SSSPResult& oracle, bool& ok, double& max_diff) {
    SSSPResult last_res;
    for (int i = 0; i < warmup; ++i) {
        last_res = fn();
    }
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(reps));
    for (int i = 0; i < reps; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        last_res = fn();
        auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    ok = verify_distances(oracle, last_res, 1e-9, max_diff);
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

struct RowResult {
    std::string name;
    uint32_t n{0};
    uint64_t m{0};
    uint32_t max_deg{0};
    double dijkstra_ms{0.0};
    double bmssp_seq_ms{0.0};
    double mutex_1t_ms{0.0};
    double mutex_4t_ms{0.0};
    double mutex_8t_ms{0.0};
    double conc_1t_ms{0.0};
    double conc_4t_ms{0.0};
    double conc_8t_ms{0.0};
    bool all_ok{true};
    double worst_diff{0.0};
};

} // namespace
} // namespace bmssp

int main() {
    using namespace bmssp;

    std::vector<GraphCase> cases;
    cases.push_back({"G01", "weighted_100 (project dataset)", load_txt_graph("../bmssp_visualizer/data/weighted_100.txt"), 0, 5, 21});
    cases.push_back({"G02", "Sparse Random 100", make_sparse_random(100, 500, 100), 0, 5, 21});
    cases.push_back({"G03", "weighted_1000 (project dataset)", load_txt_graph("../bmssp_visualizer/data/weighted_1000.txt"), 0, 5, 21});
    cases.push_back({"G04", "Sparse Random 10K", make_sparse_random(10000, 50000, 10000), 0, 2, 7});
    cases.push_back({"G05", "Sparse Random 20K", make_sparse_random(20000, 100000, 2026), 0, 2, 7});
    cases.push_back({"G06", "2D Grid 150x150", make_grid_2d(150, 150, 22500), 0, 2, 7});
    cases.push_back({"G07", "Hub 20K (deg0=10K)", make_hub_graph(20000, 10000, 80000, 20007), 0, 2, 7});
    cases.push_back({"G08", "Sparse Random 50K", make_sparse_random(50000, 250000, 50000), 0, 2, 7});
    cases.push_back({"G09", "2D Grid 300x300", make_grid_2d(300, 300, 90000), 0, 1, 5});
    cases.push_back({"G10", "Sparse Random 100K", make_sparse_random(100000, 500000, 100000), 0, 1, 5});
    cases.push_back({"G11", "Hub 100K (deg0=50K)", make_hub_graph(100000, 50000, 400000, 100011), 0, 1, 5});
    cases.push_back({"G12", "Sparse Random 200K", make_sparse_random(200000, 1000000, 200000), 0, 1, 5});

    std::vector<RowResult> rows;
    rows.reserve(cases.size());

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Running 12-graph comprehensive benchmark (HW threads = "
              << std::thread::hardware_concurrency() << ")...\n" << std::flush;

    for (const auto& tc : cases) {
        std::cout << "  -> [" << tc.id << "] " << tc.name
                  << " (n=" << tc.graph.n << ", m=" << tc.graph.m
                  << ", max_deg=" << tc.graph.max_out_degree() << ")..." << std::flush;

        SSSPResult oracle = dijkstra_sequential(tc.graph, tc.source);

        RowResult row;
        row.name = tc.name;
        row.n = tc.graph.n;
        row.m = tc.graph.m;
        row.max_deg = tc.graph.max_out_degree();

        bool ok = true;
        double diff = 0.0;
        auto record = [&](double val) {
            if (!ok) row.all_ok = false;
            if (diff > row.worst_diff) row.worst_diff = diff;
            return val;
        };

        row.dijkstra_ms = record(run_median_ms([&]() {
            return dijkstra_sequential(tc.graph, tc.source);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        row.bmssp_seq_ms = record(run_median_ms([&]() {
            return bmssp_sequential(tc.graph, tc.source);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        row.mutex_1t_ms = record(run_median_ms([&]() {
            return bmssp_parallel(tc.graph, tc.source, 1, false, FrontierType::Mutex);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        row.mutex_4t_ms = record(run_median_ms([&]() {
            return bmssp_parallel(tc.graph, tc.source, 4, false, FrontierType::Mutex);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        row.mutex_8t_ms = record(run_median_ms([&]() {
            return bmssp_parallel(tc.graph, tc.source, 8, false, FrontierType::Mutex);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        row.conc_1t_ms = record(run_median_ms([&]() {
            return bmssp_parallel(tc.graph, tc.source, 1, false, FrontierType::Concurrent);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        row.conc_4t_ms = record(run_median_ms([&]() {
            return bmssp_parallel(tc.graph, tc.source, 4, false, FrontierType::Concurrent);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        row.conc_8t_ms = record(run_median_ms([&]() {
            return bmssp_parallel(tc.graph, tc.source, 8, false, FrontierType::Concurrent);
        }, tc.warmup, tc.reps, oracle, ok, diff));

        std::cout << " " << (row.all_ok ? "OK" : "FAIL")
                  << " (Dijk=" << row.dijkstra_ms << "ms, Seq=" << row.bmssp_seq_ms
                  << "ms, C1T=" << row.conc_1t_ms << "ms, C8T=" << row.conc_8t_ms << "ms)\n"
                  << std::flush;

        rows.push_back(row);
    }

    std::cout << "\n=== MASTER TABLE (MEDIAN MS) ===\n";
    std::cout << "| # | Graph | n | m | Dijkstra (1T) | bmssp_seq (1T) | Mutex 1T | Mutex 4T | Mutex 8T | Conc 1T | Conc 4T | Conc 8T | Conc 1T/8T | M8T/C8T | Status |\n";
    std::cout << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        double self_scale = (r.conc_8t_ms > 0.0) ? (r.conc_1t_ms / r.conc_8t_ms) : 0.0;
        double ab_ratio = (r.conc_8t_ms > 0.0) ? (r.mutex_8t_ms / r.conc_8t_ms) : 0.0;
        std::cout << "| " << (i + 1)
                  << " | " << r.name
                  << " | " << r.n
                  << " | " << r.m
                  << " | " << r.dijkstra_ms
                  << " | " << r.bmssp_seq_ms
                  << " | " << r.mutex_1t_ms
                  << " | " << r.mutex_4t_ms
                  << " | " << r.mutex_8t_ms
                  << " | " << r.conc_1t_ms
                  << " | " << r.conc_4t_ms
                  << " | " << r.conc_8t_ms
                  << " | " << std::setprecision(2) << self_scale << "x"
                  << " | " << ab_ratio << "x" << std::setprecision(3)
                  << " | " << (r.all_ok ? "OK" : "FAIL") << " |\n";
    }

    for (const auto& r : rows) {
        if (!r.all_ok) return 1;
    }
    return 0;
}
