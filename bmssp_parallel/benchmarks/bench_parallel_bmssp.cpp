#include "algorithm/bmssp.hpp"
#include "baseline/bmssp_seq.hpp"
#include "core/graph.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bmssp {

namespace {

struct VerifyOutcome {
    bool ok{false};
    double max_diff{0.0};
    std::string reason;
};

bool verify_pred_tree_basic(
    const CSRGraph& g,
    uint32_t source,
    const SSSPResult& res,
    double tol,
    std::string& err_reason
) {
    if (res.pred.size() != g.n || res.dist.size() != g.n) {
        err_reason = "pred/dist size mismatch";
        return false;
    }
    if (g.n == 0 || source >= g.n) {
        return true;
    }
    if (res.pred[source] != NO_PRED) {
        err_reason = "pred[source] != NO_PRED";
        return false;
    }

    for (uint32_t v = 0; v < g.n; ++v) {
        if (res.pred[v] == v) {
            err_reason = "self-loop pred";
            return false;
        }
        if (std::isinf(res.dist[v])) {
            if (res.pred[v] != NO_PRED) {
                err_reason = "unreachable vertex has pred";
                return false;
            }
        } else if (v != source) {
            uint32_t u = res.pred[v];
            if (u == NO_PRED || u >= g.n || !std::isfinite(res.dist[u])) {
                err_reason = "invalid pred index";
                return false;
            }
        }
    }

    std::vector<uint8_t> edge_matched(g.n, 0);
    std::vector<double> best_err(g.n, INF_DIST);
    for (uint32_t u = 0; u < g.n; ++u) {
        if (!std::isfinite(res.dist[u])) continue;
        for (uint64_t e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            uint32_t v = g.edge_dest(e);
            if (v >= g.n || v == source || !std::isfinite(res.dist[v])) continue;
            if (res.pred[v] != u) continue;
            double err = std::abs(res.dist[u] + g.edge_weight(e) - res.dist[v]);
            if (!edge_matched[v] || err < best_err[v]) {
                edge_matched[v] = 1;
                best_err[v] = err;
            }
        }
    }

    for (uint32_t v = 0; v < g.n; ++v) {
        if (v != source && std::isfinite(res.dist[v])) {
            if (!edge_matched[v] || best_err[v] > tol) {
                err_reason = "missing/mismatched edge for pred";
                return false;
            }
        }
    }

    std::vector<uint8_t> color(g.n, 0);
    color[source] = 2;
    std::vector<uint32_t> chain;
    for (uint32_t v = 0; v < g.n; ++v) {
        if (!std::isfinite(res.dist[v]) || color[v] == 2) continue;
        chain.clear();
        uint32_t cur = v;
        uint32_t steps = 0;
        while (color[cur] != 2) {
            if (color[cur] == 1 || steps >= g.n) {
                err_reason = "cycle in pred chain";
                return false;
            }
            color[cur] = 1;
            chain.push_back(cur);
            cur = res.pred[cur];
            ++steps;
            if (cur >= g.n) {
                err_reason = "broken pred chain";
                return false;
            }
        }
        for (uint32_t x : chain) {
            color[x] = 2;
        }
    }
    return true;
}

VerifyOutcome verify_against_oracle(
    const CSRGraph& g,
    uint32_t source,
    const SSSPResult& oracle,
    const SSSPResult& res,
    bool check_pred_tree,
    double tol = 1e-9
) {
    VerifyOutcome out;
    if (res.dist.size() != oracle.dist.size() || res.dist.size() != g.n) {
        out.ok = false;
        out.reason = "distance vector size mismatch";
        return out;
    }

    double max_diff = 0.0;
    for (uint32_t v = 0; v < g.n; ++v) {
        double d_exp = oracle.dist[v];
        double d_got = res.dist[v];
        if (std::isinf(d_exp)) {
            if (!std::isinf(d_got) || d_got < 0.0) {
                out.ok = false;
                out.reason = "INF mismatch";
                return out;
            }
        } else {
            if (!std::isfinite(d_got)) {
                out.ok = false;
                out.reason = "finite mismatch";
                return out;
            }
            double diff = std::abs(d_exp - d_got);
            max_diff = std::max(max_diff, diff);
            if (diff > tol) {
                out.ok = false;
                out.max_diff = max_diff;
                out.reason = "distance tolerance exceeded";
                return out;
            }
        }
    }

    if (check_pred_tree) {
        std::string pred_err;
        if (!verify_pred_tree_basic(g, source, res, tol, pred_err)) {
            out.ok = false;
            out.max_diff = max_diff;
            out.reason = pred_err;
            return out;
        }
    }

    out.ok = true;
    out.max_diff = max_diff;
    return out;
}

CSRGraph make_sparse_random(uint32_t n, uint64_t m, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint32_t> dist_v(0, n - 1);
    std::uniform_real_distribution<double> dist_w(1.0, 100.0);

    std::vector<Edge> edges;
    edges.reserve(m);
    while (edges.size() < m) {
        uint32_t u = dist_v(rng);
        uint32_t v = dist_v(rng);
        if (u == v) continue;
        edges.push_back({u, v, dist_w(rng)});
    }
    return CSRGraph::from_edge_list(n, edges);
}

CSRGraph make_grid_2d(uint32_t rows, uint32_t cols, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> dist_w(1.0, 20.0);

    const uint32_t n = rows * cols;
    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * 4);

    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            uint32_t u = r * cols + c;
            if (r > 0)        edges.push_back({u, (r - 1) * cols + c, dist_w(rng)});
            if (r + 1 < rows) edges.push_back({u, (r + 1) * cols + c, dist_w(rng)});
            if (c > 0)        edges.push_back({u, r * cols + (c - 1), dist_w(rng)});
            if (c + 1 < cols) edges.push_back({u, r * cols + (c + 1), dist_w(rng)});
        }
    }
    return CSRGraph::from_edge_list(n, edges);
}

