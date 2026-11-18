#pragma once

#include "core/types.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace bmssp {

/**
 * @brief Sorted block of up to M frontier elements stored in D_0.
 */
struct D0Block {
    std::vector<VertexDist> items;
    double upper_bound{INF_DIST};
};

/**
 * @brief Sequential block deque D_0 (Lemma 3.3).
 *
 * Replaces Treiber Stack to preserve ascending distance ordering across
 * prepended blocks. Each block stores <= M elements sorted ascending by dist.
 * Intended to be externally synchronized (Master thread between barriers, or
 * via pull_mtx_ in ConcurrentFrontierD).
 */
class D0Deque {
private:
    std::deque<D0Block> blocks_;
    std::vector<VertexDist> scratch_;
    size_t M_{16};
    double B_{INF_DIST};
    size_t approx_size_{0};

    void push_front_sorted_span(std::span<const VertexDist> sorted_items);

public:
    D0Deque() = default;
    ~D0Deque() = default;

    D0Deque(const D0Deque&) = delete;
    D0Deque& operator=(const D0Deque&) = delete;
    D0Deque(D0Deque&&) noexcept = default;
    D0Deque& operator=(D0Deque&&) noexcept = default;

    /**
     * @brief Initializes D_0 with block capacity M and global upper bound B.
     */
    void init(size_t M, double B);

    /**
     * @brief Prepends batch K as blocks of size <= M so smallest distances sit at front.
     *
     * Fast O(|K|) path when K is already sorted, valid (<= B_), and its max element
     * is <= existing front min element. Gracefully sorts/merges on unsorted or
     * overlapping adversarial inputs.
     */
    void batch_prepend(std::span<const VertexDist> K);

    /**
     * @brief Drains blocks from front into `out` until at least `target_count` valid
     * elements are collected or D_0 becomes empty.
     *
     * Elements with dist > state_arr[v].dist are lazily discarded and counted in
     * `stale_dropped_counter`.
     */
    size_t drain_valid_up_to(size_t target_count,
                               const AtomicVertexState* state_arr,
                               size_t num_vertices,
                               std::vector<VertexDist>& out,
                               uint64_t& stale_dropped_counter);

    /**
     * @brief Pushes a block onto the front of D_0 (splitting into <= M blocks if needed).
     */
    void push_front_block(D0Block&& block);

    /**
     * @brief Pushes pre-sorted items back onto the front of D_0 in blocks of size <= M.
     */
    void push_front_sorted(std::span<const VertexDist> sorted_items);

    [[nodiscard]] double peek_front_upper_bound() const noexcept;
    [[nodiscard]] double peek_front_min_dist() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;
    [[nodiscard]] size_t block_count() const noexcept;
    [[nodiscard]] size_t approx_size() const noexcept;
};

} // namespace bmssp
