#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <string>
#include <thread>
#include <vector>

namespace bmssp {
namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct LoadedRoadGraph {
    CSRGraph graph;
    uint64_t raw_arcs{0};
    double parse_build_ms{0.0};
};

// High-speed buffered DIMACS .gr parser with blog-identical normalization:
// 1-based -> 0-based, drop self-loops (u == v), deduplicate parallel arcs keeping min(weight).
LoadedRoadGraph load_dimacs_gr(const std::string& path) {
    const auto t0 = Clock::now();
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        std::cerr << "[ERROR] Cannot open file: " << path << "\n";
        std::exit(1);
    }

    constexpr size_t BUF_SZ = 1 << 22; // 4 MB buffer
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

    uint32_t n = 0;
    uint64_t expected_m = 0;
    std::vector<Edge> raw_edges;

    int ch = next_char();
    while (ch != EOF) {
        while (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ch = next_char();
        }
        if (ch == EOF) break;

        if (ch == 'a') {
            ch = next_char();
            while (ch == ' ' || ch == '\t') ch = next_char();
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
            while (ch == ' ' || ch == '\t') ch = next_char();
            uint64_t w_int = 0;
            double w = 0.0;
            bool is_float = false;
            while (ch >= '0' && ch <= '9') {
                w_int = w_int * 10ULL + static_cast<uint64_t>(ch - '0');
                ch = next_char();
            }
            if (ch == '.') {
                is_float = true;
                w = static_cast<double>(w_int);
                double scale = 0.1;
                ch = next_char();
                while (ch >= '0' && ch <= '9') {
                    w += static_cast<double>(ch - '0') * scale;
                    scale *= 0.1;
                    ch = next_char();
                }
            } else {
                w = static_cast<double>(w_int);
            }
            (void)is_float;
            while (ch != EOF && ch != '\n') ch = next_char();

            if (u >= 1 && v >= 1) {
                const uint32_t u0 = u - 1;
                const uint32_t v0 = v - 1;
                if (u0 != v0) {
                    raw_edges.push_back(Edge{u0, v0, w});
                }
            }
        } else if (ch == 'p') {
            // p sp <nodes> <arcs>
            while (ch != EOF && (ch < '0' || ch > '9')) ch = next_char();
            while (ch >= '0' && ch <= '9') {
                n = n * 10U + static_cast<uint32_t>(ch - '0');
                ch = next_char();
            }
            while (ch != EOF && (ch < '0' || ch > '9')) ch = next_char();
            while (ch >= '0' && ch <= '9') {
                expected_m = expected_m * 10ULL + static_cast<uint64_t>(ch - '0');
                ch = next_char();
            }
            raw_edges.reserve(static_cast<size_t>(expected_m));
            while (ch != EOF && ch != '\n') ch = next_char();
        } else {
            // comment 'c' or other header line
            while (ch != EOF && ch != '\n') ch = next_char();
        }
    }
    std::fclose(fp);

    if (n == 0 && !raw_edges.empty()) {
        for (const auto& e : raw_edges) {
            n = std::max(n, std::max(e.u, e.v) + 1U);
        }
    }

    // Deduplicate parallel edges (u, v) keeping minimum weight (matches blog workflow)
    std::sort(raw_edges.begin(), raw_edges.end(), [](const Edge& a, const Edge& b) noexcept {
        if (a.u != b.u) return a.u < b.u;
        if (a.v != b.v) return a.v < b.v;
        return a.w < b.w;
    });

    size_t write = 0;
    for (size_t i = 0; i < raw_edges.size(); ++i) {
        if (write == 0 || raw_edges[i].u != raw_edges[write - 1].u ||
            raw_edges[i].v != raw_edges[write - 1].v) {
            raw_edges[write++] = raw_edges[i];
        }
    }
    raw_edges.resize(write);

