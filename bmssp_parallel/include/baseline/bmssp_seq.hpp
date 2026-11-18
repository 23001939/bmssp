#pragma once

#include "core/graph.hpp"
#include <vector>

namespace bmssp {

struct SSSPResult {
    std::vector<double> dist;
    std::vector<uint32_t> pred;
};

/**
 * @brief Reference sequential Dijkstra algorithm with binary min-heap.
 * Serves as the independent verification oracle.
 */
SSSPResult dijkstra_sequential(const CSRGraph& graph, uint32_t source);

/**
 * @brief Reference single-threaded BMSSP algorithm biding strictly to
 * Algorithms 1, 2, and 3 of arXiv:2504.17033v2.
 */
SSSPResult bmssp_sequential(const CSRGraph& graph, uint32_t source, double upper_bound = INF_DIST);

} // namespace bmssp
