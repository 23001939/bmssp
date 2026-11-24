#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <string>
#include <utility>
#include <vector>

namespace bmssp {
namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Deterministic SplitMix64 hash -> GAP Benchmark Suite integer weight in [1, 255]
inline double gapbs_edge_weight(uint32_t u_raw, uint32_t v_raw) noexcept {
    uint64_t x = (static_cast<uint64_t>(u_raw) << 32) | static_cast<uint64_t>(v_raw);
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return static_cast<double>(1ULL + (x % 255ULL));
}

struct LoadedSnapGraph {
    CSRGraph graph;
    uint64_t raw_arcs{0};
    double parse_build_ms{0.0};
};

LoadedSnapGraph load_snap_edgelist(const std::string& path) {
    const auto t0 = Clock::now();
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        std::cerr << "[ERROR] Cannot open SNAP file: " << path << "\n";
        std::exit(1);
    }

    constexpr size_t BUF_SZ = 1 << 23; // 8 MB buffer
    std::vector<char> buf(BUF_SZ);
    size_t pos = 0, len = 0;

    auto next_char = [&]() noexcept -> int {
        if (pos >= len) {
            len = std::fread(buf.data(), 1, BUF_SZ, fp);
            pos = 0;
            if (len == 0) return EOF;
        }
        return static_cast<unsigned char>(buf[pos++]);
    };

    std::vector<std::pair<uint32_t, uint32_t>> raw_pairs;
    raw_pairs.reserve(32000000);
    uint32_t max_id = 0;

    int ch = next_char();
    while (ch != EOF) {
        while (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ch = next_char();
        }
        if (ch == EOF) break;
        if (ch == '#' || ch == '%') {
            while (ch != EOF && ch != '\n') ch = next_char();
            continue;
        }
        if (ch < '0' || ch > '9') {
            while (ch != EOF && ch != '\n') ch = next_char();
            continue;
        }

        uint32_t u = 0;
        while (ch >= '0' && ch <= '9') {
            u = u * 10U + static_cast<uint32_t>(ch - '0');
            ch = next_char();
        }
        while (ch == ' ' || ch == '\t') ch = next_char();
        uint32_t v = 0;
        while (ch >= '0' && ch <= '9') {
            v = v * 10U + static_cast<uint32_t>(ch - '0');
            ch = next_char();
        }
        while (ch != EOF && ch != '\n') ch = next_char();

        raw_pairs.emplace_back(u, v);
        if (u > max_id) max_id = u;
        if (v > max_id) max_id = v;
    }
    std::fclose(fp);

    const uint64_t raw_arcs = raw_pairs.size();

    // Compact vertex IDs into 0..n-1 preserving ascending original ID order
    std::vector<uint32_t> id_map(static_cast<size_t>(max_id) + 1, UINT32_MAX);
    for (const auto& [u, v] : raw_pairs) {
        id_map[u] = 1;
        id_map[v] = 1;
    }
    uint32_t n = 0;
    for (size_t i = 0; i < id_map.size(); ++i) {
        if (id_map[i] != UINT32_MAX) {
            id_map[i] = n++;
        }
    }

    std::vector<Edge> edges;
    edges.reserve(raw_pairs.size());
    for (const auto& [u_raw, v_raw] : raw_pairs) {
        const uint32_t u = id_map[u_raw];
        const uint32_t v = id_map[v_raw];
        if (u == v) continue;
        edges.push_back(Edge{u, v, gapbs_edge_weight(u_raw, v_raw)});
    }
    raw_pairs.clear();
    raw_pairs.shrink_to_fit();
    id_map.clear();
    id_map.shrink_to_fit();

    // Sort and deduplicate parallel directed arcs keeping min weight
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) noexcept {
        if (a.u != b.u) return a.u < b.u;
        if (a.v != b.v) return a.v < b.v;
        return a.w < b.w;
    });
    size_t write = 0;
    for (size_t i = 0; i < edges.size(); ++i) {
        if (write == 0 || edges[i].u != edges[write - 1].u || edges[i].v != edges[write - 1].v) {
            edges[write++] = edges[i];
        }
    }
    edges.resize(write);

    LoadedSnapGraph out;
    out.raw_arcs = raw_arcs;
    out.graph = CSRGraph::from_edge_list(n, edges);
    out.parse_build_ms = elapsed_ms(t0);
    return out;
}

uint32_t select_giant_source(const CSRGraph& g, SSSPResult& out_oracle, size_t& out_reachable) {
    for (uint32_t cand = 0; cand < std::min<uint32_t>(g.n, 64U); ++cand) {
        if (g.degree(cand) == 0) continue;
        SSSPResult r = dijkstra_sequential(g, cand);
        const size_t reach = static_cast<size_t>(
            std::count_if(r.dist.begin(), r.dist.end(), [](double d) { return std::isfinite(d); })
        );
        if (reach * 2 >= g.n || (cand == 0 && reach * 4 >= g.n)) {
            out_oracle = std::move(r);
            out_reachable = reach;
            return cand;
        }
        if (reach > out_reachable) {
            out_oracle = std::move(r);
            out_reachable = reach;
        }
    }
    return 0;
}

