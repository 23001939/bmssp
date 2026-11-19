#include "algorithm/base_case.hpp"
#include <queue>

namespace bmssp {

BaseCaseResult base_case(
    const CSRGraph& graph,
    double B,
    uint32_t source,
    size_t k,
    AtomicVertexState* states
) {
    BaseCaseResult result;
    result.new_bound = B;

    if (source >= graph.n) return result;

    using Element = std::pair<double, uint32_t>;
    std::priority_queue<Element, std::vector<Element>, std::greater<Element>> heap;

    double d_src = states[source].load(std::memory_order_relaxed).dist;
    // Source vertex is the starting point of the interval, always enqueue it
    heap.emplace(d_src, source);

    std::vector<uint32_t> complete;
    complete.reserve(k + 1);

    while (!heap.empty() && complete.size() < k + 1) {
        auto [d, u] = heap.top();
        heap.pop();

        double curr_d = states[u].load(std::memory_order_relaxed).dist;
        if (d > curr_d) continue;

        complete.push_back(u);

        for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
            uint32_t v = graph.edge_dest(e);
            double w = graph.edge_weight(e);
            double new_dist = d + w;

            double curr_v = states[v].load(std::memory_order_relaxed).dist;
            if (new_dist <= curr_v && new_dist < B) {
                if (new_dist < curr_v) {
                    relax_edge_atomic(states[v], new_dist, u, 0);
                }
                heap.emplace(new_dist, v);
            }
        }
    }

    if (complete.size() <= k) {
        result.new_bound = B;
        result.completed = std::move(complete);
    } else {
        // We pulled exactly k + 1 vertices: the (k+1)-th vertex distance defines the new bound
        uint32_t last_v = complete.back();
        result.new_bound = states[last_v].load(std::memory_order_relaxed).dist;
        complete.pop_back();
        result.completed = std::move(complete);
    }

    return result;
}

} // namespace bmssp
