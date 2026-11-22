#include "algorithm/find_pivots.hpp"
#include "core/profiler.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <omp.h>

namespace bmssp {

namespace {

inline void process_edge_relaxation(
    uint32_t u,
    double dist_u,
    uint32_t v,
    double w,
    double B,
    uint32_t step_epoch,
    AtomicVertexState* states,
    EpochDeduplicator& deduplicator,
    std::vector<uint32_t>& local_buf
) {
    const double new_dist = dist_u + w;
    if (!(new_dist < B)) {
        return;
    }

    const double curr_v = states[v].load_dist(std::memory_order_relaxed);
    if (new_dist < curr_v) {
        const bool relaxed = relax_edge_atomic(states[v], new_dist, u, step_epoch);
        if (relaxed || new_dist <= states[v].load_dist(std::memory_order_relaxed)) {
            if (deduplicator.try_claim(v, step_epoch)) {
                local_buf.push_back(v);
            }
        }
    } else if (new_dist == curr_v) {
        if (deduplicator.try_claim(v, step_epoch)) {
            local_buf.push_back(v);
        }
    }
}

} // namespace

FindPivotsWorkspace::FindPivotsWorkspace(size_t n) {
    if (n > 0) {
        ensure_size(n);
    }
}

void FindPivotsWorkspace::ensure_size(size_t n) {
    if (vis_tracker.size() < n) {
        vis_tracker.resize(n);
    }
    if (in_s_tracker.size() < n) {
        in_s_tracker.resize(n);
    }
    if (parent_in_forest.size() < n) {
        parent_in_forest.resize(n, NO_PRED);
    }
    if (root_of.size() < n) {
        root_of.resize(n, NO_PRED);
    }
    if (subtree_cnt.size() < n) {
        subtree_cnt.resize(n, 0);
    }
    const size_t max_threads = static_cast<size_t>(std::max(1, omp_get_max_threads()));
    if (thread_buffers.size() < max_threads) {
        thread_buffers.resize(max_threads);
    }
}

PivotResult find_pivots_parallel(
    const CSRGraph& graph,
    double B,
    std::span<const uint32_t> S,
    size_t k,
    AtomicVertexState* states,
    EpochDeduplicator& deduplicator,
    FindPivotsWorkspace* workspace,
    size_t granularity_cutoff
) {
    PivotResult result;
    if (S.empty() || k == 0 || graph.n == 0 || states == nullptr) {
        return result;
    }

#ifdef BMSSP_ENABLE_PROFILING
    auto& prof = get_global_profile_stats();
    prof.fp_calls++;
    const uint64_t fp_t0 = prof_now_ns();
    struct FpTotalGuard {
        BMSSPProfileStats& p;
        uint64_t t0;
        ~FpTotalGuard() { p.find_pivots_total_ns += (prof_now_ns() - t0); }
    } fp_total_guard{prof, fp_t0};
#endif

    const size_t n = graph.n;
    if (deduplicator.size() < n) {
        deduplicator.resize(n);
    }

    FindPivotsWorkspace fallback_ws;
    FindPivotsWorkspace& ws = (workspace != nullptr) ? *workspace : fallback_ws;
    ws.ensure_size(n);

    const uint32_t vis_epoch = ws.vis_tracker.allocate_epoch();

    std::vector<uint32_t> unique_S;
    unique_S.reserve(S.size());

    std::vector<uint32_t> vis;
    const size_t max_vis_bound = (k <= (std::numeric_limits<size_t>::max() / S.size()))
        ? (k * S.size())
        : n;
    vis.reserve(std::min(n, max_vis_bound + S.size()));

    for (uint32_t s : S) {
        if (static_cast<size_t>(s) < n && ws.vis_tracker.try_claim(s, vis_epoch)) {
            unique_S.push_back(s);
            vis.push_back(s);
        }
    }

    if (unique_S.empty()) {
        return result;
    }

    std::vector<uint32_t> active = unique_S;
    std::vector<uint32_t> next_active;

    const int max_threads = std::max(1, omp_get_max_threads());
    const size_t cutoff = (k <= (std::numeric_limits<size_t>::max() / unique_S.size()))
        ? (k * unique_S.size())
        : std::numeric_limits<size_t>::max();

    // k steps of parallel Bellman-Ford relaxations
    for (size_t step = 1; step <= k; ++step) {
        if (active.empty()) {
            break;
        }

        const uint32_t step_epoch = deduplicator.allocate_epoch();

        // Build edge prefix offsets for edge-level work partitioning across active vertices
        ws.edge_offsets.resize(active.size() + 1);
        ws.edge_offsets[0] = 0;
        for (size_t i = 0; i < active.size(); ++i) {
            const uint32_t u = active[i];
            const uint64_t deg = (static_cast<size_t>(u) < n) ? static_cast<uint64_t>(graph.degree(u)) : 0ULL;
            ws.edge_offsets[i + 1] = ws.edge_offsets[i] + deg;
        }
        const uint64_t active_edges = ws.edge_offsets[active.size()];

        if (active_edges == 0) {
            break;
        }

        for (auto& buf : ws.thread_buffers) {
            buf.clear();
        }

        if (active_edges >= granularity_cutoff && max_threads > 1) {
#ifdef BMSSP_ENABLE_PROFILING
            prof.fp_par_steps++;
            prof.fp_par_edges += active_edges;
            const uint64_t fp_par_t0 = prof_now_ns();
            std::vector<uint64_t> fp_tid_ns(static_cast<size_t>(max_threads), 0);
            int fp_used_threads = 1;
#endif
            #pragma omp parallel
            {
                const int tid = omp_get_thread_num();
                const int num_threads = omp_get_num_threads();
#ifdef BMSSP_ENABLE_PROFILING
                const uint64_t t_local0 = prof_now_ns();
                if (tid == 0) fp_used_threads = num_threads;
#endif
                auto& local_buf = ws.thread_buffers[static_cast<size_t>(tid)];

                const uint64_t chunk_size =
                    (active_edges + static_cast<uint64_t>(num_threads) - 1ULL) /
                    static_cast<uint64_t>(num_threads);
                const uint64_t edge_start = std::min(static_cast<uint64_t>(tid) * chunk_size, active_edges);
                const uint64_t edge_end = std::min(edge_start + chunk_size, active_edges);

                if (edge_start < edge_end) {
                    auto it = std::upper_bound(
                        ws.edge_offsets.begin(),
                        ws.edge_offsets.end(),
                        edge_start
                    );
                    size_t u_idx = static_cast<size_t>(std::distance(ws.edge_offsets.begin(), it)) - 1;
                    uint64_t cur_edge = edge_start;

                    while (cur_edge < edge_end) {
                        const uint64_t u_end_offset = ws.edge_offsets[u_idx + 1];
                        if (cur_edge >= u_end_offset) {
                            ++u_idx;
                            continue;
                        }

                        const uint64_t seg_end = std::min(edge_end, u_end_offset);
                        const uint32_t u = active[u_idx];
                        const double dist_u = states[u].load_dist(std::memory_order_relaxed);

                        if (dist_u < B) {
                            const uint64_t base_offset = ws.edge_offsets[u_idx];
                            const uint64_t csr_base = graph.edge_begin(u);
                            const uint64_t e_begin = csr_base + (cur_edge - base_offset);
                            const uint64_t e_end = csr_base + (seg_end - base_offset);

                            for (uint64_t e = e_begin; e < e_end; ++e) {
                                process_edge_relaxation(
                                    u,
                                    dist_u,
                                    graph.edge_dest(e),
                                    graph.edge_weight(e),
                                    B,
                                    step_epoch,
                                    states,
                                    deduplicator,
                                    local_buf
                                );
                            }
                        }

                        cur_edge = seg_end;
                        if (cur_edge == u_end_offset) {
                            ++u_idx;
                        }
                    }
                }
#ifdef BMSSP_ENABLE_PROFILING
                fp_tid_ns[static_cast<size_t>(tid)] = prof_now_ns() - t_local0;
#endif
            }

            parallel_gather(ws.thread_buffers, next_active);
#ifdef BMSSP_ENABLE_PROFILING
            prof.fp_relax_par_ns += (prof_now_ns() - fp_par_t0);
            uint64_t fp_sum = 0, fp_max = 0;
            for (int t = 0; t < fp_used_threads; ++t) {
                fp_sum += fp_tid_ns[static_cast<size_t>(t)];
                fp_max = std::max(fp_max, fp_tid_ns[static_cast<size_t>(t)]);
            }
            prof.fp_par_thread_sum_ns += fp_sum;
            prof.fp_par_thread_max_ns += fp_max;
#endif
        } else {
            // Serial execution for small frontiers
#ifdef BMSSP_ENABLE_PROFILING
            prof.fp_seq_steps++;
            prof.fp_seq_edges += active_edges;
            const uint64_t fp_seq_t0 = prof_now_ns();
#endif
            auto& local_buf = ws.thread_buffers[0];
            for (uint32_t u : active) {
                if (static_cast<size_t>(u) >= n) {
                    continue;
                }
                const double dist_u = states[u].load_dist(std::memory_order_relaxed);
                if (!(dist_u < B)) {
                    continue;
                }

                for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
                    process_edge_relaxation(
                        u,
                        dist_u,
                        graph.edge_dest(e),
                        graph.edge_weight(e),
                        B,
                        step_epoch,
                        states,
                        deduplicator,
                        local_buf
                    );
                }
            }

            next_active.assign(local_buf.begin(), local_buf.end());
#ifdef BMSSP_ENABLE_PROFILING
            prof.fp_relax_seq_ns += (prof_now_ns() - fp_seq_t0);
#endif
        }

        for (uint32_t v : next_active) {
            if (ws.vis_tracker.try_claim(v, vis_epoch)) {
                vis.push_back(v);
            }
        }

        // Early cutoff: if visited size exceeds k * |S|, return immediately
        if (vis.size() > cutoff) {
            result.pivots = std::move(unique_S);
            result.visited = std::move(vis);
            return result;
        }

        active.swap(next_active);
    }

