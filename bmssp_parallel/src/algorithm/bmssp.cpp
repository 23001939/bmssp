#include "algorithm/bmssp.hpp"
#include "algorithm/find_pivots.hpp"
#include "algorithm/base_case.hpp"
#include "core/frontier_interface.hpp"
#include "core/primitives.hpp"
#include "core/profiler.hpp"
#include "data_structure/concurrent_frontier_d.hpp"

#include <omp.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <new>
#include <queue>
#include <span>
#include <utility>
#include <vector>

namespace bmssp {

namespace {

inline size_t safe_pow2_capped(size_t shift, size_t cap) noexcept {
    if (shift >= 62) {
        return cap;
    }
    return std::min<size_t>(static_cast<size_t>(uint64_t{1} << shift), cap);
}

struct AlignedStateDeleter {
    void operator()(AtomicVertexState* ptr) const noexcept {
        ::operator delete[](ptr, std::align_val_t{alignof(AtomicVertexState)});
    }
};

#if !defined(__SANITIZE_THREAD__)
void sort_k_openmp(std::vector<VertexDist>& K, int max_threads) {
    const size_t n = K.size();
    if (n < 4096 || max_threads <= 1) {
        std::sort(K.begin(), K.end());
        return;
    }

    const size_t num_chunks = std::min<size_t>(static_cast<size_t>(max_threads), 16);
    std::vector<size_t> bounds(num_chunks + 1, 0);
    for (size_t c = 0; c <= num_chunks; ++c) {
        bounds[c] = (n * c) / num_chunks;
    }

    #pragma omp parallel for schedule(static)
    for (ptrdiff_t c = 0; c < static_cast<ptrdiff_t>(num_chunks); ++c) {
        const size_t b = bounds[static_cast<size_t>(c)];
        const size_t e = bounds[static_cast<size_t>(c) + 1];
        if (b < e) {
            std::sort(K.begin() + static_cast<ptrdiff_t>(b),
                      K.begin() + static_cast<ptrdiff_t>(e));
        }
    }

    for (size_t step = 1; step < num_chunks; step *= 2) {
        const size_t merge_pairs = (num_chunks + 2 * step - 1) / (2 * step);
        #pragma omp parallel for schedule(static) if(merge_pairs > 1)
        for (ptrdiff_t p = 0; p < static_cast<ptrdiff_t>(merge_pairs); ++p) {
            const size_t c0 = static_cast<size_t>(p) * 2 * step;
            const size_t c1 = c0 + step;
            if (c1 >= num_chunks) continue;
            const size_t c2 = std::min(c0 + 2 * step, num_chunks);
            const size_t left = bounds[c0];
            const size_t mid = bounds[c1];
            const size_t right = bounds[c2];
            if (left < mid && mid < right) {
                std::inplace_merge(K.begin() + static_cast<ptrdiff_t>(left),
                                   K.begin() + static_cast<ptrdiff_t>(mid),
                                   K.begin() + static_cast<ptrdiff_t>(right));
            }
        }
    }
}
#endif

struct LevelWorkspace {
    std::unique_ptr<IFrontierD> frontier;
    std::vector<std::vector<VertexDist>> thread_k_buffers;
    std::vector<std::vector<VertexDist>> thread_d_buffers;
    std::vector<uint32_t> miniS;
    std::vector<uint64_t> edge_prefix;
    std::vector<VertexDist> K;
};

struct BMSSPContext {
    const CSRGraph& graph;
    size_t k;
    size_t t;
    size_t max_level;
    int max_threads;
    AtomicVertexState* states;
    EpochDeduplicator& deduplicator;
    std::unique_ptr<uint8_t[]> global_completed;
    // Bitmask (1 << level) for levels 0..7; cleared in O(|complete|) on exit from bmssp_rec.
    std::unique_ptr<uint8_t[]> level_complete_mask;
    std::deque<EpochDeduplicator> overflow_dedups;
    FrontierType frontier_type;
    FindPivotsWorkspace fp_ws;
    std::vector<LevelWorkspace> level_workspaces;

