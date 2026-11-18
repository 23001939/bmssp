#include "baseline/bmssp_seq.hpp"
#include "baseline/upstream/include/bmssp-worst-case.hpp"
#include <queue>
#include <cmath>

namespace bmssp {

SSSPResult dijkstra_sequential(const CSRGraph& graph, uint32_t source) {
    SSSPResult res;
    res.dist.assign(graph.n, INF_DIST);
    res.pred.assign(graph.n, NO_PRED);

    if (source >= graph.n) return res;

    using Element = std::pair<double, uint32_t>;
    std::priority_queue<Element, std::vector<Element>, std::greater<Element>> pq;

    res.dist[source] = 0.0;
    pq.emplace(0.0, source);

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (d > res.dist[u]) continue;

        for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
            uint32_t v = graph.edge_dest(e);
            double w = graph.edge_weight(e);
            double new_dist = d + w;

            if (new_dist < res.dist[v]) {
                res.dist[v] = new_dist;
                res.pred[v] = u;
                pq.emplace(new_dist, v);
            }
        }
    }

    return res;
}

SSSPResult bmssp_sequential(const CSRGraph& graph, uint32_t source, [[maybe_unused]] double upper_bound) {
    SSSPResult res;
    res.dist.assign(graph.n, INF_DIST);
    res.pred.assign(graph.n, NO_PRED);

    if (graph.n == 0 || source >= graph.n) {
        return res;
    }

    if (graph.n == 1) {
        res.dist[0] = 0.0;
        return res;
    }

    spp::bmssp<double> solver(static_cast<int>(graph.n));

    for (uint32_t u = 0; u < graph.n; ++u) {
        for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
            uint32_t v = graph.edge_dest(e);
            double w = graph.edge_weight(e);
            solver.addEdge(u, v, w);
        }
    }

    solver.prepare_graph(false);
    auto [raw_dist, raw_pred] = solver.execute(source);

    const double inf_threshold = 1e290;
    for (uint32_t i = 0; i < graph.n; ++i) {
        if (i < raw_dist.size() && std::isfinite(raw_dist[i]) && raw_dist[i] < inf_threshold) {
            res.dist[i] = raw_dist[i];
            res.pred[i] = (i < raw_pred.size() && raw_pred[i] >= 0) ? static_cast<uint32_t>(raw_pred[i]) : NO_PRED;
        } else {
            res.dist[i] = INF_DIST;
            res.pred[i] = NO_PRED;
        }
    }

    return res;
}

} // namespace bmssp
