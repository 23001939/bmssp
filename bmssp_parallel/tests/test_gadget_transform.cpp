#include "baseline/bmssp_seq.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using bmssp::CSRGraph;
using bmssp::Edge;

bool verify_degree(const CSRGraph& graph) {
    std::vector<uint32_t> in_degree(graph.n, 0);
    for (uint32_t u = 0; u < graph.n; ++u) {
        if (graph.degree(u) > 2) {
            std::cerr << "[FAIL] gadget out-degree vertex=" << u << " degree=" << graph.degree(u) << '\n';
            return false;
        }
        for (uint64_t edge = graph.edge_begin(u); edge < graph.edge_end(u); ++edge) {
            const uint32_t v = graph.edge_dest(edge);
            if (v >= graph.n) {
                std::cerr << "[FAIL] gadget edge source=" << u << " invalid_destination=" << v << '\n';
                return false;
            }
            ++in_degree[v];
        }
    }
    for (uint32_t v = 0; v < graph.n; ++v) {
        if (in_degree[v] > 2) {
            std::cerr << "[FAIL] gadget in-degree vertex=" << v << " degree=" << in_degree[v] << '\n';
            return false;
        }
    }
    return true;
}

bool verify_mapping(uint32_t vertex_count, const bmssp::TransformedGraphInfo& transformed) {
    if (transformed.original_n != vertex_count || transformed.orig_to_transformed_repr.size() != vertex_count ||
        transformed.transformed_to_orig.size() != transformed.graph.n) {
        std::cerr << "[FAIL] gadget mapping sizes expected_original=" << vertex_count
                  << " declared_original=" << transformed.original_n
                  << " representatives=" << transformed.orig_to_transformed_repr.size()
                  << " transformed=" << transformed.graph.n
                  << " reverse_map=" << transformed.transformed_to_orig.size() << '\n';
        return false;
    }
    for (uint32_t v = 0; v < vertex_count; ++v) {
        const uint32_t rep = transformed.orig_to_transformed_repr[v];
        if (rep >= transformed.graph.n || transformed.transformed_to_orig[rep] != v) {
            std::cerr << "[FAIL] gadget mapping vertex=" << v << " representative=" << rep << '\n';
            return false;
        }
    }
    for (uint32_t v = 0; v < transformed.graph.n; ++v) {
        if (transformed.transformed_to_orig[v] >= vertex_count) {
            std::cerr << "[FAIL] gadget reverse mapping vertex=" << v
                      << " original=" << transformed.transformed_to_orig[v] << '\n';
            return false;
        }
    }
    return true;
}

bool verify_distances(const CSRGraph& original, const bmssp::TransformedGraphInfo& transformed,
                      uint32_t source) {
    const uint32_t transformed_source = transformed.orig_to_transformed_repr[source];
    const auto expected = bmssp::dijkstra_sequential(original, source);
    const auto result = bmssp::dijkstra_sequential(transformed.graph, transformed_source);
    std::vector<double> restored;
    bmssp::restore_distances(result.dist, restored, transformed);

    if (restored.size() != original.n) {
        std::cerr << "[FAIL] gadget source=" << source << " expected_vertices=" << original.n
                  << " restored_vertices=" << restored.size() << '\n';
        return false;
    }
    for (uint32_t v = 0; v < original.n; ++v) {
        if (restored[v] != expected.dist[v]) {
            std::cerr << "[FAIL] gadget distances source=" << source << " vertex=" << v
                      << " expected=" << expected.dist[v] << " actual=" << restored[v] << '\n';
            return false;
        }
    }
    std::cout << "[PASS] gadget restored distances source=" << source << '\n';
    return true;
}

} // namespace

int main() {
    constexpr uint32_t isolated_source = 12;
    std::vector<Edge> edges;
    for (uint32_t v = 2; v < isolated_source; ++v) {
        edges.push_back({0, v, static_cast<double>(v)});
        edges.push_back({v, 1, 0.0});
    }
    edges.push_back({1, 0, 0.0});
    edges.push_back({0, 2, 1.0});

    const auto original = CSRGraph::from_edge_list(isolated_source + 1, edges);
    const auto transformed = bmssp::transform_to_constant_degree(original);
    if (!verify_degree(transformed.graph) || !verify_mapping(original.n, transformed)) {
        return EXIT_FAILURE;
    }
    std::cout << "[PASS] gadget mapping is valid and in-degree and out-degree are at most 2\n";

    const bool high_degree = verify_distances(original, transformed, 0);
    const bool isolated = verify_distances(original, transformed, isolated_source);
    return high_degree && isolated ? EXIT_SUCCESS : EXIT_FAILURE;
}
