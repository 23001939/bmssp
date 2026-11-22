#pragma once

#ifdef BMSSP_ENABLE_PROFILING
#include "core/profiler.hpp"
#endif

#include "core/types.hpp"
#include "core/memory_pool.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace bmssp {

inline void cpu_pause() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield");
#else
    std::this_thread::yield();
#endif
}

/**
 * @brief Cache-line padded TTAS spinlock using std::atomic_flag + cpu_pause().
 */
struct alignas(CACHE_LINE_SIZE) BlockSpinlock {
    std::atomic_flag flag{};

    void lock() noexcept {
#ifdef BMSSP_ENABLE_PROFILING
        if (!flag.test_and_set(std::memory_order_acquire)) {
            return;
        }
        const uint64_t start = prof_now_ns();
        do {
            while (flag.test(std::memory_order_relaxed)) {
                cpu_pause();
            }
        } while (flag.test_and_set(std::memory_order_acquire));
        auto& stats = get_d1_lock_profile();
        stats.wait_ns.fetch_add(prof_now_ns() - start, std::memory_order_relaxed);
        stats.contended_calls.fetch_add(1, std::memory_order_relaxed);
#else
        while (flag.test_and_set(std::memory_order_acquire)) {
            while (flag.test(std::memory_order_relaxed)) {
                cpu_pause();
            }
        }
#endif
    }

    bool try_lock() noexcept {
        return !flag.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept {
        flag.clear(std::memory_order_release);
    }
};

static_assert(alignof(BlockSpinlock) == CACHE_LINE_SIZE);

/**
 * @brief Concurrent B-Link Skip-List over blocks of size <= M (D_1 in Lemma 3.3).
 *
 * Concurrency invariants:
 * - Traversals in insert() run lock-free from current_max_level_ down to level 0
 *   using acquire/release on upper_bound and forward[lvl].
 * - Level-0 forward[0] serves as the B-Link right-sibling pointer: on block split,
 *   the new right block is linked into curr->forward[0] BEFORE curr->upper_bound
 *   decreases, so concurrent traversers landing on a split block recover in O(1)
 *   by stepping right along forward[0].
 * - Lazy deletion + Eager Cleanup on split avoid multi-lock deadlock while
 *   suppressing split storms on dense graphs.
 * - Non-tail emptied blocks are unlinked by drain_valid_up_to() and reclaimed safely
 *   via Epoch-Based Reclamation (EBR) so lock-free readers never dereference freed nodes.
 */
class D1SkipList {
public:
    static constexpr uint8_t MAX_LEVEL = 16;
    static constexpr size_t NUM_POOL_SHARDS = 64;

    struct BlockNode {
        // Traversal metadata grouped before spinlock to minimize false sharing with readers.
        std::atomic<double> upper_bound{INF_DIST};
        std::atomic<bool> retired{false};
        uint8_t level{1};
        uint8_t pool_id{0};
        uint64_t sub_order{0}; // Total-order tie-breaker for equal upper_bound (tower_mtx_ only)
        std::array<std::atomic<BlockNode*>, MAX_LEVEL> forward;

        BlockSpinlock spinlock; // Cache-line aligned: isolates lock from forward[] and items.
        std::vector<VertexDist> items;

        BlockNode() noexcept {
            for (auto& ptr : forward) {
                ptr.store(nullptr, std::memory_order_relaxed);
            }
        }
    };

    struct Stats {
        uint64_t overflow_events{0};
        uint64_t actual_splits{0};
        uint64_t eager_cleanup_saved_splits{0};
        uint64_t stale_rejected_on_insert{0};
    };

    struct DrainResult {
        size_t added{0};
        bool reached_end{false};
    };

private:
    struct alignas(CACHE_LINE_SIZE) PoolShard {
        BlockSpinlock lock;
        ThreadLocalBlockPool<BlockNode, 1024> pool;
    };

    struct RetiredEntry {
        BlockNode* node{nullptr};
        uint64_t unlink_epoch{0};
    };

    static_assert(std::atomic<double>::is_always_lock_free,
                  "std::atomic<double> must be lock-free for concurrent skip-list traversal");

