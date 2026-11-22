#pragma once

#include "core/graph.hpp"
#include "core/types.hpp"
#include "baseline/bmssp_seq.hpp"
#include <cstddef>
#include <cstdint>

namespace bmssp {

/**
 * @brief Frontier priority queue backend used inside BMSSP recursion levels.
 */
enum class FrontierType : uint8_t {
    Concurrent = 0,
    Mutex = 1
};

struct BMSSPOptions {
    size_t granularity_cutoff = DEFAULT_GRANULARITY_CUTOFF;
    size_t frontier_batch_cap = 0; // Zero uses the original per-level batch size.
};

struct BMSSPTimings {
    double transform_ms = 0.0;
    double solve_ms = 0.0;
    double restore_ms = 0.0;
};

/**
 * @brief Multi-threaded parallel implementation of the Duan et al. (STOC 2025)
 * BMSSP algorithm for sparse directed graphs with non-negative weights.
 *
 * Features:
 * - Distance-sliced data parallelism across OpenMP threads.
 * - Hardware lock-free 128-bit atomic relaxations (cmpxchg16b).
 * - Selectable Batch Priority Queue (Lemma 3.3): ConcurrentFrontierD (B-Link skip-list
 *   with D_0 deque and lazy deletion) or MutexFrontierD.
 * - Granularity cutoff: automatically switches to serial mode when active frontier < 2048 edges.
 * - Optional Constant-Degree Gadget Transformation with valid predecessor reconstruction.
 *
 * @param graph CSR input graph.
 * @param source Source vertex ID.
 * @param num_threads Number of worker threads (0 = keep current / auto-detect OpenMP default).
 * @param use_gadget_transform Enable constant-degree gadget transformation (max out-degree <= 2).
 * @param frontier_type Frontier data structure backend (Concurrent or Mutex).
 * @return SSSPResult containing exact shortest path distances and predecessor tree.
 */
SSSPResult bmssp_parallel(
    const CSRGraph& graph,
    uint32_t source,
    size_t num_threads = 0,
    bool use_gadget_transform = false,
    FrontierType frontier_type = FrontierType::Concurrent,
    const BMSSPOptions& options = {},
    BMSSPTimings* timings = nullptr
);

} // namespace bmssp
