#include "algorithm/bmssp.hpp"
#include "algorithm/find_pivots.hpp"
#include "core/frontier_interface.hpp"
#include "core/profiler.hpp"
#include "data_structure/concurrent_frontier_d.hpp"
#include "edge_relaxation.hpp"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <omp.h>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bmssp::CSRGraph;
using bmssp::Edge;
using bmssp::FrontierType;
using bmssp::INF_DIST;
using bmssp::NO_PRED;
using bmssp::SSSPResult;

bool verify_distances(std::string_view label, std::span<const double> expected,
                      std::span<const double> actual, double tolerance = 0.0) {
    if (expected.size() != actual.size()) {
        std::cerr << "[FAIL] " << label << " expected_vertices=" << expected.size()
                  << " actual_vertices=" << actual.size() << '\n';
        return false;
    }
    for (size_t v = 0; v < expected.size(); ++v) {
        if (actual[v] != expected[v] && !(std::abs(actual[v] - expected[v]) < tolerance)) {
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
                   std::span<const double> expected, FrontierType backend, bool exact = false) {
    const std::string label = name + " source=" + std::to_string(source);
    const auto oracle = bmssp::dijkstra_sequential(graph, source);
    if (!verify_distances(label, expected, oracle.dist) || !verify_predecessors(label, graph, source, oracle)) {
        return false;
    }
    bool passed = true;
    const char* frontier_name = backend == FrontierType::Mutex ? "Mutex" : "Concurrent";
    for (size_t threads : {1u, 2u, 4u, 8u}) {
        const auto result = bmssp::bmssp_parallel(graph, source, threads, false, backend);
        const auto run = label + " frontier=" + frontier_name + " threads=" + std::to_string(threads);
        if (verify_distances(run, oracle.dist, result.dist, exact ? 0.0 : 1e-9) &&
            verify_predecessors(run, graph, source, result)) {
            std::cout << "[PASS] " << run << " vertices=" << graph.n << " edges=" << graph.m << '\n';
        } else {
            passed = false;
        }
    }
    return passed;
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

bool verify_known_cases(FrontierType backend) {
    struct Case {
        std::string name;
        std::vector<Edge> edges;
        std::vector<double> expected;
        bool exact = false;
    };
    const Case cases[] = {
        {"single vertex", {}, {0.0}},
        {"disconnected", {{0, 1, 3.0}, {2, 3, 2.0}}, {0.0, 3.0, INF_DIST, INF_DIST}},
        {"zero-weight cycle and tied paths",
         {{0, 1, 0.0}, {0, 2, 1.0}, {1, 2, 1.0}, {1, 0, 0.0}, {2, 3, 2.0}},
         {0.0, 0.0, 1.0, 3.0}},
        {"near-tie", {{0, 1, 4e-11}, {0, 2, 0.0}, {2, 1, 0.0}}, {0.0, 0.0, 0.0}, true},
        {"high-degree source and sink",
         {{0, 1, 5.0}, {0, 2, 2.0}, {0, 3, 3.0}, {0, 4, 4.0}, {0, 5, 9.0},
          {1, 5, 1.0}, {2, 5, 2.0}, {3, 5, 1.0}, {4, 5, 0.0}},
         {0.0, 5.0, 2.0, 3.0, 4.0, 4.0}},
    };

    bool passed = true;
    for (const Case& test : cases) {
        const auto graph = CSRGraph::from_edge_list(static_cast<uint32_t>(test.expected.size()), test.edges);
        passed &= verify_oracle(test.name, graph, 0, test.expected, backend, test.exact);
    }
    return passed;
}

bool verify_generated_cases(FrontierType backend) {
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
                passed &= verify_oracle(label, graph, source, expected, backend);
            }
        }
    }
    return passed;
}

