#pragma once

#include "core/graph.hpp"
#include "core/types.hpp"
#include "core/primitives.hpp"
#include <vector>
#include <span>

namespace bmssp {

struct PivotResult {
    std::vector<uint32_t> pivots;      // Set P of pivot vertices (|P| <= |vis| / k)
    std::vector<uint32_t> visited;     // Set vis of all vertices visited during Bellman-Ford steps
};

/**
 * @brief Algorithm 1: Parallel FindPivots (arXiv:2504.17033v2).
 * 
 * Performs k steps of parallel edge relaxations starting from active sources S.
 * Restricts expansion to paths with total distance < B.
 * Identifies high-degree subtree roots (pivots) to reduce source set size.
 * 
 * @param graph CSR graph representation.
 * @param B Distance upper bound.
 * @param S Initial active source set.
 * @param k Bound on Bellman-Ford steps (floor(log^(1/3) n)).
 * @param states Global array of atomic vertex states.
 * @param deduplicator Thread-safe epoch deduplicator.
 * @return PivotResult containing pivots P and visited vertices vis.
 */
PivotResult find_pivots_parallel(
    const CSRGraph& graph,
    double B,
    std::span<const uint32_t> S,
    size_t k,
    AtomicVertexState* states,
    EpochDeduplicator& deduplicator
);

} // namespace bmssp
