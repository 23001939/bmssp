#include "algorithm/bmssp.hpp"
#include "core/profiler.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <omp.h>
#include <optional>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {

using namespace bmssp;
using Clock = std::chrono::steady_clock;

uint64_t number(std::string_view input) {
    uint64_t value{};
    const auto end = input.data() + input.size();
    const auto [ptr, error] = std::from_chars(input.data(), end, value);
    if (error != std::errc{} || ptr != end) {
        throw std::invalid_argument("Invalid unsigned integer: " + std::string(input));
    }
    return value;
}

CSRGraph erdos_renyi(uint32_t n, uint32_t degree, std::mt19937_64& rng) {
    const uint64_t stride = n - 1;
    const uint64_t possible = n * stride;
    std::optional<std::geometric_distribution<uint64_t>> gap;
    if (degree < stride) {
        gap.emplace(static_cast<double>(degree) / stride);
    }
    std::uniform_int_distribution<int> weight(1, 100);
    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * degree);
    // Bernoulli G(n,p) over directed non-self edges, skipping failed trials.
    for (uint64_t i = 0; i < possible;) {
        const uint64_t skipped = gap ? (*gap)(rng) : 0;
        if (skipped >= possible - i) {
            break;
        }
        i += skipped;
        const auto u = static_cast<uint32_t>(i / stride);
        auto v = static_cast<uint32_t>(i % stride);
        v += v >= u;
        edges.push_back({u, v, static_cast<double>(weight(rng))});
        ++i;
    }
    return CSRGraph::from_edge_list(n, edges);
}

CSRGraph scale_free(uint32_t n, uint32_t degree, std::mt19937_64& rng) {
    std::uniform_int_distribution<int> weight(1, 100);
    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * degree);
    std::vector<uint32_t> urn{0};
    urn.reserve(static_cast<size_t>(n) * (static_cast<size_t>(degree) + 1));
    std::vector<uint32_t> parents;
    parents.reserve(degree);
    for (uint32_t v = 1; v < n; ++v) {
        parents.clear();
        std::uniform_int_distribution<size_t> pick(0, urn.size() - 1);
        while (parents.size() < std::min(v, degree)) {
            const uint32_t parent = urn[pick(rng)];
            if (std::find(parents.begin(), parents.end(), parent) == parents.end()) {
                parents.push_back(parent);
                edges.push_back({parent, v, static_cast<double>(weight(rng))});
            }
        }
        // One count per vertex plus its out-degree gives affine preferential attachment.
        urn.insert(urn.end(), parents.begin(), parents.end());
        urn.push_back(v);
    }
    return CSRGraph::from_edge_list(n, edges);
}

uint64_t graph_fingerprint(const CSRGraph& graph) {
    uint64_t fingerprint = 14695981039346656037ULL;
    for (uint32_t u = 0; u < graph.n; ++u) {
        for (uint64_t e = graph.edge_begin(u); e < graph.edge_end(u); ++e) {
            fingerprint = (fingerprint ^ u) * 1099511628211ULL;
            fingerprint = (fingerprint ^ graph.edge_dest(e)) * 1099511628211ULL;
            fingerprint = (fingerprint ^ std::bit_cast<uint64_t>(graph.edge_weight(e))) * 1099511628211ULL;
        }
    }
    return fingerprint;
}

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double verify_distances(const SSSPResult& expected, const SSSPResult& actual) {
    if (expected.dist.size() != actual.dist.size()) {
        throw std::runtime_error("Wrong distance vector size");
    }
    double max_error = 0.0;
    for (size_t v = 0; v < expected.dist.size(); ++v) {
        if (expected.dist[v] == actual.dist[v]) {
            continue;
        }
        const double error = std::abs(expected.dist[v] - actual.dist[v]);
        if (!(error < 1e-9)) {
            std::cerr << std::setprecision(17) << "vertex=" << v << " expected=" << expected.dist[v]
                      << " actual=" << actual.dist[v] << '\n';
            throw std::runtime_error("Distance differs from Dijkstra");
        }
        max_error = std::max(max_error, error);
    }
    return max_error;
}

} // namespace

