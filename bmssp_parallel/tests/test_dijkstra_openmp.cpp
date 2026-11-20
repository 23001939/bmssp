#include "baseline/dijkstra_openmp.hpp"
#include <omp.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace bmssp;
namespace {
size_t checks = 0, comparisons = 0;
void require(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::vector<double> bellman_ford(const CSRGraph& g, uint32_t source) {
    std::vector<double> d(g.n, INF_DIST);
    if (source >= g.n) return d;
    d[source] = 0;
    for (uint32_t round = 1; round < g.n; ++round) {
        bool changed = false;
        for (uint32_t u = 0; u < g.n; ++u)
            for (auto e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                auto v = g.edge_dest(e);
                if (d[u] + g.edge_weight(e) < d[v]) {
                    d[v] = d[u] + g.edge_weight(e); changed = true;
                }
            }
        if (!changed) break;
    }
    return d;
}
void validate_paths(const CSRGraph& g, uint32_t source, const SSSPResult& result) {
    for (uint32_t v = 0; v < g.n; ++v) {
        if (v == source || !std::isfinite(result.dist[v])) {
            require(result.pred[v] == NO_PRED, "source/unreachable predecessor");
            continue;
        }
        auto current = v;
        uint32_t hops = 0;
        while (current != source && hops++ < g.n) {
            auto parent = result.pred[current];
            require(parent < g.n, "predecessor index");
            bool found = false;
            for (auto e = g.edge_begin(parent); e < g.edge_end(parent); ++e)
                if (g.edge_dest(e) == current &&
                    result.dist[parent] + g.edge_weight(e) == result.dist[current]) found = true;
            require(found, "predecessor edge must certify exact distance");
            current = parent;
        }
        require(current == source, "predecessor cycle");
    }
}
void compare(const CSRGraph& g, uint32_t source, bool use_bf = true, int repeats = 1) {
    const auto original_offsets = g.row_ptr;
    const auto original_destinations = g.col_idx;
    const auto original_weights = g.weight;
    auto seq = dijkstra_sequential(g, source);
    auto bf = use_bf ? bellman_ford(g, source) : seq.dist;
    const PreparedDijkstraGraph prepared(g);
    static_assert(std::is_same_v<decltype(prepared.graph()), const CSRGraph&>);
    const auto scan = dijkstra_scan_sequential(g, source);
    const auto scan_core = dijkstra_scan_prepared(prepared, source);
    require(scan.dist == seq.dist, "serial scan mismatch vs heap");
    require(scan_core.dist == scan.dist && scan_core.pred == scan.pred,
            "prepared scan mismatch vs serial scan");
    validate_paths(g, source, scan);
    validate_paths(g, source, scan_core);
    comparisons += 2;
    for (int threads : {0, 1, 2, 4, 8}) {
        SSSPResult previous;
        for (int repeat = 0; repeat < repeats; ++repeat) {
            auto result = dijkstra_openmp(g, source, threads);
            auto core = dijkstra_openmp_prepared(prepared, source, threads);
            require(result.dist.size() == g.n && result.pred.size() == g.n, "result dimensions");
            require(result.pred == scan.pred, "OpenMP predecessor mismatch vs serial scan");
            require(core.dist == result.dist && core.pred == result.pred,
                    "prepared OpenMP mismatch vs full API");
            for (uint32_t v = 0; v < g.n; ++v) {
                require(result.dist[v] == seq.dist[v], "exact distance mismatch vs sequential");
                require(result.dist[v] == bf[v] ||
                    (std::isfinite(result.dist[v]) && std::isfinite(bf[v]) &&
                     std::abs(result.dist[v] - bf[v]) <= 1e-12 * std::max(1.0, std::abs(bf[v]))),
                    "distance mismatch vs Bellman-Ford");
            }
            validate_paths(g, source, result);
            validate_paths(g, source, core);
            if (repeat) require(result.dist == previous.dist && result.pred == previous.pred,
                                "nondeterministic result");
            previous = result;
            comparisons += 2;
        }
    }
    require(g.row_ptr == original_offsets && g.col_idx == original_destinations &&
            g.weight == original_weights, "solver must not mutate input CSR");
}
template<class F> void invalid(F f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid input must throw invalid_argument");
}
void invalid_graph(const CSRGraph& graph) {
    invalid([&]{ dijkstra_openmp(graph, 0, 2); });
    invalid([&]{ dijkstra_scan_sequential(graph, 0); });
    invalid([&]{ PreparedDijkstraGraph prepared(graph); });
}
}

int main() {
    try {
        // Runtime default remains bounded even on machines with many logical CPUs.
        omp_set_num_threads(4);
        std::vector<Edge> edges;
        auto undirected = [&](uint32_t u, uint32_t v, double w) {
            edges.push_back({u,v,w}); edges.push_back({v,u,w});
        };
        undirected(0,1,40); undirected(0,2,15); undirected(1,2,20);
        undirected(1,3,10); undirected(1,4,25); undirected(1,5,6);
        undirected(2,3,100); undirected(4,5,8);
        auto sample = CSRGraph::from_edge_list(6, edges);
        require(dijkstra_openmp(sample, 0, 4).dist == std::vector<double>({0,35,15,45,49,41}),
                "six-vertex known answer");
        compare(sample, 0, true, 4);
        // One prepared object supports repeated sources and both solvers.
        const PreparedDijkstraGraph reusable(sample);
        for (uint32_t source = 0; source < sample.n; ++source) {
            const auto reference = dijkstra_sequential(sample, source);
            const auto scan = dijkstra_scan_prepared(reusable, source);
            const auto parallel = dijkstra_openmp_prepared(reusable, source, 4);
            require(scan.dist == reference.dist, "prepared reuse serial distances");
            require(parallel.dist == scan.dist && parallel.pred == scan.pred,
                    "prepared reuse OpenMP result");
            validate_paths(sample, source, parallel);
            comparisons += 2;
        }
        auto mutable_input = CSRGraph::from_edge_list(3,
            {{0,1,7},{0,1,2},{0,1,5},{0,0,0},{1,2,0}});
        const PreparedDijkstraGraph owned(mutable_input);
        require(owned.graph().m == 3, "coalesce duplicate edges");
        require(owned.graph().row_ptr == std::vector<uint64_t>({0,2,3,3}),
                "normalized CSR offsets");
        require(owned.graph().col_idx == std::vector<uint32_t>({0,1,2}) &&
                owned.graph().weight == std::vector<double>({0,2,0}),
                "normalized destinations and minimum parallel weight");
        mutable_input.weight.assign(mutable_input.m, 99);
        require(dijkstra_scan_prepared(owned, 0).dist == std::vector<double>({0,2,2}),
                "prepared graph must own data independently of input");
        const PreparedDijkstraGraph default_empty(CSRGraph{});
        require(dijkstra_scan_prepared(default_empty, 0).dist.empty(), "default empty scan");
        require(dijkstra_openmp_prepared(default_empty, 0, 4).dist.empty(), "default empty OpenMP");
        // Valid smaller actual team independent of OMP_DYNAMIC/THREAD_LIMIT:
        // nested regions serialize when the active nesting limit is one.
        const int previous_levels = omp_get_max_active_levels();
        omp_set_max_active_levels(1);
        SSSPResult nested;
        #pragma omp parallel num_threads(2) shared(nested, sample)
        {
            #pragma omp single
            nested = dijkstra_openmp(sample, 0, 8);
        }
        omp_set_max_active_levels(previous_levels);
        require(nested.dist == dijkstra_sequential(sample, 0).dist, "serialized nested team");
        validate_paths(sample, 0, nested);
        ++comparisons;
        compare(sample, 3);
        compare(CSRGraph::from_edge_list(0, {}), 0);
        compare(CSRGraph::from_edge_list(1, {}), 0, true, 3);
        compare(CSRGraph::from_edge_list(3, {}), 1);
        compare(sample, 99);
        compare(CSRGraph::from_edge_list(8, {{3,0,0},{0,1,0},{1,0,0},{1,2,0},
                 {3,2,1},{3,4,2},{4,5,1},{3,5,3},{7,6,0},{2,2,0}}), 3, true, 5);
        edges.clear();
        for (int k=200; k>=0; --k) {
            edges.push_back({0,1,static_cast<double>(k)});
            edges.push_back({0,2,static_cast<double>(k + 1)});
        }
        edges.push_back({1,2,0}); edges.push_back({2,3,200001});
        compare(CSRGraph::from_edge_list(5, edges), 0, true, 15);
        compare(CSRGraph::from_edge_list(4, {{0,1,1},{0,2,1 + 1e-11},
                 {1,3,1e-11},{2,3,0}}), 0, true, 5);
        const double huge = std::numeric_limits<double>::max() * 0.75;
        compare(CSRGraph::from_edge_list(3, {{0,1,huge},{1,2,huge}}), 0);

        std::mt19937 rng(20261119);
        for (int trial=0; trial<120; ++trial) {
            uint32_t n = 1 + rng()%45;
            edges.clear();
            for (uint32_t k=0, count=rng()%(n*n+1); k<count; ++k) {
                double w = static_cast<double>(rng()%51);
                if (trial%2) w /= 7.0;
                edges.push_back({static_cast<uint32_t>(rng()%n), static_cast<uint32_t>(rng()%n), w});
            }
            compare(CSRGraph::from_edge_list(n, edges), rng()%n, true, trial<5 ? 3 : 1);
        }

        invalid([&]{ dijkstra_openmp(sample, 0, -1); });
        invalid([&]{ dijkstra_openmp_prepared(reusable, 0, -1); });
        for (double w : {-1.0, INF_DIST, std::numeric_limits<double>::quiet_NaN()})
            invalid_graph(CSRGraph::from_edge_list(2, {{0,1,w}}));
        auto malformed = sample;
        malformed.row_ptr.pop_back(); invalid_graph(malformed);
        malformed = sample; malformed.row_ptr[0]=1;
        invalid_graph(malformed);
        malformed = sample; malformed.row_ptr[2]=malformed.m+1;
        invalid_graph(malformed);
        malformed = sample; malformed.row_ptr[2]=0;
        invalid_graph(malformed);
        malformed = sample; malformed.col_idx[0]=sample.n;
        invalid_graph(malformed);
        malformed = sample; malformed.weight.pop_back();
        invalid_graph(malformed);
        malformed = sample; ++malformed.m;
        invalid_graph(malformed);
        std::cout << "PASS: " << checks << " checks, " << comparisons
                  << " full-result comparisons; seed=20261119\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
