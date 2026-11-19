#pragma once

#include "core/graph.hpp"
#include "core/types.hpp"
#include "baseline/bmssp_seq.hpp"
#include <vector>

namespace bmssp {

/**
 * @brief Multi-threaded parallel implementation of the Duan et al. (STOC 2025)
 * BMSSP algorithm for sparse directed graphs with non-negative weights.
 * 
 * Features:
 * - Distance-sliced data parallelism across OpenMP threads.
 * - Hardware lock-free 128-bit atomic relaxations (cmpxchg16b).
 * - Concurrent Batch Priority Queue (Lemma 3.3) with fine-grained spinlocks and lazy deletion.
 * - Granularity cutoff: automatically switches to serial mode when active frontier < 2048 edges.
 * - Optional Constant-Degree Gadget Transformation.
 * 
 * @param graph CSR input graph.
 * @param source Source vertex ID.
 * @param num_threads Number of worker threads (0 = auto-detect hardware concurrency).
 * @param use_gadget_transform Enable constant-degree gadget transformation (max out-degree <= 2).
 * @return SSSPResult containing exact shortest path distances and predecessor tree.
 */
SSSPResult bmssp_parallel(
    const CSRGraph& graph,
    uint32_t source,
    size_t num_threads = 0,
    bool use_gadget_transform = false
);

} // namespace bmssp
