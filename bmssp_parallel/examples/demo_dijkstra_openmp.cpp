// Small live example: compare every distance with the existing heap solver.
#include "baseline/dijkstra_openmp.hpp"
#include <omp.h>
#include <iomanip>
#include <iostream>

int main() {
    std::vector<bmssp::Edge> edges;
    auto link = [&](uint32_t u, uint32_t v, double w) {
        edges.push_back({u, v, w});
        edges.push_back({v, u, w});
    };
    // The six-vertex graph from the Burkardt reference snapshot.
    link(0, 1, 40); link(0, 2, 15); link(1, 2, 20);
    link(1, 3, 10); link(1, 4, 25); link(1, 5, 6);
    link(2, 3, 100); link(4, 5, 8);
    auto graph = bmssp::CSRGraph::from_edge_list(6, edges);
    const auto serial = bmssp::dijkstra_sequential(graph, 0);
    const auto parallel = bmssp::dijkstra_openmp(graph, 0, 4);
    std::cout << "DIJKSTRA LIVE DEMO | source=0 | vertices=6 | requested threads=4\n\n"
              << "Vertex   Sequential   OpenMP   Match\n";
    for (uint32_t v = 0; v < graph.n; ++v) {
        std::cout << std::setw(6) << v << std::setw(13) << serial.dist[v]
                  << std::setw(9) << parallel.dist[v] << "   "
                  << (serial.dist[v] == parallel.dist[v] ? "OK" : "FAIL") << '\n';
    }
    const bool matches = serial.dist == parallel.dist;
    std::cout << "\n" << (matches ? "PASS: all distances match.\n" : "FAIL: distance mismatch.\n")
              << "This small example checks results; it is not a speed benchmark.\n";
    return matches ? 0 : 1;
}
