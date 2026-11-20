#include "algorithm/bmssp.hpp"
#include "algorithm/find_pivots.hpp"
#include "algorithm/base_case.hpp"
#include "core/frontier_interface.hpp"
#include "core/primitives.hpp"
#include "data_structure/concurrent_frontier_d.hpp"

#include <omp.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
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

struct BMSSPContext {
    const CSRGraph& graph;
    size_t k;
    size_t t;
    size_t max_level;
    AtomicVertexState* states;
    EpochDeduplicator& deduplicator;
    std::vector<uint8_t> global_completed;
    FrontierType frontier_type;
    FindPivotsWorkspace fp_ws;
    // One EpochDeduplicator per recursion level so a child call's epoch allocations
    // never clobber the parent call's my_complete_epoch.
    std::deque<EpochDeduplicator> local_complete_dedups;

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
          states(st),
          deduplicator(dedup),
          global_completed(g.n, 0),
          frontier_type(ftype),
          fp_ws(g.n) {
        for (size_t lvl = 0; lvl <= max_level; ++lvl) {
            local_complete_dedups.emplace_back(g.n);
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
    IFrontierD* D,
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
                D->insert(v, new_dist);
            } else {
                // Covers both [complete_B, trying_B) and strictly improved < complete_B.
                local_k.push_back(VertexDist{v, new_dist});
            }
        }
    } else if (new_dist == curr_v && !ctx.global_completed[v]) {
        if (new_dist >= trying_B) {
            D->insert(v, new_dist);
        } else if (new_dist >= complete_B) {
            // Algorithm 3: equal-distance uncompleted vertices in [complete_B, trying_B) go to K.
            local_k.push_back(VertexDist{v, new_dist});
        }
    }
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
        if (S.size() == 1) {
            auto bc_res = base_case(ctx.graph, B, S[0], ctx.k, ctx.states);
            for (uint32_t u : bc_res.completed) {
                ctx.global_completed[u] = 1;
            }
            return {bc_res.new_bound, std::move(bc_res.completed)};
        }

        std::vector<uint32_t> sorted_S(S.begin(), S.end());
        std::sort(sorted_S.begin(), sorted_S.end(), [&](uint32_t a, uint32_t b) {
            const double da = ctx.states[a].load_dist();
            const double db = ctx.states[b].load_dist();
            if (da != db) return da < db;
            return a < b;
        });

        auto& dedup0 = ctx.local_complete_dedups[0];
        const uint32_t my_epoch = dedup0.allocate_epoch();
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
        for (const auto& [u, call_bound] : candidates) {
            const double du = ctx.states[u].load_dist();
            if (du < cur_B || (cur_B == call_bound && du == cur_B)) {
                if (dedup0.try_claim(u, my_epoch)) {
                    ctx.global_completed[u] = 1;
                    merged.push_back(u);
                }
            }
        }
        return {cur_B, std::move(merged)};
    }

    // Step 1: Find Pivots (Algorithm 1)
    auto [P, bellman_vis] = find_pivots_parallel(
        ctx.graph, B, S, ctx.k, ctx.states, ctx.deduplicator, &ctx.fp_ws
    );

    auto& level_dedup = ctx.local_complete_dedups[static_cast<size_t>(level)];
    const uint32_t my_complete_epoch = level_dedup.allocate_epoch();

    // Step 2: Initialize Frontier D (Lemma 3.3)
    const size_t shift_batch = static_cast<size_t>(level - 1) * ctx.t;
    const size_t batch_size = std::max<size_t>(1, safe_pow2_capped(shift_batch, ctx.graph.n));

    std::unique_ptr<IFrontierD> D;
    if (ctx.frontier_type == FrontierType::Concurrent) {
        D = std::make_unique<ConcurrentFrontierD>(ctx.states, ctx.graph.n);
    } else {
        D = std::make_unique<MutexFrontierD>();
    }
    D->init(batch_size, B);

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
    complete.reserve(std::min<size_t>(quota, ctx.graph.n));

    const int max_threads = std::max(1, omp_get_max_threads());
    std::vector<std::vector<VertexDist>> thread_k_buffers(static_cast<size_t>(max_threads));
    std::vector<uint32_t> miniS;
    std::vector<uint64_t> edge_prefix;
    std::vector<VertexDist> K;

    // Never truncate the root recursion level by quota while D still holds active vertices.
    const bool is_top_level = (static_cast<size_t>(level) == ctx.max_level);

    while ((is_top_level || complete.size() < quota) && !D->empty()) {
        double trying_B = B;
        if (!D->pull(miniS, trying_B)) {
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
            if (level_dedup.try_claim(u, my_complete_epoch)) {
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
                    if (level_dedup.try_claim(x, my_complete_epoch)) {
                        complete.push_back(x);
                    }
                    nw_complete.push_back(x);
                }
            }
        }

        // Relax outgoing edges from nw_complete using edge-level partitioning
        for (auto& buf : thread_k_buffers) {
            buf.clear();
        }

        edge_prefix.resize(nw_complete.size() + 1);
        edge_prefix[0] = 0;
        for (size_t i = 0; i < nw_complete.size(); ++i) {
            edge_prefix[i + 1] = edge_prefix[i] + ctx.graph.degree(nw_complete[i]);
        }
        const uint64_t total_edges = edge_prefix.back();

        if (total_edges >= DEFAULT_GRANULARITY_CUTOFF && max_threads > 1) {
            #pragma omp parallel
            {
                const int tid = omp_get_thread_num();
                const int nthreads = omp_get_num_threads();
                auto& local_k = thread_k_buffers[static_cast<size_t>(tid)];

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
                                        u, du, e, D.get(), local_k
                                    );
                                }
                            }
                            cur_g = std::min(v_end_g, t_end);
                        }
                        ++vi;
                    }
                }
            }
        } else {
            auto& local_k = thread_k_buffers[0];
            for (uint32_t u : nw_complete) {
                const double du = ctx.states[u].load_dist();
                if (du >= B) {
                    continue;
                }
                for (uint64_t e = ctx.graph.edge_begin(u); e < ctx.graph.edge_end(u); ++e) {
                    relax_outgoing_edge(
                        ctx, level, B, trying_B, complete_B,
                        u, du, e, D.get(), local_k
                    );
                }
            }
        }

        // Gather local_k buffers into K
        K.clear();
        for (const auto& buf : thread_k_buffers) {
            K.insert(K.end(), buf.begin(), buf.end());
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
            parallel_sample_sort(K);
            D->batch_prepend(K);
        }

        last_complete_B = complete_B;
    }

    const double retB = D->empty() ? B : last_complete_B;

    // Collect completed vertices from Bellman-Ford visited set
    for (uint32_t x : bellman_vis) {
        const double dx = ctx.states[x].load_dist();
        if (dx < retB) {
            ctx.global_completed[x] = 1;
            if (level_dedup.try_claim(x, my_complete_epoch)) {
                complete.push_back(x);
            }
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
    std::vector<AtomicVertexState> atomic_states(n);
    atomic_states[source].store(VertexState{0.0, NO_PRED, 0});

    const double lg = std::max(1.0, std::log2(static_cast<double>(n)));
    const size_t k = std::max<size_t>(1, static_cast<size_t>(std::floor(std::cbrt(lg))));
    const size_t t = std::max<size_t>(1, static_cast<size_t>(std::floor(std::pow(lg, 2.0 / 3.0))));
    const size_t max_level = std::max<size_t>(1, static_cast<size_t>(std::ceil(lg / static_cast<double>(t))));

    EpochDeduplicator deduplicator(n);
    BMSSPContext ctx(graph, k, t, max_level, atomic_states.data(), deduplicator, frontier_type);

    std::vector<uint32_t> initial_sources{source};
    bmssp_rec(ctx, static_cast<int>(max_level), INF_DIST, initial_sources);

    SSSPResult res;
    res.dist.resize(n);
    res.pred.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const VertexState st = atomic_states[i].load(std::memory_order_relaxed);
        res.dist[i] = st.dist;
        res.pred[i] = st.pred;
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