    BMSSPContext(const CSRGraph& g,
                 size_t k_val,
                 size_t t_val,
                 size_t max_lvl,
                 AtomicVertexState* st,
                 EpochDeduplicator& dedup,
                 FrontierType ftype)
        : graph(g),
          k(k_val),
          t(t_val),
          max_level(max_lvl),
          max_threads(std::max(1, omp_get_max_threads())),
          states(st),
          deduplicator(dedup),
          global_completed(std::make_unique_for_overwrite<uint8_t[]>(g.n)),
          level_complete_mask(std::make_unique_for_overwrite<uint8_t[]>(g.n)),
          frontier_type(ftype),
          fp_ws(0),
          level_workspaces(max_lvl + 1) {
        fp_ws.vertex_flags.resize(g.n, 0);
        fp_ws.thread_buffers.resize(static_cast<size_t>(max_threads));

        if (g.n >= DEFAULT_GRANULARITY_CUTOFF && max_threads > 1) {
            #pragma omp parallel for schedule(static)
            for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(g.n); ++i) {
                global_completed[static_cast<size_t>(i)] = 0;
                level_complete_mask[static_cast<size_t>(i)] = 0;
            }
        } else {
            std::fill_n(global_completed.get(), g.n, uint8_t{0});
            std::fill_n(level_complete_mask.get(), g.n, uint8_t{0});
        }

        if (max_level >= 8) {
            for (size_t lvl = 8; lvl <= max_level; ++lvl) {
                overflow_dedups.emplace_back(g.n);
            }
        }

