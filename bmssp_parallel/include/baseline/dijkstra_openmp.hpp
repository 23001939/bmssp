#pragma once

#include "baseline/bmssp_seq.hpp"

namespace bmssp {

// Validated, normalized, owning snapshot. Every outgoing row has unique
// destinations. Reuse across solves/sources; construction is timed separately.
class PreparedDijkstraGraph {
public:
    explicit PreparedDijkstraGraph(const CSRGraph& graph);
    [[nodiscard]] const CSRGraph& graph() const noexcept { return graph_; }
private:
    CSRGraph graph_;
};

// Pure serial scan, with no OpenMP region/directive in the solve path.
SSSPResult dijkstra_scan_sequential(const CSRGraph& graph, uint32_t source);
// Prepared solves exclude validation/normalization, but INCLUDE result/workspace
// allocation and initialization (and team startup for OpenMP).
SSSPResult dijkstra_scan_prepared(const PreparedDijkstraGraph& graph, uint32_t source);
SSSPResult dijkstra_openmp_prepared(const PreparedDijkstraGraph& graph, uint32_t source,
                                    int threads = 0);

/** Scan-based OpenMP Dijkstra on the existing directed CSR graph.
 * threads=0 uses the OpenMP runtime default; negative counts are rejected.
 * Like dijkstra_sequential, an invalid source returns INF/NO_PRED arrays.
 * Rejects malformed CSR and negative/nonfinite weights before starting threads.
 * Parallel edges are coalesced by minimum weight so each relaxation has one
 * writer per destination. Equal distances select the smallest vertex ID.
 * Finite binary64 distances only: a sum overflowing double remains unreachable.
 * Setup/normalization is part of this call. No global runtime settings change.
 * See DIJKSTRA_OPENMP_REVIEW.md for provenance and synchronization analysis.
 */
SSSPResult dijkstra_openmp(const CSRGraph& graph, uint32_t source, int threads = 0);

} // namespace bmssp
