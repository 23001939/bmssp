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
 *
 * Thread-safety model:
 * - insert() only touches D_1 and never acquires pull_mtx_, allowing parallel
 *   edge-relaxation workers to insert concurrently with minimal contention.
 * - batch_prepend(), pull(), empty(), init(), and clear() acquire pull_mtx_ to
 *   serialize D_0 mutations and pull boundary extraction/re-insertion.
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

    D0Deque d0_;
    D1SkipList d1_;

    std::atomic<const AtomicVertexState*> state_arr_{nullptr};
    std::atomic<size_t> num_vertices_{0};

    size_t M_{16};
    double B_{INF_DIST};
    double last_Bi_{-INF_DIST};

    mutable std::mutex pull_mtx_;

    std::vector<VertexDist> cand0_buf_;
    std::vector<VertexDist> cand1_buf_;
    std::vector<TaggedCandidate> merged_buf_;
    std::vector<VertexDist> rem0_buf_;
    std::vector<VertexDist> rem1_buf_;

    std::atomic<uint64_t> stale_dropped_on_pull_{0};

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