        for (size_t lvl = 1; lvl <= max_level; ++lvl) {
            auto& ws = level_workspaces[lvl];
            if (frontier_type == FrontierType::Concurrent) {
                ws.frontier = std::make_unique<ConcurrentFrontierD>(states, g.n);
            } else {
                ws.frontier = std::make_unique<MutexFrontierD>();
            }
            ws.thread_k_buffers.resize(static_cast<size_t>(max_threads));
            ws.thread_d_buffers.resize(static_cast<size_t>(max_threads));
        }
    }
};

inline void relax_outgoing_edge(
    BMSSPContext& ctx,
    int level,
    double B,
    double trying_B,
    double complete_B,
    uint32_t u,
    double du,
    uint64_t e,
    std::vector<VertexDist>& local_d,
    std::vector<VertexDist>& local_k
) {
    const uint32_t v = ctx.graph.edge_dest(e);
    const double w = ctx.graph.edge_weight(e);
    const double new_dist = du + w;
    if (new_dist >= B) {
        return;
    }

    const double curr_v = ctx.states[v].load_dist();
    if (new_dist < curr_v) {
        if (relax_edge_atomic(ctx.states[v], new_dist, u, static_cast<uint32_t>(level))) {
            if (new_dist >= trying_B) {
                local_d.push_back(VertexDist{v, new_dist});
            } else {
                // Covers both [complete_B, trying_B) and strictly improved < complete_B.
                local_k.push_back(VertexDist{v, new_dist});
            }
        }
    } else if (new_dist == curr_v && !ctx.global_completed[v]) {
        if (new_dist >= trying_B) {
            local_d.push_back(VertexDist{v, new_dist});
        } else if (new_dist >= complete_B) {
            // Algorithm 3: equal-distance uncompleted vertices in [complete_B, trying_B) go to K.
            local_k.push_back(VertexDist{v, new_dist});
        }
    }
}

inline void flush_local_d_to_frontier(
    std::vector<VertexDist>& local_d,
    const AtomicVertexState* states,
    IFrontierD* D,
    int tid = 0,
    int nthreads = 1
) {
    if (local_d.empty()) {
        return;
    }

    // Drop stale entries in-place before sorting/deduplicating.
    size_t write = 0;
    for (size_t i = 0; i < local_d.size(); ++i) {
        const VertexDist& item = local_d[i];
        if (item.dist <= states[item.v].load_dist()) {
            local_d[write++] = item;
        }
    }
    local_d.resize(write);
    if (local_d.empty()) {
        return;
    }

    if (local_d.size() > 1) {
        // Deduplicate by vertex keeping minimum distance, then sort by distance ascending
        // so D1SkipList::insert_batch_sorted inserts contiguous runs under a single ReaderGuard.
        std::sort(local_d.begin(), local_d.end(), [](const VertexDist& a, const VertexDist& b) noexcept {
            if (a.v != b.v) return a.v < b.v;
            return a.dist < b.dist;
        });
        size_t uwrite = 0;
        for (size_t i = 0; i < local_d.size(); ++i) {
            if (uwrite == 0 || local_d[i].v != local_d[uwrite - 1].v) {
                local_d[uwrite++] = local_d[i];
            }
        }
        local_d.resize(uwrite);
        std::sort(local_d.begin(), local_d.end());
    }

    // Stagger starting distance offset across threads to scatter concurrent inserts
    // across distinct skip-list blocks instead of forming a lockstep convoy at block 0.
    if (nthreads > 1 && local_d.size() >= 64) {
        const size_t offset =
            (local_d.size() * static_cast<size_t>(tid)) / static_cast<size_t>(nthreads);
        const std::span<const VertexDist> full_span(local_d);
        if (offset > 0 && offset < local_d.size()) {
            D->insert_batch(full_span.subspan(offset));
            D->insert_batch(full_span.subspan(0, offset));
            return;
        }
    }

    D->insert_batch(local_d);
}

std::pair<double, std::vector<uint32_t>> bmssp_rec(
    BMSSPContext& ctx,
    int level,
    double B,
    std::span<const uint32_t> S
) {
    if (S.empty()) {
        return {B, {}};
    }

    if (level == 0) {
#ifdef BMSSP_ENABLE_PROFILING
        auto& prof = get_global_profile_stats();
        prof.base_case_calls++;
        const uint64_t bc_t0 = prof_now_ns();
#endif
        if (S.size() == 1) {
            auto bc_res = base_case(ctx.graph, B, S[0], ctx.k, ctx.states);
            for (uint32_t u : bc_res.completed) {
                ctx.global_completed[u] = 1;
            }
#ifdef BMSSP_ENABLE_PROFILING
            prof.base_case_ns += (prof_now_ns() - bc_t0);
#endif
            return {bc_res.new_bound, std::move(bc_res.completed)};
        }

        std::vector<uint32_t> sorted_S(S.begin(), S.end());
        std::sort(sorted_S.begin(), sorted_S.end(), [&](uint32_t a, uint32_t b) {
            const double da = ctx.states[a].load_dist();
            const double db = ctx.states[b].load_dist();
            if (da != db) return da < db;
            return a < b;
        });

        double cur_B = B;
        std::vector<std::pair<uint32_t, double>> candidates;
        candidates.reserve(sorted_S.size() * ctx.k);

        for (uint32_t s : sorted_S) {
            if (ctx.states[s].load_dist() >= cur_B) {
                continue;
            }
            auto bc_res = base_case(ctx.graph, cur_B, s, ctx.k, ctx.states);
            const double call_bound = bc_res.new_bound;
            cur_B = std::min(cur_B, call_bound);
            for (uint32_t u : bc_res.completed) {
                candidates.emplace_back(u, call_bound);
            }
        }

        std::vector<uint32_t> merged;
        merged.reserve(candidates.size());
        constexpr uint8_t bit0 = uint8_t{1};
        for (const auto& [u, call_bound] : candidates) {
            const double du = ctx.states[u].load_dist();
            if (du < cur_B || (cur_B == call_bound && du == cur_B)) {
                if ((ctx.level_complete_mask[u] & bit0) == 0) {
                    ctx.level_complete_mask[u] |= bit0;
                    ctx.global_completed[u] = 1;
                    merged.push_back(u);
                }
            }
        }
        for (uint32_t u : merged) {
            ctx.level_complete_mask[u] &= static_cast<uint8_t>(~bit0);
        }
#ifdef BMSSP_ENABLE_PROFILING
        prof.base_case_ns += (prof_now_ns() - bc_t0);
#endif
        return {cur_B, std::move(merged)};
    }

    // Step 1: Find Pivots (Algorithm 1)
    auto [P, bellman_vis] = find_pivots_parallel(
        ctx.graph, B, S, ctx.k, ctx.states, ctx.deduplicator, &ctx.fp_ws
    );

    const bool use_bitmask = (level < 8);
    const uint8_t lvl_bit = use_bitmask ? static_cast<uint8_t>(1U << level) : uint8_t{0};
    uint32_t overflow_epoch = 0;
    if (!use_bitmask) {
        overflow_epoch = ctx.overflow_dedups[static_cast<size_t>(level - 8)].allocate_epoch();
    }

    auto try_claim_level = [&](uint32_t u) noexcept -> bool {
        if (use_bitmask) {
            if ((ctx.level_complete_mask[u] & lvl_bit) == 0) {
                ctx.level_complete_mask[u] |= lvl_bit;
                return true;
            }
            return false;
        }
        return ctx.overflow_dedups[static_cast<size_t>(level - 8)].try_claim(u, overflow_epoch);
    };

    // Step 2: Reuse Per-Level Frontier D and Workspace (Lemma 3.3)
    const size_t shift_batch = static_cast<size_t>(level - 1) * ctx.t;
    const size_t batch_size = std::max<size_t>(1, safe_pow2_capped(shift_batch, ctx.graph.n));

    auto& lvl_ws = ctx.level_workspaces[static_cast<size_t>(level)];
    IFrontierD* D = lvl_ws.frontier.get();
#ifdef BMSSP_ENABLE_PROFILING
    auto& prof = get_global_profile_stats();
    prof.d_init_calls++;
    const uint64_t d_init_t0 = prof_now_ns();
#endif
    D->init(batch_size, B);
#ifdef BMSSP_ENABLE_PROFILING
    prof.d_init_ns += (prof_now_ns() - d_init_t0);
#endif

    double last_complete_B = B;
    for (uint32_t p : P) {
        const double dp = ctx.states[p].load_dist();
        if (dp < B) {
            D->insert(p, dp);
            last_complete_B = std::min(last_complete_B, dp);
        }
    }

    // Step 3: Main Iteration Loop
    const size_t shift_quota = static_cast<size_t>(level) * ctx.t;
    const size_t pow2_lvl = safe_pow2_capped(shift_quota, ctx.graph.n);
    const size_t quota = (pow2_lvl > ctx.graph.n / ctx.k) ? ctx.graph.n : (ctx.k * pow2_lvl);

    std::vector<uint32_t> complete;
    const size_t initial_reserve = std::min<size_t>(
        quota,
        std::min<size_t>(ctx.graph.n, std::max<size_t>(S.size() * ctx.k, size_t{64}))
    );
    complete.reserve(initial_reserve);

    const int max_threads = ctx.max_threads;
    const uint64_t par_edge_cutoff = std::max<uint64_t>(
        DEFAULT_GRANULARITY_CUTOFF,
        512ULL * static_cast<uint64_t>(max_threads)
    );
    auto& thread_k_buffers = lvl_ws.thread_k_buffers;
    auto& thread_d_buffers = lvl_ws.thread_d_buffers;
    auto& miniS = lvl_ws.miniS;
    auto& edge_prefix = lvl_ws.edge_prefix;
    auto& K = lvl_ws.K;
    miniS.clear();
    edge_prefix.clear();
    K.clear();

    // Never truncate the root recursion level by quota while D still holds active vertices.
    const bool is_top_level = (static_cast<size_t>(level) == ctx.max_level);
    bool d_exhausted = false;

    while (is_top_level || complete.size() < quota) {
        double trying_B = B;
#ifdef BMSSP_ENABLE_PROFILING
        prof.d_pull_calls++;
        const uint64_t pull_t0 = prof_now_ns();
#endif
        const bool pulled = D->pull(miniS, trying_B);
#ifdef BMSSP_ENABLE_PROFILING
        prof.d_pull_ns += (prof_now_ns() - pull_t0);
#endif
        if (!pulled) {
            d_exhausted = true;
            break;
        }

        if (miniS.size() > 1 && ctx.frontier_type == FrontierType::Mutex) {
            std::sort(miniS.begin(), miniS.end());
            miniS.erase(std::unique(miniS.begin(), miniS.end()), miniS.end());
        }

        // Recursive call down to level - 1
        auto [complete_B, nw_complete] = bmssp_rec(ctx, level - 1, trying_B, miniS);

        for (uint32_t u : nw_complete) {
            ctx.global_completed[u] = 1;
            if (try_claim_level(u)) {
                complete.push_back(u);
            }
        }

        // When > M vertices tie at distance trying_B and the sub-call settled all < trying_B,
        // promote the tied miniS vertices directly so they do not re-pull indefinitely at trying_B.
        if (complete_B == trying_B) {
            for (uint32_t x : miniS) {
                if (ctx.global_completed[x]) {
                    continue;
                }
                const double dx = ctx.states[x].load_dist();
                if (dx == trying_B && dx < B) {
                    ctx.global_completed[x] = 1;
                    if (try_claim_level(x)) {
                        complete.push_back(x);
                    }
                    nw_complete.push_back(x);
                }
            }
        }

        // Relax outgoing edges from nw_complete using edge-level partitioning
        edge_prefix.resize(nw_complete.size() + 1);
        edge_prefix[0] = 0;
        for (size_t i = 0; i < nw_complete.size(); ++i) {
            edge_prefix[i + 1] = edge_prefix[i] + ctx.graph.degree(nw_complete[i]);
        }
        const uint64_t total_edges = edge_prefix.back();

        if (total_edges >= par_edge_cutoff && max_threads > 1) {
            for (auto& buf : thread_k_buffers) {
                buf.clear();
            }
            for (auto& buf : thread_d_buffers) {
                buf.clear();
            }
#ifdef BMSSP_ENABLE_PROFILING
            prof.relax_par_calls++;
            prof.relax_par_edges += total_edges;
            const uint64_t par_wall_t0 = prof_now_ns();
            std::vector<uint64_t> tid_relax_ns(static_cast<size_t>(max_threads), 0);
            std::vector<uint64_t> tid_ins_ns(static_cast<size_t>(max_threads), 0);
            std::vector<uint64_t> tid_ins_items(static_cast<size_t>(max_threads), 0);
            int par_used_threads = 1;
#endif
            #pragma omp parallel
            {
                const int tid = omp_get_thread_num();
                const int nthreads = omp_get_num_threads();
#ifdef BMSSP_ENABLE_PROFILING
                const uint64_t t_rel0 = prof_now_ns();
                if (tid == 0) par_used_threads = nthreads;
#endif
                auto& local_k = thread_k_buffers[static_cast<size_t>(tid)];
                auto& local_d = thread_d_buffers[static_cast<size_t>(tid)];

                const uint64_t t_begin =
                    (total_edges * static_cast<uint64_t>(tid)) / static_cast<uint64_t>(nthreads);
                const uint64_t t_end =
                    (total_edges * static_cast<uint64_t>(tid + 1)) / static_cast<uint64_t>(nthreads);

                if (t_begin < t_end) {
                    auto it = std::upper_bound(edge_prefix.begin() + 1, edge_prefix.end(), t_begin);
                    size_t vi = static_cast<size_t>(std::distance(edge_prefix.begin() + 1, it));
                    uint64_t cur_g = t_begin;

                    while (cur_g < t_end && vi < nw_complete.size()) {
                        const uint64_t v_start_g = edge_prefix[vi];
                        const uint64_t v_end_g = edge_prefix[vi + 1];
                        if (cur_g < v_end_g) {
                            const uint32_t u = nw_complete[vi];
                            const uint64_t local_begin = cur_g - v_start_g;
                            const uint64_t local_end = std::min(v_end_g, t_end) - v_start_g;
                            const double du = ctx.states[u].load_dist();
                            if (du < B) {
                                const uint64_t csr_base = ctx.graph.edge_begin(u);
                                for (uint64_t e = csr_base + local_begin; e < csr_base + local_end; ++e) {
                                    relax_outgoing_edge(
                                        ctx, level, B, trying_B, complete_B,
                                        u, du, e, local_d, local_k
                                    );
                                }
                            }
                            cur_g = std::min(v_end_g, t_end);
                        }
                        ++vi;
                    }
                }

#ifdef BMSSP_ENABLE_PROFILING
                const uint64_t t_ins0 = prof_now_ns();
                tid_relax_ns[static_cast<size_t>(tid)] = t_ins0 - t_rel0;
                tid_ins_items[static_cast<size_t>(tid)] = local_d.size();
#endif
                flush_local_d_to_frontier(local_d, ctx.states, D, tid, nthreads);
#ifdef BMSSP_ENABLE_PROFILING
                tid_ins_ns[static_cast<size_t>(tid)] = prof_now_ns() - t_ins0;
#endif
            }
#ifdef BMSSP_ENABLE_PROFILING
            const uint64_t par_wall = prof_now_ns() - par_wall_t0;
            prof.relax_par_wall_ns += par_wall;
            uint64_t r_sum = 0, r_max = 0, i_sum = 0, i_max = 0, items_sum = 0;
            for (int t = 0; t < par_used_threads; ++t) {
                const size_t idx = static_cast<size_t>(t);
                r_sum += tid_relax_ns[idx];
                r_max = std::max(r_max, tid_relax_ns[idx]);
                i_sum += tid_ins_ns[idx];
                i_max = std::max(i_max, tid_ins_ns[idx]);
                items_sum += tid_ins_items[idx];
            }
            prof.relax_par_thread_sum_ns += r_sum;
            prof.relax_par_thread_max_ns += r_max;
            prof.insert_par_thread_sum_ns += i_sum;
            prof.insert_par_thread_max_ns += i_max;
            prof.insert_batch_items += items_sum;
            const uint64_t tot_work = r_sum + i_sum;
            if (tot_work > 0) {
                const uint64_t ins_share = static_cast<uint64_t>(
                    (static_cast<double>(par_wall) * static_cast<double>(i_sum)) / static_cast<double>(tot_work)
                );
                prof.insert_par_est_wall_ns += ins_share;
                prof.relax_par_est_wall_ns += (par_wall - ins_share);
            } else {
                prof.relax_par_est_wall_ns += par_wall;
            }
#endif
            // Gather local_k buffers into K
            size_t total_k = 0;
            for (const auto& buf : thread_k_buffers) {
                total_k += buf.size();
            }
            K.clear();
            if (total_k + miniS.size() > K.capacity()) {
                K.reserve(total_k + miniS.size());
            }
            for (const auto& buf : thread_k_buffers) {
                K.insert(K.end(), buf.begin(), buf.end());
            }
        } else {
#ifdef BMSSP_ENABLE_PROFILING
            prof.relax_seq_calls++;
            prof.relax_seq_edges += total_edges;
            const uint64_t seq_rel_t0 = prof_now_ns();
#endif
            K.clear();
            auto& local_d = thread_d_buffers[0];
            local_d.clear();
            if (total_edges > 0) {
                for (uint32_t u : nw_complete) {
                    const double du = ctx.states[u].load_dist();
                    if (du >= B) {
                        continue;
                    }
                    for (uint64_t e = ctx.graph.edge_begin(u); e < ctx.graph.edge_end(u); ++e) {
                        relax_outgoing_edge(
                            ctx, level, B, trying_B, complete_B,
                            u, du, e, local_d, K
                        );
                    }
                }
            }
#ifdef BMSSP_ENABLE_PROFILING
            const uint64_t seq_ins_t0 = prof_now_ns();
            prof.relax_seq_ns += (seq_ins_t0 - seq_rel_t0);
            prof.insert_batch_items += local_d.size();
#endif
            if (batch_size == 1) {
                D->insert_batch(local_d);
            } else {
                flush_local_d_to_frontier(local_d, ctx.states, D, 0, 1);
            }
#ifdef BMSSP_ENABLE_PROFILING
            prof.insert_batch_seq_ns += (prof_now_ns() - seq_ins_t0);
#endif
        }

        // Return remaining unfinished miniS vertices into K or D
        for (uint32_t x : miniS) {
            if (!ctx.global_completed[x]) {
                const double dx = ctx.states[x].load_dist();
                if (dx < B) {
                    if (dx >= trying_B) {
                        D->insert(x, dx);
                    } else {
                        K.push_back(VertexDist{x, dx});
                    }
                }
            }
        }

        if (!K.empty()) {
#ifdef BMSSP_ENABLE_PROFILING
            const uint64_t ksort_t0 = prof_now_ns();
#endif
#if defined(__SANITIZE_THREAD__)
            parallel_sample_sort(K);
#else
            sort_k_openmp(K, max_threads);
#endif
#ifdef BMSSP_ENABLE_PROFILING
            const uint64_t bp_t0 = prof_now_ns();
            prof.k_sort_ns += (bp_t0 - ksort_t0);
            prof.d_batch_prepend_calls++;
#endif
            D->batch_prepend(K);
#ifdef BMSSP_ENABLE_PROFILING
            prof.d_batch_prepend_ns += (prof_now_ns() - bp_t0);
#endif
        }

        last_complete_B = complete_B;
    }

    const double retB = (d_exhausted || D->empty()) ? B : last_complete_B;

    // Collect completed vertices from Bellman-Ford visited set
    for (uint32_t x : bellman_vis) {
        const double dx = ctx.states[x].load_dist();
        if (dx < retB) {
            ctx.global_completed[x] = 1;
            if (try_claim_level(x)) {
                complete.push_back(x);
            }
        }
    }

    if (use_bitmask) {
        const uint8_t clear_mask = static_cast<uint8_t>(~lvl_bit);
        for (uint32_t u : complete) {
            ctx.level_complete_mask[u] &= clear_mask;
        }
    }

    return {retB, std::move(complete)};
}

void reconstruct_valid_predecessors(
    const CSRGraph& g,
    uint32_t source,
    const std::vector<double>& dist,
    std::vector<uint32_t>& pred
) {
    pred.assign(g.n, NO_PRED);
    if (g.n == 0 || source >= g.n || source >= dist.size() || !std::isfinite(dist[source])) {
        return;
    }

    std::vector<uint32_t> order;
    order.reserve(g.n);
    for (uint32_t u = 0; u < g.n; ++u) {
        if (u < dist.size() && std::isfinite(dist[u])) {
            order.push_back(u);
        }
    }

    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (dist[a] != dist[b]) {
            return dist[a] < dist[b];
        }
        if (a == source) return true;
        if (b == source) return false;
        return a < b;
    });

    std::vector<uint8_t> in_tree(g.n, 0);
    in_tree[source] = 1;
    size_t placed = 1;

    // Pass 1: Exact edge matches processed by distance level with intra-level BFS
    // to resolve zero-weight paths/cycles acyclically regardless of vertex ID ordering.
    std::queue<uint32_t> q;
    for (size_t i = 0; i < order.size();) {
        const double level_dist = dist[order[i]];
        size_t j = i;
        while (j < order.size() && dist[order[j]] == level_dist) {
            const uint32_t u = order[j];
            if (in_tree[u]) {
                q.push(u);
            }
            ++j;
        }

        while (!q.empty()) {
            const uint32_t u = q.front();
            q.pop();
            const double du = dist[u];
            for (uint64_t e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                const uint32_t v = g.edge_dest(e);
                if (v >= g.n || in_tree[v] || !std::isfinite(dist[v])) {
                    continue;
                }
                const double w = g.edge_weight(e);
                if (du + w == dist[v]) {
                    pred[v] = u;
                    in_tree[v] = 1;
                    ++placed;
                    if (dist[v] == level_dist) {
                        q.push(v);
                    }
                }
            }
        }
        i = j;
    }

    // Pass 2: Tolerance fallback (1e-12) if any reachable vertex was not linked via exact arithmetic.
    if (placed < order.size()) {
        std::queue<uint32_t> q_exact;
        std::queue<uint32_t> q_tol;
        for (uint32_t u : order) {
            if (in_tree[u]) {
                q_tol.push(u);
            }
        }

        while (placed < order.size() && (!q_exact.empty() || !q_tol.empty())) {
            const bool use_exact = !q_exact.empty();
            uint32_t u = 0;
            if (use_exact) {
                u = q_exact.front();
                q_exact.pop();
            } else {
                u = q_tol.front();
                q_tol.pop();
            }

            const double du = dist[u];
            for (uint64_t e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                const uint32_t v = g.edge_dest(e);
                if (v >= g.n || in_tree[v] || !std::isfinite(dist[v])) {
                    continue;
                }
                const double w = g.edge_weight(e);
                const double dv = dist[v];
                const bool exact_match = (du + w == dv);
                const double tol = 1e-12 * std::max(1.0, std::abs(dv));
                if (exact_match || (!use_exact && std::abs((du + w) - dv) <= tol)) {
                    pred[v] = u;
                    in_tree[v] = 1;
                    ++placed;
                    q_exact.push(v);
                    q_tol.push(v);
                }
            }
        }
    }
}

