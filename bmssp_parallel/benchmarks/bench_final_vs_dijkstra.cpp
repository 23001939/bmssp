#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bmssp {
namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

uint64_t graph_fingerprint(const CSRGraph& graph) {
    uint64_t fp = 14695981039346656037ULL;
    for (uint32_t u = 0; u < graph.n; ++u) {
        for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
            fp = (fp ^ u) * 1099511628211ULL;
            fp = (fp ^ graph.edge_dest(e)) * 1099511628211ULL;
            fp = (fp ^ std::bit_cast<uint64_t>(graph.edge_weight(e))) * 1099511628211ULL;
        }
    }
    return fp;
}

CSRGraph erdos_renyi(uint32_t n, uint32_t degree, uint64_t seed) {
    std::mt19937_64 rng(seed);
    const uint64_t stride = n - 1;
    const uint64_t possible = static_cast<uint64_t>(n) * stride;
    std::optional<std::geometric_distribution<uint64_t>> gap;
    if (degree < stride) {
        gap.emplace(static_cast<double>(degree) / static_cast<double>(stride));
    }
    std::uniform_int_distribution<int> weight(1, 100);
    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * degree);
    for (uint64_t i = 0; i < possible;) {
        const uint64_t skipped = gap ? (*gap)(rng) : 0;
        if (skipped >= possible - i) break;
        i += skipped;
        const auto u = static_cast<uint32_t>(i / stride);
        auto v = static_cast<uint32_t>(i % stride);
        v += (v >= u) ? 1U : 0U;
        edges.push_back({u, v, static_cast<double>(weight(rng))});
        ++i;
    }
    return CSRGraph::from_edge_list(n, edges);
}

CSRGraph scale_free(uint32_t n, uint32_t degree, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> weight(1, 100);
    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * degree);
    std::vector<uint32_t> urn{0};
    urn.reserve(static_cast<size_t>(n) * (static_cast<size_t>(degree) + 1));
    std::vector<uint32_t> parents;
    parents.reserve(degree);
    for (uint32_t v = 1; v < n; ++v) {
        parents.clear();
        std::uniform_int_distribution<size_t> pick(0, urn.size() - 1);
        while (parents.size() < std::min(v, degree)) {
            const uint32_t parent = urn[pick(rng)];
            if (std::find(parents.begin(), parents.end(), parent) == parents.end()) {
                parents.push_back(parent);
                edges.push_back({parent, v, static_cast<double>(weight(rng))});
            }
        }
        urn.insert(urn.end(), parents.begin(), parents.end());
        urn.push_back(v);
    }
    return CSRGraph::from_edge_list(n, edges);
}

bool verify_against_oracle(const SSSPResult& oracle, const SSSPResult& res, double tol, double& max_diff) {
    max_diff = 0.0;
    if (oracle.dist.size() != res.dist.size()) return false;
    for (size_t i = 0; i < oracle.dist.size(); ++i) {
        const double a = oracle.dist[i];
        const double b = res.dist[i];
        if (std::isinf(a) || std::isinf(b)) {
            if (std::isinf(a) != std::isinf(b)) return false;
            continue;
        }
        const double d = std::abs(a - b);
        if (d > max_diff) max_diff = d;
        if (d > tol) return false;
    }
    return true;
}

struct StatSummary {
    double median_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double max_diff{0.0};
    bool ok{true};
};

template <typename Fn>
StatSummary measure_solver(Fn&& fn, int warmups, int samples, const SSSPResult& oracle) {
    StatSummary out;
    for (int i = 0; i < warmups; ++i) {
        SSSPResult r = fn();
        double diff = 0.0;
        if (!verify_against_oracle(oracle, r, 1e-9, diff)) {
            out.ok = false;
            return out;
        }
    }
    std::vector<double> times;
    times.reserve(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const auto t0 = Clock::now();
        SSSPResult r = fn();
        const double ms = elapsed_ms(t0);
        times.push_back(ms);
        double diff = 0.0;
        if (!verify_against_oracle(oracle, r, 1e-9, diff)) {
            out.ok = false;
        }
        if (diff > out.max_diff) out.max_diff = diff;
    }
    std::sort(times.begin(), times.end());
    out.min_ms = times.front();
    out.median_ms = times[times.size() / 2];
    out.max_ms = times.back();
    return out;
}

struct GraphSpec {
    std::string id;
    std::string label;
    std::string family;
    uint32_t n;
    uint32_t deg;
    uint64_t seed;
    int warmups;
    int samples;
};