int main(int argc, char** argv) try {
    if (argc != 12) {
        std::cerr << "Usage: bench_synthetic er|scale-free n degree seed source threads cutoff cap gadget warmups samples\n";
        return 2;
    }
    const std::string_view family = argv[1];
    const auto n = number(argv[2]);
    const auto degree = number(argv[3]);
    const auto seed = number(argv[4]);
    const auto source = number(argv[5]);
    const auto threads = number(argv[6]);
    const auto cutoff = number(argv[7]);
    const auto cap = number(argv[8]);
    const auto gadget = number(argv[9]);
    const auto warmups = number(argv[10]);
    const auto samples = number(argv[11]);
    if ((family != "er" && family != "scale-free") || n < 2 || n > std::numeric_limits<uint32_t>::max() ||
        degree == 0 || degree >= n || source >= n || threads == 0 || threads > std::numeric_limits<int>::max() ||
        cutoff == 0 || gadget > 1 || samples == 0 || samples > std::numeric_limits<uint64_t>::max() - warmups) {
        throw std::invalid_argument("Invalid benchmark parameters");
    }

    std::mt19937_64 rng(seed);
    const auto generation_start = Clock::now();
    const auto graph = family == "er" ? erdos_renyi(n, degree, rng) : scale_free(n, degree, rng);
    const double generation_ms = elapsed_ms(generation_start);
    const auto fingerprint = graph_fingerprint(graph);
    const auto oracle_start = Clock::now();
    const auto oracle = dijkstra_sequential(graph, source);
    const double oracle_ms = elapsed_ms(oracle_start);
    const auto reachable = std::count_if(oracle.dist.begin(), oracle.dist.end(), [](double d) { return std::isfinite(d); });

    omp_set_dynamic(0);
    omp_set_num_threads(static_cast<int>(threads));
    int team_threads = 1;
    #pragma omp parallel
    {
        #pragma omp master
        team_threads = omp_get_num_threads();
    }
    const int places = omp_get_num_places();
    const BMSSPOptions options{static_cast<size_t>(cutoff), static_cast<size_t>(cap)};
    std::cout << "family,n,m,seed,source,graph_hash,reachable,threads_requested,threads_actual,omp_places,cutoff,block_cap,gadget,sample,"
                 "generation_ms,oracle_ms,transform_ms,solve_ms,restore_ms,total_ms,max_error,fp_parallel_steps,"
                 "relax_parallel_calls,insert_parallel_thread_ms,insert_serial_ms,frontier_pull_ms,"
                 "d1_contended_calls,d1_wait_thread_ms\n";
    std::cout << std::setprecision(12);
    for (uint64_t sample = 0; sample < warmups + samples; ++sample) {
#ifdef BMSSP_ENABLE_PROFILING
        auto& profile = get_global_profile_stats();
        profile.reset();
        auto& lock_profile = get_d1_lock_profile();
        lock_profile.contended_calls.store(0, std::memory_order_relaxed);
        lock_profile.wait_ns.store(0, std::memory_order_relaxed);
#endif
        BMSSPTimings timings;
        const auto start = Clock::now();
        const auto result = bmssp_parallel(graph, source, threads, gadget != 0, FrontierType::Concurrent, options, &timings);
        const double total_ms = elapsed_ms(start);
        const double error = verify_distances(oracle, result);
        if (sample < warmups) {
            continue;
        }
        std::cout << family << ',' << graph.n << ',' << graph.m << ',' << seed << ',' << source << ',' << fingerprint
                  << ',' << reachable << ',' << threads << ',' << team_threads << ',' << places << ',' << cutoff
                  << ',' << cap << ',' << gadget << ',' << (sample - warmups + 1) << ',' << generation_ms
                  << ',' << oracle_ms << ',' << timings.transform_ms << ',' << timings.solve_ms << ','
                  << timings.restore_ms << ',' << total_ms << ',' << error;
#ifdef BMSSP_ENABLE_PROFILING
        std::cout << ',' << profile.fp_par_steps << ',' << profile.relax_par_calls << ','
                  << profile.insert_par_thread_sum_ns / 1e6 << ',' << profile.insert_batch_seq_ns / 1e6
                  << ',' << profile.d_pull_ns / 1e6 << ',' << lock_profile.contended_calls.load()
                  << ',' << lock_profile.wait_ns.load() / 1e6;
#else
        std::cout << ",,,,,,,";
#endif
        std::cout << '\n' << std::flush;
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