SSSPResult run_bmssp_direct(
    const CSRGraph& graph,
    uint32_t source,
    FrontierType frontier_type
) {
    const size_t n = graph.n;
    const int max_threads = std::max(1, omp_get_max_threads());

    std::unique_ptr<AtomicVertexState, AlignedStateDeleter> raw_states(
        static_cast<AtomicVertexState*>(
            ::operator new[](n * sizeof(AtomicVertexState), std::align_val_t{alignof(AtomicVertexState)})
        )
    );
    AtomicVertexState* atomic_states = raw_states.get();

    if (n >= DEFAULT_GRANULARITY_CUTOFF && max_threads > 1) {
        #pragma omp parallel for schedule(static)
        for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(n); ++i) {
            new (&atomic_states[static_cast<size_t>(i)]) AtomicVertexState();
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            new (&atomic_states[i]) AtomicVertexState();
        }
    }
    atomic_states[source].init_raw(0.0, NO_PRED, 0);

    const double lg = std::max(1.0, std::log2(static_cast<double>(n)));
    const size_t k = std::max<size_t>(1, static_cast<size_t>(std::floor(std::cbrt(lg))));
    const size_t t = std::max<size_t>(1, static_cast<size_t>(std::floor(std::pow(lg, 2.0 / 3.0))));
    const size_t max_level = std::max<size_t>(1, static_cast<size_t>(std::ceil(lg / static_cast<double>(t))));

    EpochDeduplicator deduplicator(n);
    BMSSPContext ctx(graph, k, t, max_level, atomic_states, deduplicator, frontier_type);

    std::vector<uint32_t> initial_sources{source};
