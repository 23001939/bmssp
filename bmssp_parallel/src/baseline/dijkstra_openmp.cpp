// Independently implemented for bmssp; algorithm references and licenses:
// ../../DIJKSTRA_OPENMP_REVIEW.md and ../../references/dijkstra_openmp/.
#include "baseline/dijkstra_openmp.hpp"

#include <omp.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace bmssp {
namespace {
void validate(const CSRGraph& g) {
    if (g.row_ptr.size() != static_cast<size_t>(g.n) + 1 ||
        g.col_idx.size() != g.m || g.weight.size() != g.m ||
        g.row_ptr.front() != 0 || g.row_ptr.back() != g.m) {
        throw std::invalid_argument("Dijkstra: invalid CSR dimensions/offsets");
    }
    for (uint32_t u = 0; u < g.n; ++u) {
        if (g.row_ptr[u] > g.row_ptr[u + 1]) {
            throw std::invalid_argument("Dijkstra: CSR offsets must be monotone");
        }
    }
    for (uint64_t e = 0; e < g.m; ++e) {
        if (g.col_idx[e] >= g.n || !std::isfinite(g.weight[e]) || g.weight[e] < 0) {
            throw std::invalid_argument("Dijkstra: invalid endpoint or negative/nonfinite weight");
        }
    }
}

struct alignas(64) Candidate {
    double distance = INF_DIST;
    uint32_t vertex = NO_PRED;
};

bool better(double d, uint32_t v, const Candidate& old) {
    return d < old.distance || (d == old.distance && v < old.vertex);
}
} // namespace

PreparedDijkstraGraph::PreparedDijkstraGraph(const CSRGraph& graph) {
    // A default-constructed empty CSR has no row_ptr. Preserve oracle behavior.
    if (graph.n == 0 && graph.m == 0 && graph.row_ptr.empty() &&
        graph.col_idx.empty() && graph.weight.empty()) {
        graph_ = CSRGraph(0, 0);
        return;
    }
    validate(graph);

    // Normalize duplicate destinations BEFORE concurrent edge relaxations.
    // Parallelizing raw CSR edges would race on dist[v] and pred[v].
    graph_.n = graph.n;
    graph_.row_ptr.assign(static_cast<size_t>(graph.n) + 1, 0);
    graph_.col_idx.reserve(graph.m);
    graph_.weight.reserve(graph.m);
    std::vector<std::pair<uint32_t, double>> row;
    for (uint32_t u = 0; u < graph.n; ++u) {
        row.clear();
        for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e)
            row.emplace_back(graph.col_idx[e], graph.weight[e]);
        std::sort(row.begin(), row.end());
        for (const auto& edge : row) {
            // Sorting by (destination,weight) puts minimum parallel weight first.
            if (graph_.col_idx.size() == graph_.row_ptr[u] ||
                graph_.col_idx.back() != edge.first) {
                graph_.col_idx.push_back(edge.first);
                graph_.weight.push_back(edge.second);
            }
        }
        graph_.row_ptr[u + 1] = graph_.col_idx.size();
    }
    graph_.m = graph_.col_idx.size();
}

SSSPResult dijkstra_scan_prepared(const PreparedDijkstraGraph& prepared, uint32_t source) {
    const auto& graph = prepared.graph();
    SSSPResult result;
    result.dist.assign(graph.n, INF_DIST);
    result.pred.assign(graph.n, NO_PRED);
    if (source >= graph.n) return result;
    std::vector<unsigned char> settled(graph.n, 0);
    result.dist[source] = 0;
    for (;;) {
        Candidate best;
        for (uint32_t v = 0; v < graph.n; ++v) {
            if (!settled[v] && result.dist[v] < INF_DIST && better(result.dist[v], v, best)) {
                best.distance = result.dist[v];
                best.vertex = v;
            }
        }
        if (best.vertex == NO_PRED) break;
        const uint32_t u = best.vertex;
        settled[u] = 1;
        for (auto e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
            const auto v = graph.col_idx[e];
            const double next = best.distance + graph.weight[e];
            if (!settled[v] && next < result.dist[v]) {
                result.dist[v] = next;
                result.pred[v] = u;
            }
        }
    }
    return result;
}

SSSPResult dijkstra_scan_sequential(const CSRGraph& graph, uint32_t source) {
    return dijkstra_scan_prepared(PreparedDijkstraGraph(graph), source);
}

SSSPResult dijkstra_openmp(const CSRGraph& graph, uint32_t source, int threads) {
    if (threads < 0) throw std::invalid_argument("Dijkstra: negative thread count");
    return dijkstra_openmp_prepared(PreparedDijkstraGraph(graph), source, threads);
}

SSSPResult dijkstra_openmp_prepared(const PreparedDijkstraGraph& prepared,
                                    uint32_t source, int threads) {
    if (threads < 0) throw std::invalid_argument("Dijkstra: negative thread count");
    const auto& graph = prepared.graph();
    SSSPResult result;
    result.dist.assign(graph.n, INF_DIST);
    result.pred.assign(graph.n, NO_PRED);
    if (source >= graph.n) return result;

    const int requested = threads == 0 ? omp_get_max_threads() : threads;
    std::vector<Candidate> candidates(static_cast<size_t>(requested));
    // Avoid vector<bool>: different vertices must occupy distinct memory locations.
    std::vector<unsigned char> settled(graph.n, 0);
    result.dist[source] = 0;
    uint32_t chosen = NO_PRED;
    double chosen_distance = INF_DIST;
    const int64_t n = graph.n;

    #pragma omp parallel num_threads(requested) default(none) \
        shared(result, settled, candidates, chosen, chosen_distance, graph, n)
    {
        const int tid = omp_get_thread_num();
        for (;;) {
            Candidate local;
            #pragma omp for schedule(static) nowait
            for (int64_t v = 0; v < n; ++v) {
                if (!settled[v] && result.dist[v] < INF_DIST &&
                    better(result.dist[v], static_cast<uint32_t>(v), local)) {
                    local.distance = result.dist[v];
                    local.vertex = static_cast<uint32_t>(v);
                }
            }
            candidates[tid] = local;
            // Publish ALL thread-local minima before single reads the slots.
            #pragma omp barrier
            #pragma omp single
            {
                Candidate best;
                // Runtime can create fewer threads (dynamic/nested/thread limit).
                for (int t = 0; t < omp_get_num_threads(); ++t) {
                    if (better(candidates[t].distance, candidates[t].vertex, best))
                        best = candidates[t];
                }
                chosen = best.vertex;
                chosen_distance = best.distance;
                if (chosen != NO_PRED) settled[chosen] = 1;
            } // implicit barrier publishes chosen + settled to every thread
            if (chosen == NO_PRED) break; // collective termination, including disconnected graphs

            const uint32_t u = chosen;
            const double d = chosen_distance;
            #pragma omp for schedule(static)
            for (int64_t e = static_cast<int64_t>(graph.row_ptr[u]);
                 e < static_cast<int64_t>(graph.row_ptr[u + 1]); ++e) {
                const auto v = graph.col_idx[e];
                const double w = graph.weight[e];
                if (!settled[v]) {
                    const double next = d + w;
                    if (next < result.dist[v]) {
                        result.dist[v] = next;
                        result.pred[v] = u;
                    }
                }
            } // implicit barrier: all dist/pred writes precede next minimum scan
        }
    }
    return result;
}
} // namespace bmssp