bool verify_slice_boundaries(FrontierType backend) {
    using bmssp::detail::EdgeRoute;
    constexpr double complete_bound = 10.0;
    constexpr double trying_bound = 20.0;
    constexpr double bound = 30.0;
    struct Case {
        const char* name;
        double candidate;
        double current;
        bool completed;
        EdgeRoute expected;
        bool updates;
    };
    const Case cases[] = {
        {"at B'", complete_bound, bound, false, EdgeRoute::Prepend, true},
        {"at B_i", trying_bound, bound, false, EdgeRoute::Insert, true},
        {"at B", bound, INF_DIST, false, EdgeRoute::None, false},
        {"equal at B'", complete_bound, complete_bound, false, EdgeRoute::Prepend, false},
        {"equal at B_i", trying_bound, trying_bound, false, EdgeRoute::Insert, false},
        {"equal at B", bound, bound, false, EdgeRoute::None, false},
        {"equal below B'", 9.0, 9.0, false, EdgeRoute::None, false},
        {"equal but completed", trying_bound, trying_bound, true, EdgeRoute::None, false},
    };
    bool passed = true;
    for (const auto& test : cases) {
        bmssp::AtomicVertexState state(bmssp::VertexState{test.current, 2, 7});
        const auto route = bmssp::detail::relax_and_route_edge(
            state, test.candidate, 1, 8, bound, trying_bound, complete_bound, test.completed);
        const bmssp::VertexState expected = test.updates ? bmssp::VertexState{test.candidate, 1, 8}
                                                       : bmssp::VertexState{test.current, 2, 7};
        const auto actual = state.load();
        if (route != test.expected || !(actual == expected)) {
            std::cerr << "[FAIL] " << test.name << " expected_route=" << static_cast<int>(test.expected)
                      << " actual_route=" << static_cast<int>(route) << " expected_dist=" << expected.dist
                      << " actual_dist=" << actual.dist << " pred=" << actual.pred << " epoch=" << actual.epoch << '\n';
            passed = false;
        }
    }
    std::unique_ptr<bmssp::IFrontierD> frontier;
    if (backend == FrontierType::Concurrent) {
        frontier = std::make_unique<bmssp::ConcurrentFrontierD>();
    } else {
        frontier = std::make_unique<bmssp::MutexFrontierD>();
    }
    frontier->init(1, bound);
    frontier->insert(1, complete_bound);
    frontier->insert(2, trying_bound);
    std::vector<uint32_t> vertices;
    double separator = 0.0;
    const bool first = frontier->pull(vertices, separator) && vertices == std::vector<uint32_t>{1} &&
                       separator == trying_bound;
    const bool last = frontier->pull(vertices, separator) && vertices == std::vector<uint32_t>{2} &&
                      separator == bound && frontier->empty();
    const bool empty = !frontier->pull(vertices, separator) && vertices.empty() && separator == bound;
    if (!(first && last && empty)) {
        std::cerr << "[FAIL] frontier pull: first separator=" << first << " last batch=" << last
                  << " empty pull=" << empty << '\n';
        passed = false;
    }
    if (passed) {
        std::cout << "[PASS] slice boundaries, equal-distance routing and empty frontier pull\n";
    }
    return passed;
}

bool verify_granularity(FrontierType backend) {
    static_assert(bmssp::DEFAULT_GRANULARITY_CUTOFF == 2048);
    bool passed = true;
    constexpr uint32_t source = 0;
    omp_set_dynamic(0);
    omp_set_num_threads(4);
    for (uint32_t active_edges : {2047u, 2048u, 2049u}) {
        std::vector<Edge> edges;
        std::vector<bmssp::AtomicVertexState> states(active_edges + 1);
        for (uint32_t v = 1; v <= active_edges; ++v) {
            edges.push_back({source, v, static_cast<double>(v)});
        }
        const auto graph = CSRGraph::from_edge_list(active_edges + 1, edges);
        const auto expected = bmssp::dijkstra_sequential(graph, source);
        bmssp::EpochDeduplicator epochs(graph.n);
        states[source].store({0.0, NO_PRED, 0});
#ifdef BMSSP_ENABLE_PROFILING
        auto& stats = bmssp::get_global_profile_stats();
        stats.reset();
#endif
        bmssp::find_pivots_parallel(graph, INF_DIST, std::span(&source, 1), 1, states.data(), epochs);
        const auto label = "cutoff active_edges=" + std::to_string(active_edges) + " threads=4";
        std::vector<double> actual(graph.n);
        for (uint32_t v = 0; v < graph.n; ++v) {
            actual[v] = states[v].load_dist();
        }
        passed &= verify_distances(label, expected.dist, actual);
#ifdef BMSSP_ENABLE_PROFILING
        const bool expect_parallel = active_edges >= bmssp::DEFAULT_GRANULARITY_CUTOFF;
        if (stats.fp_par_steps != static_cast<uint64_t>(expect_parallel) ||
            stats.fp_seq_steps != static_cast<uint64_t>(!expect_parallel)) {
            std::cerr << "[FAIL] " << label << " sequential=" << stats.fp_seq_steps
                      << " parallel=" << stats.fp_par_steps << '\n';
            passed = false;
        }
        std::cout << "[INFO] " << label << " sequential=" << stats.fp_seq_steps
                  << " parallel=" << stats.fp_par_steps << '\n';
        stats.reset();
#endif
        const auto result = bmssp::bmssp_parallel(graph, source, 4, false, backend);
        passed &= verify_distances(label, expected.dist, result.dist);
#ifdef BMSSP_ENABLE_PROFILING
        if (expect_parallel ? stats.relax_par_edges < active_edges
                            : (stats.relax_par_edges != 0 || stats.relax_seq_edges < active_edges)) {
            std::cerr << "[FAIL] " << label << " unexpected BMSSP relaxation mode\n";
            passed = false;
        }
        std::cout << "[INFO] " << label << " BMSSP relax edges: sequential=" << stats.relax_seq_edges
                  << " parallel=" << stats.relax_par_edges << '\n';
#endif
    }
    return passed;
}