    LoadedRoadGraph out;
    out.raw_arcs = expected_m;
    out.graph = CSRGraph::from_edge_list(n, raw_edges);
    out.parse_build_ms = elapsed_ms(t0);
    return out;
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
        std::cerr << "Usage: bench_dimacs_road <csv_path> <init|append> <NAME1:PATH1> [NAME2:PATH2 ...]\n";
        return 2;
    }

    const std::string csv_path = argv[1];
    const std::string mode = argv[2];
    omp_set_dynamic(0);

    std::ofstream csv;
    if (mode == "init") {
        csv.open(csv_path, std::ios::out | std::ios::trunc);
        csv << "code,n,raw_arcs,m,max_out_deg,reachable,parse_build_ms,dijkstra_1t_ms,"
               "bmssp_1t_ms,bmssp_4t_ms,bmssp_8t_ms,bmssp_12t_ms,best_par_ms,"
               "ratio_bmssp_over_dijkstra,self_speedup,max_diff,status\n";
    } else {
        csv.open(csv_path, std::ios::out | std::ios::app);
    }
    csv << std::fixed << std::setprecision(3);
    std::cout << std::fixed << std::setprecision(2);

    bool all_ok = true;
    for (int a = 3; a < argc; ++a) {
        const std::string spec = argv[a];
        const size_t colon = spec.find(':');
        const std::string code = spec.substr(0, colon);
        const std::string path = spec.substr(colon + 1);

        std::cout << "\n[*] Loading " << code << " from " << path << "..." << std::flush;
        LoadedRoadGraph lg = load_dimacs_gr(path);
        const CSRGraph& g = lg.graph;

        SSSPResult oracle = dijkstra_sequential(g, 0);
        const size_t reachable = static_cast<size_t>(
            std::count_if(oracle.dist.begin(), oracle.dist.end(), [](double d) { return std::isfinite(d); })
        );
        std::cout << " done in " << lg.parse_build_ms << " ms (n=" << g.n
                  << ", raw_arcs=" << lg.raw_arcs << ", m=" << g.m
                  << ", max_deg=" << g.max_out_degree() << ", reachable=" << reachable << ")\n" << std::flush;

        const int warmups = 1;
        const int samples = 5;

        const Stat d1 = run_stat([&] { return dijkstra_sequential(g, 0); }, warmups, samples, oracle);
        std::cout << "    Dijkstra (1T) : " << std::setw(8) << d1.median_ms << " ms\n" << std::flush;

        const Stat b1 = run_stat([&] { return bmssp_parallel(g, 0, 1, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt  1T : " << std::setw(8) << b1.median_ms << " ms\n" << std::flush;

        const Stat b4 = run_stat([&] { return bmssp_parallel(g, 0, 4, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt  4T : " << std::setw(8) << b4.median_ms << " ms\n" << std::flush;

        const Stat b8 = run_stat([&] { return bmssp_parallel(g, 0, 8, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt  8T : " << std::setw(8) << b8.median_ms << " ms\n" << std::flush;

        const Stat b12 = run_stat([&] { return bmssp_parallel(g, 0, 12, false, FrontierType::Concurrent); }, warmups, samples, oracle);
        std::cout << "    BMSSP Opt 12T : " << std::setw(8) << b12.median_ms << " ms\n" << std::flush;

        const bool ok = d1.ok && b1.ok && b4.ok && b8.ok && b12.ok;
        if (!ok) all_ok = false;
        const double max_diff = std::max({b1.max_diff, b4.max_diff, b8.max_diff, b12.max_diff});
        const double best_par = std::min({b4.median_ms, b8.median_ms, b12.median_ms});
        const double ratio_over_dijk = best_par / d1.median_ms;
        const double self_scale = b1.median_ms / best_par;

        std::cout << "    => Best Parallel BMSSP = " << best_par << " ms | BMSSP/Dijkstra = "
                  << ratio_over_dijk << "x | Self-Scale = " << self_scale
                  << "x | MaxDiff = " << max_diff << " [" << (ok ? "OK" : "FAIL") << "]\n" << std::flush;

        csv << code << "," << g.n << "," << lg.raw_arcs << "," << g.m << "," << g.max_out_degree() << ","
            << reachable << "," << lg.parse_build_ms << "," << d1.median_ms << ","
            << b1.median_ms << "," << b4.median_ms << "," << b8.median_ms << "," << b12.median_ms << ","
            << best_par << "," << ratio_over_dijk << "," << self_scale << "," << max_diff << ","
            << (ok ? "OK" : "FAIL") << "\n" << std::flush;
    }

    return all_ok ? 0 : 1;
}
