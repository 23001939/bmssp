#pragma once

#include "core/graph.hpp"
#include "core/types.hpp"
#include "core/primitives.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace bmssp {

struct PivotResult {
    std::vector<uint32_t> pivots;      // Set P of pivot vertices (|P| <= |vis| / k)
    std::vector<uint32_t> visited;     // Set vis of all vertices visited during Bellman-Ford steps
};

/**
 * @brief Reusable scratch workspace for find_pivots_parallel to avoid per-call O(n) allocations.
 */
struct FindPivotsWorkspace {
    // Bit 0x01: visited in current find_pivots call; Bit 0x02: member of unique_S.
    // Cleared in O(|vis|) on exit from find_pivots_parallel.
    std::vector<uint8_t> vertex_flags;
    size_t forest_cap{0};
    std::unique_ptr<uint32_t[]> parent_in_forest; // allocated lazily for overwrite, touched on vis
    std::unique_ptr<uint32_t[]> root_of;          // allocated lazily for overwrite, touched on vis
    std::unique_ptr<uint32_t[]> subtree_cnt;      // allocated lazily for overwrite, touched on unique_S

    std::vector<std::vector<uint32_t>> thread_buffers;
    std::vector<uint64_t> edge_offsets;
    std::vector<uint32_t> unique_S_buf;
    std::vector<uint32_t> active_buf;
    std::vector<uint32_t> next_active_buf;
    std::vector<size_t> gather_offsets;
    std::vector<uint32_t> path_buf;

    explicit FindPivotsWorkspace(size_t n = 0);
    void ensure_size(size_t n);
    void ensure_forest_arrays(size_t n);
};

/**
 * @brief Algorithm 1: Parallel FindPivots (arXiv:2504.17033v2).
 */
PivotResult find_pivots_parallel(
    const CSRGraph& graph,
    double B,
    std::span<const uint32_t> S,
    size_t k,
    AtomicVertexState* states,
    EpochDeduplicator& deduplicator,
    FindPivotsWorkspace* workspace = nullptr
);

} // namespace bmssp