CSRGraph make_hub_graph(uint32_t n, uint32_t hub_degree, uint64_t bg_edges, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> dist_hub_w(1.0, 50.0);
    std::uniform_real_distribution<double> dist_bg_w(1.0, 100.0);

    std::vector<uint32_t> targets(n - 1);
    for (uint32_t i = 0; i < n - 1; ++i) {
        targets[i] = i + 1;
    }
    std::shuffle(targets.begin(), targets.end(), rng);

    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(hub_degree) + bg_edges);
    for (uint32_t i = 0; i < hub_degree && i < n - 1; ++i) {
        edges.push_back({0, targets[i], dist_hub_w(rng)});
    }

    std::uniform_int_distribution<uint32_t> dist_u(1, n - 1);
    std::uniform_int_distribution<uint32_t> dist_v(0, n - 1);
    uint64_t added = 0;
    while (added < bg_edges) {
        uint32_t u = dist_u(rng);
        uint32_t v = dist_v(rng);
        if (u == v) continue;
        edges.push_back({u, v, dist_bg_w(rng)});
        ++added;
    }
    return CSRGraph::from_edge_list(n, edges);
}

struct SolverRow {
    std::string solver_name;
    size_t threads{1};
    int group{0}; // 0=baseline, 1=mutex, 2=concurrent
    double median_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double max_diff{0.0};
    bool ok{false};
    std::string fail_reason;
};

