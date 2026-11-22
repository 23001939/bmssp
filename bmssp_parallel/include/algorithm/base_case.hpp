#pragma once

#include "core/graph.hpp"
#include "core/types.hpp"
#include <vector>
#include <utility>

namespace bmssp {

struct BaseCaseResult {
    double new_bound{INF_DIST};
    std::vector<uint32_t> completed;
};

/**
 * @brief Algorithm 2: BaseCase for recursion level l = 0 (arXiv:2504.17033v2).
 * 
 * Runs a local Dijkstra search bounded by distance B and size cutoff k + 1.
 * 
 * @param graph CSR graph representation.
 * @param B Distance upper bound.
 * @param source Starting source vertex.
 * @param k Maximum number of completed vertices to extract.
 * @param states Global array of atomic vertex states.
 * @return BaseCaseResult containing the updated bound and completed vertices.
 */
BaseCaseResult base_case(
    const CSRGraph& graph,
    double B,
    uint32_t source,
    size_t k,
    AtomicVertexState* states
);

} // namespace bmssp
