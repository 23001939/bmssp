#include "algorithm/bmssp.hpp"
#include "algorithm/find_pivots.hpp"
#include "algorithm/base_case.hpp"
#include "data_structure/concurrent_frontier_d.hpp"
#include "core/primitives.hpp"
#include <omp.h>
#include <cmath>
#include <memory>
#include <algorithm>

namespace bmssp {

namespace {

struct BMSSPContext {
    const CSRGraph& graph;
    size_t k;
    size_t t;
    size_t max_level;
    AtomicVertexState* states;
    EpochDeduplicator& deduplicator;
    std::vector<bool>& is_completed;
};

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
        auto bc_res = base_case(ctx.graph, B, S[0], ctx.k, ctx.states);
        for (uint32_t u : bc_res.completed) {
            ctx.is_completed[u] = true;
        }
        return {bc_res.new_bound, std::move(bc_res.completed)};
    }

    // Step 1: Find Pivots (Algorithm 1)
    auto [P, bellman_vis] = find_pivots_parallel(
        ctx.graph, B, S, ctx.k, ctx.states, ctx.deduplicator
    );

    // Step 2: Initialize Frontier D (Lemma 3.3)
    size_t batch_size = 1ULL << ((level - 1) * ctx.t);
    batch_size = std::max(batch_size, size_t{1});

    auto D = std::make_unique<MutexFrontierD>();
    D->init(batch_size, B);

    double last_complete_B = B;
    for (uint32_t p : P) {
        double dp = ctx.states[p].load(std::memory_order_relaxed).dist;
        if (dp < B) {
            D->insert(p, dp);
            last_complete_B = std::min(last_complete_B, dp);
        }
    }

    // Step 3: Main Iteration Loop
    size_t quota = ctx.k * (1ULL << (level * ctx.t));
    std::vector<uint32_t> complete;
    complete.reserve(quota + bellman_vis.size());

    int max_threads = omp_get_max_threads();
    std::vector<std::vector<VertexDist>> thread_k_buffers(max_threads);

    while (complete.size() < quota && !D->empty()) {
        std::vector<uint32_t> miniS;
        double trying_B = B;

        if (!D->pull(miniS, trying_B)) {
            break;
        }

        // Recursive call down to level - 1
        auto [complete_B, nw_complete] = bmssp_rec(ctx, level - 1, trying_B, miniS);

        for (uint32_t u : nw_complete) {
            if (!ctx.is_completed[u]) {
                ctx.is_completed[u] = true;
                complete.push_back(u);
            }
        }

        // Relax outgoing edges from nw_complete in parallel
        for (auto& buf : thread_k_buffers) {
            buf.clear();
        }

        size_t total_edges = 0;
        for (uint32_t u : nw_complete) {
            total_edges += ctx.graph.degree(u);
        }

        if (total_edges >= DEFAULT_GRANULARITY_CUTOFF && max_threads > 1) {
            #pragma omp parallel
            {
                int tid = omp_get_thread_num();
                auto& local_k = thread_k_buffers[tid];

                #pragma omp for schedule(guided)
                for (size_t i = 0; i < nw_complete.size(); ++i) {
                    uint32_t u = nw_complete[i];
                    double du = ctx.states[u].load(std::memory_order_relaxed).dist;
                    if (du >= B) continue;

                    for (uint64_t e = ctx.graph.edge_begin(u); e < ctx.graph.edge_end(u); ++e) {
                        uint32_t v = ctx.graph.edge_dest(e);
                        double w = ctx.graph.edge_weight(e);
                        double new_dist = du + w;

                        double curr_v = ctx.states[v].load(std::memory_order_relaxed).dist;
                        if (new_dist <= curr_v && new_dist < B) {
                            if (new_dist < curr_v) {
                                relax_edge_atomic(ctx.states[v], new_dist, u, static_cast<uint32_t>(level));
                            }
                            if (new_dist >= trying_B) {
                                D->insert(v, new_dist);
                            } else {
                                local_k.push_back({v, new_dist});
                            }
                        }
                    }
                }
            }
        } else {
            // Serial relax for small frontiers
            auto& local_k = thread_k_buffers[0];
            for (uint32_t u : nw_complete) {
                double du = ctx.states[u].load(std::memory_order_relaxed).dist;
                if (du >= B) continue;

                for (uint64_t e = ctx.graph.edge_begin(u); e < ctx.graph.edge_end(u); ++e) {
                    uint32_t v = ctx.graph.edge_dest(e);
                    double w = ctx.graph.edge_weight(e);
                    double new_dist = du + w;

                    double curr_v = ctx.states[v].load(std::memory_order_relaxed).dist;
                    if (new_dist <= curr_v && new_dist < B) {
                        if (new_dist < curr_v) {
                            relax_edge_atomic(ctx.states[v], new_dist, u, static_cast<uint32_t>(level));
                        }
                        if (new_dist >= trying_B) {
                            D->insert(v, new_dist);
                        } else {
                            local_k.push_back({v, new_dist});
                        }
                    }
                }
            }
        }

        // Gather local_k buffers into K
        std::vector<VertexDist> K;
        for (const auto& buf : thread_k_buffers) {
            K.insert(K.end(), buf.begin(), buf.end());
        }

        if (nw_complete.empty()) {
            for (uint32_t x : miniS) {
                if (!ctx.is_completed[x]) {
                    ctx.is_completed[x] = true;
                    complete.push_back(x);
                }
            }
        }

        // Add remaining unfinished miniS vertices into K
        for (uint32_t x : miniS) {
            if (!ctx.is_completed[x]) {
                double dx = ctx.states[x].load(std::memory_order_relaxed).dist;
                if (dx < B) {
                    K.push_back({x, dx});
                }
            }
        }

        if (!K.empty()) {
            parallel_sample_sort(K);
            D->batch_prepend(K);
        }

        last_complete_B = complete_B;
    }