bool verify_dense_contention(FrontierType backend) {
    constexpr uint32_t width = 64;
    constexpr uint32_t vertices = 1 + 2 * width;
    constexpr uint32_t seed = 20261122;
    std::mt19937 random(seed);
    std::vector<Edge> edges;
    std::vector<uint32_t> hubs;
    std::vector<bmssp::AtomicVertexState> states(vertices);
    states[0].store({0.0, NO_PRED, 0});
    for (uint32_t u = 1; u <= width; ++u) {
        edges.push_back({0, u, 1.0});
        hubs.push_back(u);
        states[u].store({1.0, 0, 0});
        for (uint32_t v = 1 + width; v < vertices; ++v) {
            edges.push_back({u, v, static_cast<double>(random() % 5)});
        }
    }

    const auto graph = CSRGraph::from_edge_list(vertices, edges);
    const auto expected = bellman_ford(vertices, edges, 0);
    const auto name = "dense shared destinations seed=" + std::to_string(seed);
    bool passed = verify_oracle(name, graph, 0, expected, backend);

    omp_set_num_threads(4);
    bmssp::EpochDeduplicator epochs(vertices);
#ifdef BMSSP_ENABLE_PROFILING
    auto& stats = bmssp::get_global_profile_stats();
    stats.reset();
#endif
    bmssp::find_pivots_parallel(graph, INF_DIST, hubs, 1, states.data(), epochs);
    std::vector<double> actual(vertices);
    for (uint32_t v = 0; v < vertices; ++v) {
        actual[v] = states[v].load_dist();
    }
    passed &= verify_distances(name + " FindPivots threads=4", expected, actual);
#ifdef BMSSP_ENABLE_PROFILING
    if (stats.fp_par_steps != 1 || stats.fp_par_edges != width * width) {
        std::cerr << "[FAIL] " << name << " parallel_steps=" << stats.fp_par_steps
                  << " parallel_edges=" << stats.fp_par_edges << '\n';
        passed = false;
    }
    std::cout << "[INFO] " << name << " FindPivots threads=4 parallel_edges="
              << stats.fp_par_edges << '\n';
#endif
    return passed;
}

} // namespace

int main(int argc, char** argv) {
    FrontierType backend = FrontierType::Mutex;
    if (argc == 2 && std::string_view(argv[1]) == "--frontier=concurrent") {
        backend = FrontierType::Concurrent;
    } else if (argc != 1) {
        std::cerr << "Usage: verify_correctness [--frontier=concurrent]\n";
        return EXIT_FAILURE;
    }
    const bool known_cases = verify_known_cases(backend);
    const bool generated_cases = verify_generated_cases(backend);
    const bool boundaries = verify_slice_boundaries(backend);
    const bool granularity = verify_granularity(backend);
    const bool dense_contention = verify_dense_contention(backend);
    return known_cases && generated_cases && boundaries && granularity && dense_contention
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
