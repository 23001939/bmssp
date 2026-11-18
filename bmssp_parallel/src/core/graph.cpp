#include "core/graph.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

namespace bmssp {

CSRGraph CSRGraph::from_edge_list(uint32_t num_vertices, const std::vector<Edge>& edges) {
    CSRGraph g(num_vertices, edges.size());

    // 1. Count degrees
    std::vector<uint64_t> counts(num_vertices, 0);
    for (const auto& e : edges) {
        if (e.u < num_vertices) {
            counts[e.u]++;
        }
    }

    // 2. Compute row_ptr via prefix sum
    g.row_ptr[0] = 0;
    for (uint32_t i = 0; i < num_vertices; ++i) {
        g.row_ptr[i + 1] = g.row_ptr[i] + counts[i];
    }

    // 3. Fill edges using working copy of row_ptr
    std::vector<uint64_t> curr_pos = g.row_ptr;
    for (const auto& e : edges) {
        if (e.u < num_vertices) {
            uint64_t idx = curr_pos[e.u]++;
            g.col_idx[idx] = e.v;
            g.weight[idx] = e.w;
        }
    }

    return g;
}

CSRGraph CSRGraph::from_file(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open graph file: " + filepath);
    }

    std::string line;
    // Skip empty lines and comment lines starting with '#'
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        break;
    }

    std::stringstream ss(line);
    uint32_t n = 0;
    uint64_t m = 0;
    if (!(ss >> n >> m)) {
        throw std::runtime_error("Invalid graph header: expected 'n m'");
    }

    std::vector<Edge> edges;
    edges.reserve(m);
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream edge_ss(line);
        uint32_t u = 0, v = 0;
        double w = 0.0;
        if (edge_ss >> u >> v >> w) {
            edges.push_back({u, v, w});
        }
    }

    return from_edge_list(n, edges);
}

TransformedGraphInfo transform_to_constant_degree(const CSRGraph& original) {
    TransformedGraphInfo info;
    info.original_n = original.n;
    info.orig_to_transformed_repr.resize(original.n);

    // Compute in-degrees and out-degrees in original graph
    std::vector<uint32_t> in_deg(original.n, 0);
    std::vector<uint32_t> out_deg(original.n, 0);
    for (uint32_t u = 0; u < original.n; ++u) {
        out_deg[u] = original.degree(u);
        for (uint64_t e = original.edge_begin(u); e < original.edge_end(u); ++e) {
            uint32_t v = original.edge_dest(e);
            if (v < original.n) in_deg[v]++;
        }
    }

    // Allocate virtual vertices for each original vertex
    // For vertex u:
    // in_slots: max(in_deg[u], 1)
    // out_slots: max(out_deg[u], 1)
    // If in_deg[u] <= 1 and out_deg[u] <= 1, keep 1 vertex.
    std::vector<uint32_t> in_base(original.n);
    std::vector<uint32_t> out_base(original.n);

    uint32_t current_id = 0;
    for (uint32_t u = 0; u < original.n; ++u) {
        if (in_deg[u] <= 1 && out_deg[u] <= 1) {
            in_base[u] = current_id;
            out_base[u] = current_id;
            info.orig_to_transformed_repr[u] = current_id;
            info.transformed_to_orig.push_back(u);
            current_id++;
        } else {
            uint32_t n_in = std::max(in_deg[u], 1U);
            uint32_t n_out = std::max(out_deg[u], 1U);

            in_base[u] = current_id;
            for (uint32_t i = 0; i < n_in; ++i) {
                info.transformed_to_orig.push_back(u);
            }
            current_id += n_in;

            out_base[u] = current_id;
            for (uint32_t j = 0; j < n_out; ++j) {
                info.transformed_to_orig.push_back(u);
            }
            current_id += n_out;

            info.orig_to_transformed_repr[u] = out_base[u];
        }
    }

    uint32_t transformed_n = current_id;
    std::vector<Edge> new_edges;
    new_edges.reserve(original.m + transformed_n * 2);

    // Track which in_slot / out_slot has been connected
    std::vector<uint32_t> curr_in_slot(original.n, 0);
    std::vector<uint32_t> curr_out_slot(original.n, 0);

    // 1. Build zero-weight cycle edges within each gadget
    for (uint32_t u = 0; u < original.n; ++u) {
        if (in_deg[u] <= 1 && out_deg[u] <= 1) {
            continue; // single vertex, no internal cycle
        }
        uint32_t n_in = std::max(in_deg[u], 1U);
        uint32_t n_out = std::max(out_deg[u], 1U);
        uint32_t total = n_in + n_out;

        // Cycle through all vertices in gadget:
        // in_base[u] + 0 -> in_base[u] + 1 -> ... -> out_base[u] + 0 -> ... -> in_base[u] + 0
        for (uint32_t k = 0; k < total; ++k) {
            uint32_t src = in_base[u] + k;
            uint32_t dst = in_base[u] + ((k + 1) % total);
            new_edges.push_back({src, dst, 0.0});
        }
    }

    // 2. Map original edges (u, v, w) to gadget vertices
    for (uint32_t u = 0; u < original.n; ++u) {
        for (uint64_t e = original.edge_begin(u); e < original.edge_end(u); ++e) {
            uint32_t v = original.edge_dest(e);
            double w = original.edge_weight(e);

            uint32_t src_node = (in_deg[u] <= 1 && out_deg[u] <= 1)
                                ? in_base[u]
                                : (out_base[u] + curr_out_slot[u]++);
            uint32_t dst_node = (in_deg[v] <= 1 && out_deg[v] <= 1)
                                ? in_base[v]
                                : (in_base[v] + curr_in_slot[v]++);

            new_edges.push_back({src_node, dst_node, w});
        }
    }

    info.graph = CSRGraph::from_edge_list(transformed_n, new_edges);
    return info;
}

void restore_distances(const std::vector<double>& transformed_dist,
                       std::vector<double>& orig_dist,
                       const TransformedGraphInfo& info) {
    orig_dist.assign(info.original_n, INF_DIST);
    for (size_t v_prime = 0; v_prime < transformed_dist.size(); ++v_prime) {
        if (v_prime >= info.transformed_to_orig.size()) break;
        uint32_t orig_u = info.transformed_to_orig[v_prime];
        double d = transformed_dist[v_prime];
        if (d < orig_dist[orig_u]) {
            orig_dist[orig_u] = d;
        }
    }
}

} // namespace bmssp