template <typename Fn>
SolverRow measure_solver(
    const std::string& name,
    size_t threads,
    int group,
    const CSRGraph& g,
    uint32_t source,
    const SSSPResult& oracle,
    bool check_pred_tree,
    Fn&& fn
) {
    constexpr int kWarmups = 2;
    constexpr int kSamples = 7;

    bool all_ok = true;
    double worst_diff = 0.0;
    std::string first_err;

    for (int w = 0; w < kWarmups; ++w) {
        SSSPResult res = fn();
        VerifyOutcome v = verify_against_oracle(g, source, oracle, res, check_pred_tree);
        if (!v.ok) {
            all_ok = false;
            if (first_err.empty()) first_err = v.reason;
        }
        worst_diff = std::max(worst_diff, v.max_diff);
    }

    std::array<double, kSamples> times{};
    for (int s = 0; s < kSamples; ++s) {
        auto t0 = std::chrono::steady_clock::now();
        SSSPResult res = fn();
        auto t1 = std::chrono::steady_clock::now();
        times[static_cast<size_t>(s)] = std::chrono::duration<double, std::milli>(t1 - t0).count();

        VerifyOutcome v = verify_against_oracle(g, source, oracle, res, check_pred_tree);
        if (!v.ok) {
            all_ok = false;
            if (first_err.empty()) first_err = v.reason;
        }
        worst_diff = std::max(worst_diff, v.max_diff);
    }

    std::sort(times.begin(), times.end());

    SolverRow row;
    row.solver_name = name;
    row.threads = threads;
    row.group = group;
    row.median_ms = times[kSamples / 2];
    row.min_ms = times.front();
    row.max_ms = times.back();
    row.max_diff = worst_diff;
    row.ok = all_ok;
    row.fail_reason = first_err;
    return row;
}

uint32_t count_reachable(const SSSPResult& oracle) {
    uint32_t count = 0;
    for (double d : oracle.dist) {
        if (std::isfinite(d)) {
            ++count;
        }
    }
    return count;
}

std::string fmt_fixed(double val, int prec = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << val;
    return oss.str();
}

std::string fmt_sci(double val) {
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(2) << val;
    return oss.str();
}

bool run_graph_benchmark(const std::string& title, const CSRGraph& g, uint32_t source) {
    SSSPResult oracle = dijkstra_sequential(g, source);
    uint32_t reachable = count_reachable(oracle);

    std::cout << "\n### " << title << "\n";
    std::cout << "- Graph: `n=" << g.n << "`, `m=" << g.m << "`, `max_out_degree="
              << g.max_out_degree() << "`, `source=" << source << "`, `reachable="
              << reachable << "/" << g.n << "`\n\n";

    std::vector<SolverRow> rows;
    rows.reserve(8);

    rows.push_back(measure_solver(
        "dijkstra_sequential", 1, 0, g, source, oracle, true,
        [&]() { return dijkstra_sequential(g, source); }));

    rows.push_back(measure_solver(
        "bmssp_seq", 1, 0, g, source, oracle, false,
        [&]() { return bmssp_sequential(g, source); }));

    for (size_t T : {size_t{1}, size_t{4}, size_t{8}}) {
        rows.push_back(measure_solver(
            "bmssp_parallel (Mutex)", T, 1, g, source, oracle, true,
            [&, T]() { return bmssp_parallel(g, source, T, false, FrontierType::Mutex); }));
    }

    for (size_t T : {size_t{1}, size_t{4}, size_t{8}}) {
        rows.push_back(measure_solver(
            "bmssp_parallel (Concurrent)", T, 2, g, source, oracle, true,
            [&, T]() { return bmssp_parallel(g, source, T, false, FrontierType::Concurrent); }));
    }

    const double dijkstra_ms = rows[0].median_ms;
    double mutex_1t_ms = 0.0;
    double conc_1t_ms = 0.0;
    for (const auto& r : rows) {
        if (r.group == 1 && r.threads == 1) mutex_1t_ms = r.median_ms;
        if (r.group == 2 && r.threads == 1) conc_1t_ms = r.median_ms;
    }

    std::cout << "| Solver | Threads | Median (ms) | Min (ms) | Max (ms) | vs Dijkstra | Self-Scale | MaxDiff | Status |\n";
    std::cout << "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: |\n";

    bool all_ok = true;
    for (const auto& r : rows) {
        if (!r.ok) all_ok = false;

        std::string vs_dijk = (r.median_ms > 0.0) ? (fmt_fixed(dijkstra_ms / r.median_ms, 2) + "x") : "N/A";
        std::string self_scale = "-";
        if (r.group == 1 && r.median_ms > 0.0) {
            self_scale = fmt_fixed(mutex_1t_ms / r.median_ms, 2) + "x";
        } else if (r.group == 2 && r.median_ms > 0.0) {
            self_scale = fmt_fixed(conc_1t_ms / r.median_ms, 2) + "x";
        }

        std::cout << "| " << r.solver_name
                  << " | " << r.threads
                  << " | " << fmt_fixed(r.median_ms, 3)
                  << " | " << fmt_fixed(r.min_ms, 3)
                  << " | " << fmt_fixed(r.max_ms, 3)
                  << " | " << vs_dijk
                  << " | " << self_scale
                  << " | " << fmt_sci(r.max_diff)
                  << " | " << (r.ok ? "OK" : "FAIL")
                  << " |\n";
    }

    // A/B direct summary (Concurrent vs Mutex) at 1T, 4T, 8T
    std::cout << "\n- A/B Speedup (`Mutex / Concurrent`): ";
    bool first = true;
    for (size_t T : {size_t{1}, size_t{4}, size_t{8}}) {
        double mtx_ms = 0.0;
        double cnc_ms = 0.0;
        for (const auto& r : rows) {
            if (r.group == 1 && r.threads == T) mtx_ms = r.median_ms;
            if (r.group == 2 && r.threads == T) cnc_ms = r.median_ms;
        }
        if (!first) std::cout << " | ";
        first = false;
        std::cout << T << "T=" << (cnc_ms > 0.0 ? fmt_fixed(mtx_ms / cnc_ms, 2) + "x" : "N/A");
    }
    std::cout << "\n";

    for (const auto& r : rows) {
        if (!r.ok) {
            std::cout << "- [FAIL] `" << r.solver_name << "` (" << r.threads << "T): "
                      << r.fail_reason << "\n";
        }
    }

    return all_ok;
}

} // namespace

} // namespace bmssp

