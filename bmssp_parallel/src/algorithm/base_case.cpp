#include "algorithm/base_case.hpp"
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bmssp {

BaseCaseResult base_case(
    const CSRGraph& graph,
    double B,
    uint32_t source,
    size_t k,
    AtomicVertexState* states
) {
    BaseCaseResult result;
    result.new_bound = B;

    if (static_cast<size_t>(source) >= graph.n || states == nullptr || k == 0) {
        return result;
    }

    const double d_src = states[source].load_dist(std::memory_order_relaxed);
    if (!(d_src < B)) {
        return result;
    }

    using Element = std::pair<double, uint32_t>;
    static thread_local std::vector<Element> heap;
    heap.clear();
    if (heap.capacity() < 64) {
        heap.reserve(64);
    }

    const size_t reserve_sz = std::min<size_t>(k, graph.n) + 1;
    std::vector<uint32_t> complete;
    complete.reserve(reserve_sz);

    // For practical k (floor(log^(1/3) n) <= 4 for all 64-bit graphs, and fast up to k <= 32),
    // linear scan over `complete` avoids 100% of std::unordered_set heap allocations.
    const bool use_small_scan = (k <= 32);
    std::unordered_set<uint32_t> large_settled;
    if (!use_small_scan) {
        large_settled.reserve(reserve_sz * 2 + 2);
    }

    auto is_settled = [&](uint32_t u) noexcept -> bool {
        if (use_small_scan) {
            for (uint32_t x : complete) {
                if (x == u) return true;
            }
            return false;
        }
        return large_settled.count(u) > 0;
    };

    heap.emplace_back(d_src, source);

    while (!heap.empty() && complete.size() <= k) {
        const auto [d, u] = heap.front();
        std::pop_heap(heap.begin(), heap.end(), std::greater<Element>{});
        heap.pop_back();

        if (is_settled(u)) {
            continue;
        }

        const double curr_d = states[u].load_dist(std::memory_order_relaxed);
        if (d > curr_d || !(d < B)) {
            continue;
        }

        if (!use_small_scan) {
            large_settled.insert(u);
        }
        complete.push_back(u);

        for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
            const uint32_t v = graph.edge_dest(e);
            const double w = graph.edge_weight(e);
            const double new_dist = d + w;

            if (new_dist < B) {
                const double curr_v = states[v].load_dist(std::memory_order_relaxed);
                if (new_dist < curr_v) {
                    static_cast<void>(relax_edge_atomic(states[v], new_dist, u, 0));
                    heap.emplace_back(new_dist, v);
                    std::push_heap(heap.begin(), heap.end(), std::greater<Element>{});
                } else if (new_dist == curr_v && !is_settled(v)) {
                    heap.emplace_back(new_dist, v);
                    std::push_heap(heap.begin(), heap.end(), std::greater<Element>{});
                }
            }
        }
    }

    if (complete.size() <= k) {
        result.new_bound = B;
        result.completed = std::move(complete);
    } else {
        // Pulled k + 1 vertices: determine new bound from the (k+1)-th vertex
        const double last_d = states[complete.back()].load_dist(std::memory_order_relaxed);
        if (last_d > d_src) {
            result.new_bound = last_d;
            result.completed.reserve(k);
            for (uint32_t u : complete) {
                if (states[u].load_dist(std::memory_order_relaxed) < last_d) {
                    result.completed.push_back(u);
                }
            }
        } else {
            // All k + 1 settled vertices tie at d_src (e.g. zero-weight edges/cycle):
            // drop complete.back() and keep the first k vertices to guarantee progress.
            complete.pop_back();
            result.new_bound = last_d;
            result.completed = std::move(complete);
        }
    }

    return result;
}

} // namespace bmssp
