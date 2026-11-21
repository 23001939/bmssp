#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace bmssp {

struct BMSSPProfileStats {
    uint64_t total_solve_ns{0};

    // Algorithm 1: FindPivots
    uint64_t find_pivots_total_ns{0};
    uint64_t fp_relax_par_ns{0};
    uint64_t fp_relax_seq_ns{0};
    uint64_t fp_forest_ns{0};
    uint64_t fp_calls{0};
    uint64_t fp_par_steps{0};
    uint64_t fp_seq_steps{0};
    uint64_t fp_par_edges{0};
    uint64_t fp_seq_edges{0};
    uint64_t fp_par_thread_sum_ns{0};
    uint64_t fp_par_thread_max_ns{0};

    // Algorithm 2: BaseCase
    uint64_t base_case_ns{0};
    uint64_t base_case_calls{0};

    // Data structure D operations
    uint64_t d_init_ns{0};
    uint64_t d_init_calls{0};
    uint64_t d_pull_ns{0};
    uint64_t d_pull_calls{0};
    uint64_t k_sort_ns{0};
    uint64_t d_batch_prepend_ns{0};
    uint64_t d_batch_prepend_calls{0};

    // Algorithm 3: Edge relaxation & insert_batch (non-overlapping wall times)
    uint64_t relax_par_wall_ns{0};       // Wall time of parallel relax + insert_batch region
    uint64_t relax_seq_ns{0};            // Serial relax loop (excluding flush/insert_batch)
    uint64_t insert_batch_seq_ns{0};     // Serial flush_local_d_to_frontier
    uint64_t relax_par_calls{0};
    uint64_t relax_seq_calls{0};
    uint64_t relax_par_edges{0};
    uint64_t relax_seq_edges{0};

    // Per-thread breakdown inside parallel relax + insert_batch region
    uint64_t relax_par_thread_sum_ns{0};
    uint64_t relax_par_thread_max_ns{0};
    uint64_t insert_par_thread_sum_ns{0};
    uint64_t insert_par_thread_max_ns{0};
    uint64_t insert_par_est_wall_ns{0};  // Estimated wall share of insert_batch inside parallel region
    uint64_t relax_par_est_wall_ns{0};   // Estimated wall share of pure edge relax inside parallel region
    uint64_t insert_batch_items{0};

    void reset() noexcept {
        *this = BMSSPProfileStats{};
    }
};

#ifdef BMSSP_ENABLE_PROFILING

inline BMSSPProfileStats& get_global_profile_stats() noexcept {
    static BMSSPProfileStats stats;
    return stats;
}

inline uint64_t prof_now_ns() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

#endif // BMSSP_ENABLE_PROFILING

} // namespace bmssp