    std::array<PoolShard, NUM_POOL_SHARDS> shards_;
    BlockNode* head_{nullptr};
    std::atomic<int> current_max_level_{1};
    size_t M_{16};
    double B_{INF_DIST};

    // Lock order invariant (strictly acyclic):
    //   drain_mtx_ -> BlockNode::spinlock -> tower_mtx_ (or PoolShard::lock outside tower_mtx_)
    mutable std::mutex drain_mtx_;
    std::mutex tower_mtx_;

    std::vector<RetiredEntry> retired_nodes_;
    std::atomic<size_t> pending_retired_count_{0};

    std::atomic<bool> enable_eager_cleanup_{true};
    std::atomic<uint64_t> overflow_events_{0};
    std::atomic<uint64_t> actual_splits_{0};
    std::atomic<uint64_t> eager_cleanup_saved_splits_{0};
    std::atomic<uint64_t> stale_rejected_on_insert_{0};

    [[nodiscard]] BlockNode* allocate_node(uint8_t pool_id, uint8_t level);
    void deallocate_node(BlockNode* node) noexcept;

    [[nodiscard]] static bool less_order(const BlockNode* node,
                                         double ub,
                                         uint64_t sub_order) noexcept;
    void relabel_ub_run_locked(double ub);
    [[nodiscard]] uint64_t assign_sub_order_at_end_of_ub_locked(double target_ub,
                                                                  const BlockNode* exclude_node);

    [[nodiscard]] BlockNode* find_first_candidate_block(double dist) const noexcept;

    void handle_overflow_locked(BlockNode* curr,
                                uint8_t pool_id,
                                const AtomicVertexState* state_arr,
                                size_t num_vertices);

    void unlink_node_locked(BlockNode* target);
    void collect_reclaimable_locked(std::vector<BlockNode*>& ready_to_free);
    void retire_and_reclaim_batch(const std::vector<BlockNode*>& unlinked_nodes);
    void try_reclaim_retired();
    void destroy_all_locked(bool release_memory = true) noexcept;
    void init_sentinel_and_tail_locked();

public:
    D1SkipList();
    ~D1SkipList() noexcept;

    D1SkipList(const D1SkipList&) = delete;
    D1SkipList& operator=(const D1SkipList&) = delete;
    D1SkipList(D1SkipList&&) = delete;
    D1SkipList& operator=(D1SkipList&&) = delete;

    /**
     * @brief Resets skip list with block limit M and global distance bound B.
     * Caller ensures no concurrent insert/pull is active during init().
     */
    void init(size_t M, double B);

    /**
     * @brief Releases all blocks and pool chunks, then restores fresh initial tail block.
     * Caller ensures no concurrent insert/pull is active during clear().
     */
    void clear();

    /**
     * @brief Thread-safe concurrent insertion with optional fast stale filtering
     * and B-Link split recovery.
     */
    void insert(uint32_t v,
                double dist,
                const AtomicVertexState* state_arr = nullptr,
                size_t num_vertices = 0);

    /**
     * @brief Inserts a pre-sorted batch (used to push back remaining pull candidates)
     * in amortized O(|items| + log |blocks|).
     */
    void insert_batch_sorted(std::span<const VertexDist> sorted_items,
                             const AtomicVertexState* state_arr = nullptr,
                             size_t num_vertices = 0);

    /**
     * @brief Drains front blocks up to `target_count` valid items (skipping stale
     * entries against `state_arr`), unlinking emptied non-tail blocks via EBR.
     */
    DrainResult drain_valid_up_to(size_t target_count,
                                  const AtomicVertexState* state_arr,
                                  size_t num_vertices,
                                  std::vector<VertexDist>& out,
                                  uint64_t& stale_dropped_counter);

    [[nodiscard]] bool empty() const noexcept;

    void set_eager_cleanup(bool enabled) noexcept {
        enable_eager_cleanup_.store(enabled, std::memory_order_relaxed);
    }

    [[nodiscard]] bool eager_cleanup_enabled() const noexcept {
        return enable_eager_cleanup_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] Stats get_stats() const noexcept;
    void reset_stats() noexcept;
};

} // namespace bmssp