int main() {
    using namespace bmssp;

    std::cout << "# Parallel BMSSP A/B Benchmark Report\n\n";
    std::cout << "- Hardware concurrency (`std::thread::hardware_concurrency`): "
              << std::thread::hardware_concurrency() << " threads\n";
    std::cout << "- Methodology: 2 warmup + 7 timed sequential samples per configuration (median `solve_ms` reported).\n";
    std::cout << "- Scope: Graph generation excluded from timing. `bmssp_seq` (`bmssp_sequential`) includes internal `spp::bmssp` edge loading and `prepare_graph(false)`. `bmssp_parallel` runs directly on CSR with `use_gadget_transform=false`.\n";
    std::cout << "- Oracle verification: All solvers verified against `dijkstra_sequential` (`tol=1e-9`); `bmssp_parallel` also verifies full predecessor tree invariants.\n";

    // Build all 3 graphs outside timing windows
    CSRGraph g_sparse = make_sparse_random(20000, 100000, 2026ULL);
    CSRGraph g_grid = make_grid_2d(150, 150, 202602ULL);
    CSRGraph g_hub = make_hub_graph(20000, 10000, 80000, 202603ULL);

    bool ok1 = run_graph_benchmark("1. Sparse Random (`n=20000, m=100000, seed=2026`)", g_sparse, 0);
    bool ok2 = run_graph_benchmark("2. 2D Grid (`150x150, n=22500, 4-neighbor directed`)", g_grid, 0);
    bool ok3 = run_graph_benchmark("3. Hub Graph (`n=20000, deg(0)=10000 + 80000 sparse bg`)", g_hub, 0);

    bool all_ok = ok1 && ok2 && ok3;
    std::cout << "\nOverall Verification Status: " << (all_ok ? "OK" : "FAIL") << std::endl;
    return all_ok ? 0 : 1;
}