    double retB = D->empty() ? B : last_complete_B;

    // Collect completed vertices from Bellman-Ford visited set
    for (uint32_t x : bellman_vis) {
        double dx = ctx.states[x].load(std::memory_order_relaxed).dist;
        if (!ctx.is_completed[x] && dx < retB) {
            ctx.is_completed[x] = true;
            complete.push_back(x);
        }
    }

    return {retB, complete};
}

} // anonymous namespace

SSSPResult bmssp_parallel(
    const CSRGraph& original_graph,
    uint32_t source,
    size_t num_threads,
    bool use_gadget_transform
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

    // Configure thread count
    if (num_threads > 0) {
        omp_set_num_threads(static_cast<int>(num_threads));
    }

    // Handle optional Gadget Constant-Degree Transformation
    if (use_gadget_transform) {
        TransformedGraphInfo info = transform_to_constant_degree(original_graph);
        const CSRGraph& transformed = info.graph;
        uint32_t transformed_source = info.orig_to_transformed_repr[source];

        const size_t n = transformed.n;
        std::vector<AtomicVertexState> atomic_states(n);
        atomic_states[transformed_source].store(VertexState{0.0, NO_PRED, 0});

        size_t k = 1;
        size_t t = 1;
        size_t max_level = static_cast<size_t>(std::ceil(std::log2(static_cast<double>(n)) / static_cast<double>(t)));
        max_level = std::max(max_level, size_t{1});

        EpochDeduplicator deduplicator(n);
        std::vector<bool> is_completed(n, false);

        BMSSPContext ctx{
            transformed,
            k,
            t,
            max_level,
            atomic_states.data(),
            deduplicator,
            is_completed
        };

        std::vector<uint32_t> initial_sources{transformed_source};
        bmssp_rec(ctx, static_cast<int>(max_level), INF_DIST, initial_sources);

        std::vector<double> transformed_dist(n);
        for (size_t i = 0; i < n; ++i) {
            transformed_dist[i] = atomic_states[i].load(std::memory_order_relaxed).dist;
        }

        res.dist.resize(original_graph.n);
        res.pred.assign(original_graph.n, NO_PRED);
        restore_distances(transformed_dist, res.dist, info);

        for (uint32_t i = 0; i < original_graph.n; ++i) {
            uint32_t repr = info.orig_to_transformed_repr[i];
            uint32_t p = atomic_states[repr].load(std::memory_order_relaxed).pred;
            if (p < n && p < info.transformed_to_orig.size()) {
                res.pred[i] = info.transformed_to_orig[p];
            }
        }
        res.pred[source] = NO_PRED;
        return res;
    }

    // Direct execution on CSR graph
    const size_t n = original_graph.n;
    std::vector<AtomicVertexState> atomic_states(n);
    atomic_states[source].store(VertexState{0.0, NO_PRED, 0});

    size_t k = 1;
    size_t t = 1;
    size_t max_level = static_cast<size_t>(std::ceil(std::log2(static_cast<double>(n)) / static_cast<double>(t)));
    max_level = std::max(max_level, size_t{1});

    EpochDeduplicator deduplicator(n);
    std::vector<bool> is_completed(n, false);

    BMSSPContext ctx{
        original_graph,
        k,
        t,
        max_level,
        atomic_states.data(),
        deduplicator,
        is_completed
    };

    std::vector<uint32_t> initial_sources{source};
    bmssp_rec(ctx, static_cast<int>(max_level), INF_DIST, initial_sources);

    res.dist.resize(n);
    res.pred.resize(n);
    for (size_t i = 0; i < n; ++i) {
        auto st = atomic_states[i].load(std::memory_order_relaxed);
        res.dist[i] = st.dist;
        res.pred[i] = st.pred;
    }
    res.pred[source] = NO_PRED;

    return res;
}

} // namespace bmssp
