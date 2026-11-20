#include "baseline/bmssp_seq.hpp"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bmssp::CSRGraph;
using bmssp::Edge;
using bmssp::INF_DIST;
using bmssp::NO_PRED;
using bmssp::SSSPResult;

bool verify_distances(std::string_view label, std::span<const double> expected,
                      std::span<const double> actual) {
    if (expected.size() != actual.size()) {
        std::cerr << "[FAIL] " << label << " expected_vertices=" << expected.size()
                  << " actual_vertices=" << actual.size() << '\n';
        return false;
    }
    for (size_t v = 0; v < expected.size(); ++v) {
        if (actual[v] != expected[v]) {
            std::cerr << std::setprecision(17) << "[FAIL] " << label << " vertex=" << v
                      << " expected=" << expected[v] << " actual=" << actual[v] << '\n';
            return false;
        }
    }
    return true;
}

bool has_tight_edge(const CSRGraph& graph, const SSSPResult& result, uint32_t u, uint32_t v) {
    for (uint64_t edge = graph.edge_begin(u); edge < graph.edge_end(u); ++edge) {
        if (graph.edge_dest(edge) == v && result.dist[u] + graph.edge_weight(edge) == result.dist[v]) {
            return true;
        }
    }
    return false;
}

bool verify_predecessors(std::string_view label, const CSRGraph& graph, uint32_t source,
                         const SSSPResult& result) {
    if (result.pred.size() != graph.n || result.pred[source] != NO_PRED) {
        std::cerr << "[FAIL] " << label << " invalid predecessor list or source predecessor\n";
        return false;
    }

    for (uint32_t v = 0; v < graph.n; ++v) {
        if (result.dist[v] == INF_DIST) {
            if (result.pred[v] != NO_PRED) {
                std::cerr << "[FAIL] " << label << " unreachable vertex=" << v << " has a predecessor\n";
                return false;
            }
            continue;
        }

        uint32_t current = v;
        for (uint32_t steps = 0; current != source; ++steps) {
            const uint32_t parent = result.pred[current];
            if (steps >= graph.n || parent >= graph.n || !std::isfinite(result.dist[parent]) ||
                !has_tight_edge(graph, result, parent, current)) {
                std::cerr << "[FAIL] " << label << " invalid path vertex=" << v
                          << " current=" << current << " parent=" << parent << '\n';
                return false;
            }
            current = parent;
        }
    }
    return true;
}

bool verify_oracle(const std::string& name, const CSRGraph& graph, uint32_t source,
                   std::span<const double> expected) {
    const std::string label = name + " source=" + std::to_string(source);
    const auto oracle = bmssp::dijkstra_sequential(graph, source);
    if (!verify_distances(label, expected, oracle.dist) || !verify_predecessors(label, graph, source, oracle)) {
        return false;
    }
    std::cout << "[PASS] " << label << " vertices=" << graph.n << " edges=" << graph.m << '\n';
    return true;
}

// Independent reference for small generated graphs. The generated weights are exact integers.
std::vector<double> bellman_ford(uint32_t vertex_count, std::span<const Edge> edges, uint32_t source) {
    std::vector<double> distances(vertex_count, INF_DIST);
    distances[source] = 0.0;
    for (uint32_t pass = 1; pass < vertex_count; ++pass) {
        bool changed = false;
        for (const Edge& edge : edges) {
            const double candidate = distances[edge.u] + edge.w;
            if (candidate < distances[edge.v]) {
                distances[edge.v] = candidate;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    return distances;
}

bool verify_known_cases() {
    struct Case {
        std::string name;
        std::vector<Edge> edges;
        std::vector<double> expected;
    };
    const Case cases[] = {
        {"single vertex", {}, {0.0}},
        {"disconnected", {{0, 1, 3.0}, {2, 3, 2.0}}, {0.0, 3.0, INF_DIST, INF_DIST}},
        {"zero-weight cycle and tied paths",
         {{0, 1, 0.0}, {0, 2, 1.0}, {1, 2, 1.0}, {1, 0, 0.0}, {2, 3, 2.0}},
         {0.0, 0.0, 1.0, 3.0}},
        {"near-tie", {{0, 1, 4e-11}, {0, 2, 0.0}, {2, 1, 0.0}}, {0.0, 0.0, 0.0}},
        {"high-degree source and sink",
         {{0, 1, 5.0}, {0, 2, 2.0}, {0, 3, 3.0}, {0, 4, 4.0}, {0, 5, 9.0},
          {1, 5, 1.0}, {2, 5, 2.0}, {3, 5, 1.0}, {4, 5, 0.0}},
         {0.0, 5.0, 2.0, 3.0, 4.0, 4.0}},
    };

    bool passed = true;
    for (const Case& test : cases) {
        const auto graph = CSRGraph::from_edge_list(static_cast<uint32_t>(test.expected.size()), test.edges);
        passed &= verify_oracle(test.name, graph, 0, test.expected);
    }
    return passed;
}

bool verify_generated_cases() {
    constexpr uint32_t vertices = 48;
    constexpr uint32_t component_size = vertices / 2;
    bool passed = true;
    for (uint32_t density : {8u, 70u}) {
        for (uint32_t seed : {20261118u, 20261119u, 20261120u}) {
            std::mt19937 random(seed);
            std::vector<Edge> edges;
            for (uint32_t u = 0; u < vertices; ++u) {
                const uint32_t component_start = (u / component_size) * component_size;
                for (uint32_t v = component_start; v < component_start + component_size; ++v) {
                    if (u != v && random() % 100 < density) {
                        edges.push_back({u, v, static_cast<double>(random() % 21)});
                    }
                }
            }
            const auto graph = CSRGraph::from_edge_list(vertices, edges);
            const std::string label = "generated density=" + std::to_string(density) +
                                      "% seed=" + std::to_string(seed);
            for (uint32_t source : {0u, component_size}) {
                const auto expected = bellman_ford(vertices, edges, source);
                passed &= verify_oracle(label, graph, source, expected);
            }
        }
    }
    return passed;
}

} // namespace

int main() {
    const bool known_cases = verify_known_cases();
    const bool generated_cases = verify_generated_cases();
    return known_cases && generated_cases ? EXIT_SUCCESS : EXIT_FAILURE;
}
