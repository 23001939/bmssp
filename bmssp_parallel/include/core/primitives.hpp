#pragma once

#include "types.hpp"
#include <cstddef>
#include <limits>
#include <vector>
#include <numeric>
#include <algorithm>
#include <atomic>
#include <span>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <omp.h>

namespace bmssp {

/**
 * @brief Thread-safe epoch tracker to eliminate duplicate frontier insertions.
 * Each vertex can enter frontier W_j at most once per relaxation step j.
 */
class EpochDeduplicator {
private:
    std::vector<std::atomic<uint32_t>> visited_epoch_;
    uint32_t current_epoch_{0};

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
        current_epoch_ = 0;
    }

    void reset() {
        for (auto& item : visited_epoch_) {
            item.store(0, std::memory_order_relaxed);
        }
        current_epoch_ = 0;
    }

    /**
     * @brief Allocates a fresh epoch ID, resetting all vertex marks on wraparound.
     */
    uint32_t allocate_epoch() {
        ++current_epoch_;
        if (current_epoch_ == std::numeric_limits<uint32_t>::max()) {
            reset();
            current_epoch_ = 1;
        }
        return current_epoch_;
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

    /**
     * @brief Checks whether vertex v has already been claimed for target_epoch.
     */
    [[nodiscard]] bool is_claimed(uint32_t v, uint32_t target_epoch) const noexcept {
        return v < visited_epoch_.size() &&
               visited_epoch_[v].load(std::memory_order_relaxed) == target_epoch;
    }

    [[nodiscard]] size_t size() const noexcept {
        return visited_epoch_.size();
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
    const int requested = omp_get_max_threads();
    if (n < DEFAULT_GRANULARITY_CUTOFF || requested <= 1) {
        for (size_t i = 1; i < n; ++i) {
            arr[i] += arr[i - 1];
        }
        return;
    }

    // std::thread joins give TSan explicit happens-before edges. libgomp
    // barriers do not, so the previous OpenMP version produced false positives.
    const size_t p = std::min<size_t>(static_cast<size_t>(requested), 32);
    const size_t chunk = (n + p - 1) / p;
    std::vector<T> block_sums(p, T{0});

    auto local_scan = [&](size_t tid) {
        const size_t start = std::min(tid * chunk, n);
        const size_t end = std::min(start + chunk, n);
        for (size_t i = start + 1; i < end; ++i) {
            arr[i] += arr[i - 1];
        }
        if (start < end) block_sums[tid] = arr[end - 1];
    };

    std::vector<std::thread> workers;
    workers.reserve(p);
    for (size_t tid = 0; tid < p; ++tid) workers.emplace_back(local_scan, tid);
    for (auto& w : workers) w.join();

    std::vector<T> offsets(p, T{0});
    T running = T{0};
    for (size_t i = 0; i < p; ++i) {
        offsets[i] = running;
        running += block_sums[i];
    }

    auto add_offset = [&](size_t tid) {
        const size_t start = std::min(tid * chunk, n);
        const size_t end = std::min(start + chunk, n);
        if (tid > 0 && start < end) {
            for (size_t i = start; i < end; ++i) arr[i] += offsets[tid];
        }
    };

    workers.clear();
    workers.reserve(p);
    for (size_t tid = 0; tid < p; ++tid) workers.emplace_back(add_offset, tid);
    for (auto& w : workers) w.join();
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

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t] {
            const size_t dest_start = offsets[t];
            const auto& buf = thread_buffers[t];
            std::copy(buf.begin(), buf.end(), output.begin() + dest_start);
        });
    }
    for (auto& w : workers) w.join();
}

/**
 * @brief Parallel sorting and partitioning for frontier batch K.
 * Sorts array of VertexDist in ascending order of distance.
 */
inline void parallel_sample_sort(std::span<VertexDist> items) {
    using It = std::span<VertexDist>::iterator;
    const size_t n = items.size();
    const int requested_threads = omp_get_max_threads();
    if (n < DEFAULT_GRANULARITY_CUTOFF || requested_threads <= 1) {
        std::sort(items.begin(), items.end());
        return;
    }

    // Quicksort over a shared range queue. std::thread + mutex are used instead
    // of OpenMP tasks because libgomp task bookkeeping is invisible to TSan;
    // this keeps every relevant happens-before edge visible to the detector.
    struct Range { It first; It last; };

    std::deque<Range> queue;
    queue.push_back({items.begin(), items.end()});
    std::mutex queue_mtx;
    std::condition_variable queue_cv;
    std::atomic<size_t> pending{1};
    std::atomic<bool> done{false};

    auto partition = [](const Range& range) {
        auto first = range.first;
        auto last = range.last;
        const auto count = std::distance(first, last);
        if (count <= 1024) {
            std::sort(first, last);
            return std::vector<Range>{};
        }

        auto mid = first + count / 2;
        if (*mid < *first) std::iter_swap(first, mid);
        if (*(last - 1) < *first) std::iter_swap(first, last - 1);
        if (*(last - 1) < *mid) std::iter_swap(mid, last - 1);
        const auto pivot = *mid;

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

        std::vector<Range> children;
        if (left > first) children.push_back({first, left});
        if (right + 1 < last) children.push_back({right + 1, last});
        return children;
    };

    auto worker = [&]() {
        for (;;) {
            Range current;
            {
                std::unique_lock<std::mutex> lock(queue_mtx);
                queue_cv.wait(lock, [&] {
                    return !queue.empty() || done.load(std::memory_order_acquire);
                });
                if (queue.empty()) {
                    if (done.load(std::memory_order_acquire)) return;
                    continue;
                }
                current = queue.front();
                queue.pop_front();
            }

            auto children = partition(current);

            if (!children.empty()) {
                std::lock_guard<std::mutex> lock(queue_mtx);
                for (const auto& child : children) queue.push_back(child);
                pending.fetch_add(children.size(), std::memory_order_relaxed);
            }

            if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                done.store(true, std::memory_order_release);
            }
            queue_cv.notify_all();
        }
    };

    const size_t worker_count = std::min<size_t>(static_cast<size_t>(requested_threads), 32);
    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1);
    for (size_t i = 1; i < worker_count; ++i) {
        workers.emplace_back(worker);
    }
    worker(); // calling thread participates
    for (auto& w : workers) w.join();
}

} // namespace bmssp