bool verify_oracle(const SSSPResult& oracle, const SSSPResult& res, double tol, double& max_diff) {
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

struct Stat {
    double median_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double max_diff{0.0};
    bool ok{true};
};

template <typename Fn>
Stat run_stat(Fn&& fn, int warmups, int samples, const SSSPResult& oracle) {
    Stat st;
    for (int i = 0; i < warmups; ++i) {
        SSSPResult r = fn();
        double diff = 0.0;
        if (!verify_oracle(oracle, r, 1e-9, diff)) {
            st.ok = false;
            return st;
        }
    }
    std::vector<double> ts;
    ts.reserve(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const auto t0 = Clock::now();
        SSSPResult r = fn();
        ts.push_back(elapsed_ms(t0));
        double diff = 0.0;
        if (!verify_oracle(oracle, r, 1e-9, diff)) st.ok = false;
        if (diff > st.max_diff) st.max_diff = diff;
    }
    std::sort(ts.begin(), ts.end());
    st.min_ms = ts.front();
    st.median_ms = ts[ts.size() / 2];
    st.max_ms = ts.back();
    return st;
}

} // namespace
} // namespace bmssp

int main(int argc, char** argv) {
    using namespace bmssp;
    if (argc < 4) {
        std::cerr << "Usage: bench_snap_highdeg <csv_path> <init|append> <NAME1:PATH1> [NAME2:PATH2 ...]\n";
        return 2;
    }

    const std::string csv_path = argv[1];
    const std::string mode = argv[2];
    omp_set_dynamic(0);

    std::ofstream csv;
    if (mode == "init") {
        csv.open(csv_path, std::ios::out | std::ios::trunc);
        csv << "dataset,n,raw_arcs,m,avg_deg,max_out_deg,source,reachable,parse_build_ms,"
               "dijkstra_1t_ms,bmssp_1t_ms,bmssp_2t_ms,bmssp_4t_ms,bmssp_8t_ms,bmssp_12t_ms,"
               "best_par_ms,best_threads,speedup_vs_dijkstra,self_speedup,max_diff,status\n";
    } else {
        csv.open(csv_path, std::ios::out | std::ios::app);
    }
    csv << std::fixed << std::setprecision(3);
    std::cout << std::fixed << std::setprecision(2);

    bool all_ok = true;
    for (int a = 3; a < argc; ++a) {
        const std::string spec = argv[a];
        const size_t colon = spec.find(':');
        const std::string name = spec.substr(0, colon);
        const std::string path = spec.substr(colon + 1);

        std::cout << "\n[*] Loading SNAP graph " << name << " from " << path << "..." << std::flush;
        LoadedSnapGraph lg = load_snap_edgelist(path);
        const CSRGraph& g = lg.graph;
        const double avg_deg = static_cast<double>(g.m) / static_cast<double>(g.n);

        SSSPResult oracle;
        size_t reachable = 0;
        const uint32_t src = select_giant_source(g, oracle, reachable);

        std::cout << " done in " << lg.parse_build_ms << " ms\n"
                  << "    n=" << g.n << ", raw_arcs=" << lg.raw_arcs << ", m=" << g.m
                  << ", avg_deg=" << avg_deg << ", max_out_deg=" << g.max_out_degree()
                  << ", src=" << src << ", reachable=" << reachable
                  << " (" << (100.0 * reachable / g.n) << "%)\n" << std::flush;

        const int warmups = 1;
        const int samples = 5;

        const Stat d1 = run_stat([&] { return dijkstra_sequential(g, src); }, warmups, samples, oracle);
        std::cout << "    Dijkstra (1T) : " << std::setw(8) << d1.median_ms << " ms\n" << std::flush;

        const Stat b1 = run_stat([&] { return bmssp_parallel(g, src, 1, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt  1T : " << std::setw(8) << b1.median_ms << " ms\n" << std::flush;

        const Stat b2 = run_stat([&] { return bmssp_parallel(g, src, 2, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt  2T : " << std::setw(8) << b2.median_ms << " ms\n" << std::flush;

        const Stat b4 = run_stat([&] { return bmssp_parallel(g, src, 4, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt  4T : " << std::setw(8) << b4.median_ms << " ms\n" << std::flush;

        const Stat b8 = run_stat([&] { return bmssp_parallel(g, src, 8, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt  8T : " << std::setw(8) << b8.median_ms << " ms\n" << std::flush;

        const Stat b12 = run_stat([&] { return bmssp_parallel(g, src, 12, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt 12T : " << std::setw(8) << b12.median_ms << " ms\n" << std::flush;

        const bool ok = d1.ok && b1.ok && b2.ok && b4.ok && b8.ok && b12.ok;
        if (!ok) all_ok = false;
        const double max_diff = std::max({b1.max_diff, b2.max_diff, b4.max_diff, b8.max_diff, b12.max_diff});

        struct Cand { int t; double ms; };
        const std::vector<Cand> cands = {{1, b1.median_ms}, {2, b2.median_ms}, {4, b4.median_ms}, {8, b8.median_ms}, {12, b12.median_ms}};
        auto best_it = std::min_element(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.ms < y.ms; });

        const double speedup_vs_dijk = d1.median_ms / best_it->ms;
        const double self_scale = b1.median_ms / best_it->ms;

        std::cout << "    => Best BMSSP = " << best_it->ms << " ms (" << best_it->t << "T)"
                  << " | Speedup vs Dijkstra = " << speedup_vs_dijk << "x"
                  << " | Self-Scale = " << self_scale << "x"
                  << " | MaxDiff = " << max_diff << " [" << (ok ? "OK" : "FAIL") << "]\n" << std::flush;

        csv << name << "," << g.n << "," << lg.raw_arcs << "," << g.m << "," << avg_deg << ","
            << g.max_out_degree() << "," << src << "," << reachable << "," << lg.parse_build_ms << ","
            << d1.median_ms << "," << b1.median_ms << "," << b2.median_ms << "," << b4.median_ms << ","
            << b8.median_ms << "," << b12.median_ms << "," << best_it->ms << "," << best_it->t << ","
            << speedup_vs_dijk << "," << self_scale << "," << max_diff << ","
            << (ok ? "OK" : "FAIL") << "\n" << std::flush;
    }

    return all_ok ? 0 : 1;
}
