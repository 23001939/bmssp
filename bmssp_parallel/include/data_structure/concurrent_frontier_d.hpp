#pragma once

#include "core/frontier_interface.hpp"
#include "core/types.hpp"
#include "data_structure/d0_deque.hpp"
#include "data_structure/d1_skiplist.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace bmssp {

/**
 * @brief Concurrent tiered frontier data structure D = (D_0, D_1) per Lemma 3.3.
 *
 * - D_0 (D0Deque): sequential block deque for Master-thread batch_prepend(K).
 * - D_1 (D1SkipList): concurrent B-Link Skip-List over blocks supporting lock-free
 *   traversal, block-level spinlocks, lazy deletion, and eager cleanup on split.
 * - Exact M=1 specialization: spinlock-protected flat min-heap avoiding degenerated
 *   1-element skip-list block splits while preserving identical Lemma 3.3 semantics.
 */
class ConcurrentFrontierD : public IFrontierD {
public:
    struct FrontierStats {
        uint64_t overflow_events{0};
        uint64_t actual_splits{0};
        uint64_t eager_cleanup_saved_splits{0};
        uint64_t stale_dropped_on_insert{0};
        uint64_t stale_dropped_on_pull{0};
    };

private:
    struct TaggedCandidate {
        uint32_t v;
        double dist;
        uint8_t source; // 0 = D_0, 1 = D_1
    };

    struct DedupSlot {
        uint32_t epoch{0};
        uint32_t v{0};
        uint32_t merged_idx{0};
        uint8_t seen_mask{0}; // bit 0 = D_0, bit 1 = D_1
    };

    D0Deque d0_;
    D1SkipList d1_;
    bool d1_dirty_{true};

    // Fast exact M = 1 path (capacity-1 blocks = total-order priority queue)
    std::atomic<bool> use_m1_heap_{false};
    mutable BlockSpinlock m1_lock_;
    std::vector<VertexDist> m1_heap_;
    std::atomic<uint64_t> stale_rejected_m1_{0};

    std::atomic<const AtomicVertexState*> state_arr_{nullptr};
    std::atomic<size_t> num_vertices_{0};

    size_t M_{16};
    double B_{INF_DIST};
    double last_Bi_{-INF_DIST};

    mutable std::mutex pull_mtx_;

    std::vector<DedupSlot> dedup_table_;
    uint32_t dedup_epoch_{0};

    std::vector<VertexDist> cand0_buf_;
    std::vector<VertexDist> cand1_buf_;
    std::vector<TaggedCandidate> merged_buf_;
    std::vector<VertexDist> rem0_buf_;
    std::vector<VertexDist> rem1_buf_;

    std::atomic<uint64_t> stale_dropped_on_pull_{0};

    void prepare_dedup_table(size_t min_capacity);
    void rehash_dedup_table(size_t new_pow2);
    bool upsert_merged(uint32_t v, double dist, uint8_t source);

public:
    ConcurrentFrontierD() = default;
    ConcurrentFrontierD(const AtomicVertexState* state_arr, size_t num_vertices);
    ~ConcurrentFrontierD() override = default;

    ConcurrentFrontierD(const ConcurrentFrontierD&) = delete;
    ConcurrentFrontierD& operator=(const ConcurrentFrontierD&) = delete;
    ConcurrentFrontierD(ConcurrentFrontierD&&) = delete;
    ConcurrentFrontierD& operator=(ConcurrentFrontierD&&) = delete;

    void set_state_array(const AtomicVertexState* state_arr, size_t num_vertices) noexcept;
    void set_eager_cleanup(bool enabled) noexcept;

    [[nodiscard]] FrontierStats get_stats() const noexcept;
    void reset_stats() noexcept;

    void init(size_t M, double B) override;
    void insert(uint32_t v, double dist) override;
    void insert_batch(std::span<const VertexDist> items) override;
    void batch_prepend(std::span<const VertexDist> K) override;
    bool pull(std::vector<uint32_t>& S_i, double& B_i) override;
    [[nodiscard]] bool empty() const override;
    void clear() override;

    [[nodiscard]] double last_separator_bound() const noexcept;
};

} // namespace bmssp
