#pragma once

#include "types.hpp"
#include <vector>
#include <numeric>
#include <algorithm>
#include <atomic>
#include <span>
#include <omp.h>

namespace bmssp {

/**
 * @brief Thread-safe epoch tracker to eliminate duplicate frontier insertions.
 * Each vertex can enter frontier W_j at most once per relaxation step j.
 */
class EpochDeduplicator {
private:
    std::vector<std::atomic<uint32_t>> visited_epoch_;

public:
    explicit EpochDeduplicator(size_t n = 0) : visited_epoch_(n) {
        for (auto& item : visited_epoch_) {
            item.store(0, std::memory_order_relaxed);
        }
    }

    void resize(size_t n) {
        visited_epoch_ = std::vector<std::atomic<uint32_t>>(n);
        for (auto& item : visited_epoch_) {
            item.store(0, std::memory_order_relaxed);
        }
    }

    void reset() {
        for (auto& item : visited_epoch_) {
            item.store(0, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Attempts to claim vertex v for step target_epoch.
     * @return true if this thread is the unique winner who claimed v for target_epoch, false otherwise.
     */
    inline bool try_claim(uint32_t v, uint32_t target_epoch) noexcept {
        if (v >= visited_epoch_.size()) return false;
        uint32_t old_epoch = visited_epoch_[v].load(std::memory_order_relaxed);
        while (old_epoch < target_epoch) {
            if (visited_epoch_[v].compare_exchange_weak(old_epoch, target_epoch,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief 3-Phase Cache-Blocked Parallel Prefix Sum (Inclusive Scan).
 * In-place computation: arr[i] = sum_{k=0}^i arr[k].
 * Work: O(N), Span: O(N/P + log P).
 */
template <typename T>
void parallel_prefix_scan(std::span<T> arr) {
    const size_t n = arr.size();
    if (n == 0) return;
    if (n < DEFAULT_GRANULARITY_CUTOFF || omp_get_max_threads() <= 1) {
        for (size_t i = 1; i < n; ++i) {
            arr[i] += arr[i - 1];
        }
        return;
    }

    int num_threads = omp_get_max_threads();
    std::vector<T> block_sums(num_threads, 0);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        int p = omp_get_num_threads();

        size_t chunk = (n + p - 1) / p;
        size_t start = std::min(static_cast<size_t>(tid) * chunk, n);
        size_t end = std::min(start + chunk, n);

        // Phase 1: Local prefix scan within chunk
        for (size_t i = start + 1; i < end; ++i) {
            arr[i] += arr[i - 1];
        }

        if (start < end) {
            block_sums[tid] = arr[end - 1];
        }

        #pragma omp barrier

        // Phase 2: Single-thread scan over block sums
        #pragma omp single
        {
            for (int i = 1; i < p; ++i) {
                block_sums[i] += block_sums[i - 1];
            }
        }

        // Phase 3: Add base offset to all elements in chunk
        if (tid > 0 && start < end) {
            T offset = block_sums[tid - 1];
            for (size_t i = start; i < end; ++i) {
                arr[i] += offset;
            }
        }
    }
}

/**
 * @brief Collects thread-local buffers into a single contiguous vector using parallel scan offsets.
 */
template <typename T>
void parallel_gather(const std::vector<std::vector<T>>& thread_buffers, std::vector<T>& output) {
    size_t num_threads = thread_buffers.size();
    if (num_threads == 0) {
        output.clear();
        return;
    }

    std::vector<size_t> offsets(num_threads + 1, 0);
    for (size_t t = 0; t < num_threads; ++t) {
        offsets[t + 1] = offsets[t] + thread_buffers[t].size();
    }

    size_t total_count = offsets.back();
    output.resize(total_count);

    #pragma omp parallel for schedule(static)
    for (size_t t = 0; t < num_threads; ++t) {
        size_t dest_start = offsets[t];
        const auto& buf = thread_buffers[t];
        std::copy(buf.begin(), buf.end(), output.begin() + dest_start);
    }
}

/**
 * @brief Parallel sorting and partitioning for frontier batch K.
 * Sorts array of VertexDist in ascending order of distance.
 */
inline void parallel_sample_sort(std::span<VertexDist> items) {
    const size_t n = items.size();
    if (n < DEFAULT_GRANULARITY_CUTOFF || omp_get_max_threads() <= 1) {
        std::sort(items.begin(), items.end());
        return;
    }

    // Task-based parallel quicksort with serial cutoff
    auto parallel_qsort_rec = [](auto& self, auto first, auto last) -> void {
        auto count = std::distance(first, last);
        if (count <= 1024) {
            std::sort(first, last);
            return;
        }

        // Median-of-three pivot selection
        auto mid = first + count / 2;
        if (*mid < *first) std::iter_swap(first, mid);
        if (*(last - 1) < *first) std::iter_swap(first, last - 1);
        if (*(last - 1) < *mid) std::iter_swap(mid, last - 1);
        auto pivot = *mid;

        // Partition
        auto left = first;
        auto right = last - 1;
        while (true) {
            while (*left < pivot) ++left;
            while (pivot < *right) --right;
            if (left >= right) break;
            std::iter_swap(left, right);
            ++left;
            --right;
        }

        #pragma omp task default(none) firstprivate(first, left) shared(self)
        self(self, first, left);

        #pragma omp task default(none) firstprivate(right, last) shared(self)
        self(self, right + 1, last);
    };

    #pragma omp parallel
    {
        #pragma omp single nowait
        {
            parallel_qsort_rec(parallel_qsort_rec, items.begin(), items.end());
        }
    }
}

} // namespace bmssp