struct BenchmarkRow {
    GraphSpec spec;
    uint64_t m{0};
    uint32_t max_out_deg{0};
    size_t reachable{0};
    uint64_t hash{0};
    StatSummary dijkstra_1t;
    StatSummary bmssp_1t;
    StatSummary bmssp_2t;
    StatSummary bmssp_4t;
    StatSummary bmssp_8t;
    StatSummary bmssp_12t;
};

} // namespace
} // namespace bmssp

int main(int argc, char** argv) {
    using namespace bmssp;

    std::string csv_path = "bmssp_parallel/final_benchmark_1m_2m.csv";
    if (argc >= 2) {
        csv_path = argv[1];
    }

    omp_set_dynamic(0);

    std::vector<GraphSpec> specs = {
        {"ER_100K",  "Erdos-Renyi 100K (deg=4)",    "er",         100000,  4,  20261122ULL, 2, 5},
        {"ER_200K",  "Erdos-Renyi 200K (deg=4)",    "er",         200000,  4,  20261122ULL, 2, 5},
        {"D20_200K", "Dense-20 Random 200K (deg=20)","er",        200000,  20, 20261122ULL, 1, 5},
        {"ER_500K",  "Erdos-Renyi 500K (deg=4)",    "er",         500000,  4,  20261122ULL, 1, 5},
        {"D16_500K", "Dense-16 Random 500K (deg=16)","er",        500000,  16, 20261122ULL, 1, 5},
        {"ER_1M",    "Erdos-Renyi 1M (deg=4)",      "er",         1000000, 4,  20261122ULL, 1, 5},
        {"SF_1M",    "Scale-Free 1M (deg=4)",       "scale-free", 1000000, 4,  20261122ULL, 1, 5},
        {"D12_1M",   "Dense-12 Random 1M (deg=12)", "er",         1000000, 12, 20261122ULL, 1, 5},
        {"ER_2M",    "Erdos-Renyi 2M (deg=4)",      "er",         2000000, 4,  20261122ULL, 1, 5},
        {"SF_2M",    "Scale-Free 2M (deg=4)",       "scale-free", 2000000, 4,  20261122ULL, 1, 5},
        {"D12_2M",   "Dense-12 Random 2M (deg=12)", "er",         2000000, 12, 20261122ULL, 1, 3},
    };

    std::vector<BenchmarkRow> rows;
    rows.reserve(specs.size());

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "================================================================================\n";
    std::cout << "  FINAL BENCHMARK: OPTIMIZED PARALLEL BMSSP vs DIJKSTRA (UP TO 2M VERTICES, 12T)\n";
    std::cout << "  Hardware Concurrency = " << std::thread::hardware_concurrency() << " logical threads\n";
    std::cout << "================================================================================\n" << std::flush;

    for (const auto& sp : specs) {
        std::cout << "\n[*] Generating " << sp.label << " (seed=" << sp.seed << ")..." << std::flush;
        CSRGraph g = (sp.family == "er")
                         ? erdos_renyi(sp.n, sp.deg, sp.seed)
                         : scale_free(sp.n, sp.deg, sp.seed);

        BenchmarkRow row;
        row.spec = sp;
        row.m = g.m;
        row.max_out_deg = g.max_out_degree();
        row.hash = graph_fingerprint(g);

        SSSPResult oracle = dijkstra_sequential(g, 0);
        row.reachable = static_cast<size_t>(
            std::count_if(oracle.dist.begin(), oracle.dist.end(), [](double d) { return std::isfinite(d); })
        );

        std::cout << " done (m=" << row.m << ", max_deg=" << row.max_out_deg
                  << ", reachable=" << row.reachable << ")\n" << std::flush;

        row.dijkstra_1t = measure_solver([&] { return dijkstra_sequential(g, 0); }, sp.warmups, sp.samples, oracle);
        std::cout << "    Dijkstra (1T) : " << std::setw(8) << row.dijkstra_1t.median_ms << " ms\n" << std::flush;

        row.bmssp_1t = measure_solver([&] {
            return bmssp_parallel(g, 0, 1, false, FrontierType::Concurrent);
        }, sp.warmups, sp.samples, oracle);
        std::cout << "    BMSSP Par  1T : " << std::setw(8) << row.bmssp_1t.median_ms << " ms\n" << std::flush;

        row.bmssp_2t = measure_solver([&] {
            return bmssp_parallel(g, 0, 2, false, FrontierType::Concurrent);
        }, sp.warmups, sp.samples, oracle);
        std::cout << "    BMSSP Par  2T : " << std::setw(8) << row.bmssp_2t.median_ms << " ms\n" << std::flush;

        row.bmssp_4t = measure_solver([&] {
            return bmssp_parallel(g, 0, 4, false, FrontierType::Concurrent);
        }, sp.warmups, sp.samples, oracle);
        std::cout << "    BMSSP Par  4T : " << std::setw(8) << row.bmssp_4t.median_ms << " ms\n" << std::flush;

        row.bmssp_8t = measure_solver([&] {
            return bmssp_parallel(g, 0, 8, false, FrontierType::Concurrent);
        }, sp.warmups, sp.samples, oracle);
        std::cout << "    BMSSP Par  8T : " << std::setw(8) << row.bmssp_8t.median_ms << " ms\n" << std::flush;

        row.bmssp_12t = measure_solver([&] {
            return bmssp_parallel(g, 0, 12, false, FrontierType::Concurrent);
        }, sp.warmups, sp.samples, oracle);
        std::cout << "    BMSSP Par 12T : " << std::setw(8) << row.bmssp_12t.median_ms << " ms\n" << std::flush;

        const double best_par = std::min({row.bmssp_4t.median_ms, row.bmssp_8t.median_ms, row.bmssp_12t.median_ms});
        const double speedup_vs_dijk = row.dijkstra_1t.median_ms / best_par;
        const double self_speedup = row.bmssp_1t.median_ms / best_par;
        std::cout << "    => Best Parallel BMSSP = " << best_par << " ms | Speedup vs Dijkstra = "
                  << speedup_vs_dijk << "x | Self-Scaling = " << self_speedup << "x\n" << std::flush;

        rows.push_back(row);
    }

    std::ofstream csv(csv_path);
    csv << "id,label,family,n,m,max_out_deg,reachable,hash,dijkstra_1t_ms,bmssp_1t_ms,bmssp_2t_ms,"
           "bmssp_4t_ms,bmssp_8t_ms,bmssp_12t_ms,best_par_ms,speedup_vs_dijkstra,self_speedup_1t_best,max_diff,all_ok\n";
    csv << std::fixed << std::setprecision(4);

    std::cout << "\n=== FINAL SUMMARY TABLE (MEDIAN MS) ===\n";
    std::cout << "| ID | Graph | n | m | Dijkstra 1T | BMSSP 1T | BMSSP 2T | BMSSP 4T | BMSSP 8T | BMSSP 12T | Best Par | Faster vs Dijk | Self Scale | Status |\n";
    std::cout << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|\n";

    bool global_ok = true;
    for (const auto& r : rows) {
        const bool ok = r.dijkstra_1t.ok && r.bmssp_1t.ok && r.bmssp_2t.ok &&
                        r.bmssp_4t.ok && r.bmssp_8t.ok && r.bmssp_12t.ok;
        if (!ok) global_ok = false;
        const double max_diff = std::max({
            r.bmssp_1t.max_diff, r.bmssp_2t.max_diff, r.bmssp_4t.max_diff,
            r.bmssp_8t.max_diff, r.bmssp_12t.max_diff
        });
        const double best_par = std::min({
            r.bmssp_2t.median_ms, r.bmssp_4t.median_ms, r.bmssp_8t.median_ms, r.bmssp_12t.median_ms
        });
        const double vs_dijk = r.dijkstra_1t.median_ms / best_par;
        const double self_sc = r.bmssp_1t.median_ms / best_par;

        csv << r.spec.id << ",\"" << r.spec.label << "\"," << r.spec.family << ","
            << r.spec.n << "," << r.m << "," << r.max_out_deg << "," << r.reachable << "," << r.hash << ","
            << r.dijkstra_1t.median_ms << "," << r.bmssp_1t.median_ms << "," << r.bmssp_2t.median_ms << ","
            << r.bmssp_4t.median_ms << "," << r.bmssp_8t.median_ms << "," << r.bmssp_12t.median_ms << ","
            << best_par << "," << vs_dijk << "," << self_sc << "," << max_diff << "," << (ok ? "OK" : "FAIL") << "\n";

        std::cout << std::setprecision(2)
                  << "| " << r.spec.id
                  << " | " << r.spec.label
                  << " | " << r.spec.n
                  << " | " << r.m
                  << " | " << r.dijkstra_1t.median_ms
                  << " | " << r.bmssp_1t.median_ms
                  << " | " << r.bmssp_2t.median_ms
                  << " | " << r.bmssp_4t.median_ms
                  << " | " << r.bmssp_8t.median_ms
                  << " | " << r.bmssp_12t.median_ms
                  << " | **" << best_par << "**"
                  << " | **" << vs_dijk << "x**"
                  << " | " << self_sc << "x"
                  << " | " << (ok ? "OK" : "FAIL") << " |\n";
    }

    return global_ok ? 0 : 1;
}