    // Sequential forest root resolution on vis (size <= k * |S|)
#ifdef BMSSP_ENABLE_PROFILING
    const uint64_t fp_forest_t0 = prof_now_ns();
#endif
    const uint32_t s_epoch = ws.in_s_tracker.allocate_epoch();
    for (uint32_t s : unique_S) {
        static_cast<void>(ws.in_s_tracker.try_claim(s, s_epoch));
        ws.root_of[s] = s;
        ws.subtree_cnt[s] = 0;
    }

    for (uint32_t v : vis) {
        if (!ws.in_s_tracker.is_claimed(v, s_epoch)) {
            ws.root_of[v] = NO_PRED;
            ws.parent_in_forest[v] = states[v].load(std::memory_order_relaxed).pred;
        } else {
            ws.parent_in_forest[v] = v;
        }
    }

    constexpr uint32_t ROOT_VISITING = NO_PRED - 1;
    constexpr uint32_t ROOT_NONE = NO_PRED - 2;

    if (n <= static_cast<size_t>(ROOT_NONE)) {
        std::vector<uint32_t> path;
        path.reserve(k + 2);

        for (uint32_t v : vis) {
            uint32_t r = ws.root_of[v];
            if (r == NO_PRED) {
                path.clear();
                uint32_t cur = v;
                uint32_t resolved = ROOT_NONE;

                for (size_t hop = 0; hop <= vis.size(); ++hop) {
                    const uint32_t st = ws.root_of[cur];
                    if (st != NO_PRED) {
                        resolved = (st == ROOT_VISITING) ? ROOT_NONE : st;
                        break;
                    }
                    ws.root_of[cur] = ROOT_VISITING;
                    path.push_back(cur);

                    const uint32_t p = ws.parent_in_forest[cur];
                    if (p == NO_PRED || static_cast<size_t>(p) >= n ||
                        !ws.vis_tracker.is_claimed(p, vis_epoch)) {
                        resolved = ROOT_NONE;
                        break;
                    }
                    cur = p;
                }

                for (uint32_t node : path) {
                    ws.root_of[node] = resolved;
                }
                r = resolved;
            }

            if (static_cast<size_t>(r) < n) {
                ws.subtree_cnt[r]++;
            }
        }
    } else {
        for (uint32_t v : vis) {
            uint32_t r = ws.root_of[v];
            if (r == NO_PRED) {
                uint32_t cur = v;
                for (size_t hop = 0; hop < k; ++hop) {
                    const uint32_t p = ws.parent_in_forest[cur];
                    if (p == NO_PRED || static_cast<size_t>(p) >= n ||
                        !ws.vis_tracker.is_claimed(p, vis_epoch)) {
                        break;
                    }
                    cur = p;
                    if (ws.root_of[cur] != NO_PRED) {
                        r = ws.root_of[cur];
                        break;
                    }
                    if (ws.in_s_tracker.is_claimed(cur, s_epoch)) {
                        r = cur;
                        break;
                    }
                }
                if (r != NO_PRED) {
                    ws.root_of[v] = r;
                }
            }
            if (r != NO_PRED) {
                ws.subtree_cnt[r]++;
            }
        }
    }

    result.pivots.reserve(unique_S.size());
    for (uint32_t s : unique_S) {
        if (static_cast<size_t>(ws.subtree_cnt[s]) >= k) {
            result.pivots.push_back(s);
        }
    }

    result.visited = std::move(vis);
#ifdef BMSSP_ENABLE_PROFILING
    prof.fp_forest_ns += (prof_now_ns() - fp_forest_t0);
#endif
    return result;
}

} // namespace bmssp