#ifdef BMSSP_ENABLE_PROFILING
    const uint64_t solve_t0 = prof_now_ns();
#endif
    bmssp_rec(ctx, static_cast<int>(max_level), INF_DIST, initial_sources);
#ifdef BMSSP_ENABLE_PROFILING
    get_global_profile_stats().total_solve_ns += (prof_now_ns() - solve_t0);
#endif

    SSSPResult res;
    res.dist.resize(n);
    res.pred.resize(n);
    if (n >= DEFAULT_GRANULARITY_CUTOFF && max_threads > 1) {
        #pragma omp parallel for schedule(static)
        for (ptrdiff_t i = 0; i < static_cast<ptrdiff_t>(n); ++i) {
            const size_t idx = static_cast<size_t>(i);
            res.dist[idx] = atomic_states[idx].load_dist(std::memory_order_relaxed);
            res.pred[idx] = atomic_states[idx].load_pred(std::memory_order_relaxed);
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            res.dist[i] = atomic_states[i].load_dist(std::memory_order_relaxed);
            res.pred[i] = atomic_states[i].load_pred(std::memory_order_relaxed);
        }
    }
    res.pred[source] = NO_PRED;
    return res;
}

} // anonymous namespace

SSSPResult bmssp_parallel(
    const CSRGraph& original_graph,
    uint32_t source,
    size_t num_threads,
    bool use_gadget_transform,
    FrontierType frontier_type
) {
    SSSPResult res;
    if (original_graph.n == 0 || source >= original_graph.n) {
        res.dist.assign(original_graph.n, INF_DIST);
        res.pred.assign(original_graph.n, NO_PRED);
        return res;
    }

    if (original_graph.n == 1) {
        res.dist.assign(1, 0.0);
        res.pred.assign(1, NO_PRED);
        return res;
    }

    if (num_threads > 0) {
        omp_set_num_threads(static_cast<int>(num_threads));
    }

    if (use_gadget_transform) {
        TransformedGraphInfo info = transform_to_constant_degree(original_graph);
        const uint32_t transformed_source = info.orig_to_transformed_repr[source];
        SSSPResult trans_res = run_bmssp_direct(info.graph, transformed_source, frontier_type);

        res.dist.resize(original_graph.n);
        restore_distances(trans_res.dist, res.dist, info);
        reconstruct_valid_predecessors(original_graph, source, res.dist, res.pred);
        return res;
    }

    return run_bmssp_direct(original_graph, source, frontier_type);
}

} // namespace bmssp
