#include "algorithm/find_pivots.hpp"
#include <omp.h>
#include <unordered_map>

namespace bmssp {

PivotResult find_pivots_parallel(
    const CSRGraph& graph,
    double B,
    std::span<const uint32_t> S,
    size_t k,
    AtomicVertexState* states,
    EpochDeduplicator& deduplicator
) {
    PivotResult result;
    if (S.empty() || k == 0) return result;

    const size_t n = graph.n;
    std::vector<uint32_t> root(n, NO_PRED);
    std::vector<uint32_t> vis;
    vis.reserve(std::min(n, k * S.size() * 2));

    for (uint32_t s : S) {
        root[s] = s;
        vis.push_back(s);
    }

    std::vector<uint32_t> active(S.begin(), S.end());

    int max_threads = omp_get_max_threads();
    std::vector<std::vector<uint32_t>> thread_buffers(max_threads);

    // k steps of parallel Bellman-Ford relaxations
    for (uint32_t step = 1; step <= k; ++step) {
        if (active.empty()) break;

        // Count active outgoing edges to decide execution mode
        size_t active_edges = 0;
        for (uint32_t u : active) {
            active_edges += graph.degree(u);
        }

        for (auto& buf : thread_buffers) {
            buf.clear();
        }

        if (active_edges >= DEFAULT_GRANULARITY_CUTOFF && max_threads > 1) {
            #pragma omp parallel
            {
                int tid = omp_get_thread_num();
                auto& local_buf = thread_buffers[tid];

                #pragma omp for schedule(guided)
                for (size_t i = 0; i < active.size(); ++i) {
                    uint32_t u = active[i];
                    double dist_u = states[u].load(std::memory_order_relaxed).dist;
                    uint32_t root_u = root[u];

                    for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
                        uint32_t v = graph.edge_dest(e);
                        double w = graph.edge_weight(e);
                        double new_dist = dist_u + w;

                        if (new_dist < B) {
                            if (relax_edge_atomic(states[v], new_dist, u, step)) {
                                root[v] = root_u;
                                if (deduplicator.try_claim(v, step)) {
                                    local_buf.push_back(v);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            // Serial execution for small frontiers
            auto& local_buf = thread_buffers[0];
            for (uint32_t u : active) {
                double dist_u = states[u].load(std::memory_order_relaxed).dist;
                uint32_t root_u = root[u];

                for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
                    uint32_t v = graph.edge_dest(e);
                    double w = graph.edge_weight(e);
                    double new_dist = dist_u + w;

                    if (new_dist < B) {
                        if (relax_edge_atomic(states[v], new_dist, u, step)) {
                            root[v] = root_u;
                            if (deduplicator.try_claim(v, step)) {
                                local_buf.push_back(v);
                            }
                        }
                    }
                }
            }
        }

        // Gather new active frontier
        std::vector<uint32_t> next_active;
        parallel_gather(thread_buffers, next_active);

        for (uint32_t v : next_active) {
            vis.push_back(v);
        }

        // Early cutoff: if visited size exceeds k * |S|, return immediately
        if (vis.size() > k * S.size()) {
            result.pivots.assign(S.begin(), S.end());
            result.visited = std::move(vis);
            return result;
        }

        active = std::move(next_active);
    }

    // Subtree decomposition: count size of arborescences rooted at each s in S
    std::unordered_map<uint32_t, size_t> subtree_size;
    subtree_size.reserve(S.size());

    for (uint32_t v : vis) {
        uint32_t r = root[v];
        if (r != NO_PRED) {
            subtree_size[r]++;
        }
    }

    result.pivots.reserve(S.size());
    for (uint32_t s : S) {
        if (subtree_size[s] >= k) {
            result.pivots.push_back(s);
        }
    }

    result.visited = std::move(vis);
    return result;
}

} // namespace bmssp
