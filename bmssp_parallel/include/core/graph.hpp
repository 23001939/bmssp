#pragma once

#include "types.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <algorithm>

namespace bmssp {

struct Edge {
    uint32_t u;
    uint32_t v;
    double w;
};

/**
 * @brief Compressed Sparse Row (CSR) directed graph representation.
 * Optimized for CPU cache locality and multi-threaded edge sweeps.
 */
class CSRGraph {
public:
    uint32_t n{0};
    uint64_t m{0};
    std::vector<uint64_t> row_ptr;  // Size n + 1
    std::vector<uint32_t> col_idx;  // Size m (destination v)
    std::vector<double> weight;     // Size m (edge weight w >= 0)

    CSRGraph() = default;
    CSRGraph(uint32_t num_vertices, uint64_t num_edges)
        : n(num_vertices), m(num_edges), row_ptr(num_vertices + 1, 0), col_idx(num_edges), weight(num_edges) {}

    [[nodiscard]] inline uint32_t degree(uint32_t u) const noexcept {
        return static_cast<uint32_t>(row_ptr[u + 1] - row_ptr[u]);
    }

    [[nodiscard]] inline uint64_t edge_begin(uint32_t u) const noexcept {
        return row_ptr[u];
    }

    [[nodiscard]] inline uint64_t edge_end(uint32_t u) const noexcept {
        return row_ptr[u + 1];
    }

    [[nodiscard]] inline uint32_t edge_dest(uint64_t e_idx) const noexcept {
        return col_idx[e_idx];
    }

    [[nodiscard]] inline double edge_weight(uint64_t e_idx) const noexcept {
        return weight[e_idx];
    }

    [[nodiscard]] uint32_t max_out_degree() const noexcept {
        uint32_t max_d = 0;
        for (uint32_t u = 0; u < n; ++u) {
            max_d = std::max(max_d, degree(u));
        }
        return max_d;
    }

    /**
     * @brief Constructs a CSR graph from an edge list.
     */
    static CSRGraph from_edge_list(uint32_t num_vertices, const std::vector<Edge>& edges);

    /**
     * @brief Parses graph from DIMACS / text edge list format (n m, followed by u v w lines).
     */
    static CSRGraph from_file(const std::string& filepath);
};

/**
 * @brief Information package from Gadget Constant-Degree Transformation.
 */
struct TransformedGraphInfo {
    CSRGraph graph;                                 // Transformed graph with max out/in degree <= 2
    uint32_t original_n{0};                         // Original vertex count
    std::vector<uint32_t> orig_to_transformed_repr; // Maps original vertex u -> representative transformed vertex
    std::vector<uint32_t> transformed_to_orig;      // Maps transformed vertex v' -> original vertex u
};

/**
 * @brief Transforms an arbitrary directed graph into a constant-degree graph (max degree <= 2)
 * using zero-weight directed cycle gadgets (Section 2 of arXiv:2504.17033v2).
 */
TransformedGraphInfo transform_to_constant_degree(const CSRGraph& original);

/**
 * @brief Restores shortest path distances on original vertices from transformed graph distances.
 */
void restore_distances(const std::vector<double>& transformed_dist,
                       std::vector<double>& orig_dist,
                       const TransformedGraphInfo& info);

} // namespace bmssp
