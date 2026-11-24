#include "data_structure/d1_skiplist.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace bmssp {

namespace {

// ============================================================================
// Process-wide Epoch-Based Reclamation (EBR) Registry for D1SkipList readers
// ============================================================================

struct alignas(CACHE_LINE_SIZE) ReaderSlot {
    std::atomic<uint64_t> epoch{0}; // 0 => quiescent; >= 1 => pinned at epoch
    std::atomic<bool> in_use{false};
};

struct SlotAssignment {
    ReaderSlot* slot{nullptr};
    size_t index{0};
    bool is_overflow{false};
};

class ReaderEpochRegistry {
public:
    static constexpr size_t MAX_FIXED_SLOTS = 512;

private:
    std::array<ReaderSlot, MAX_FIXED_SLOTS> fixed_slots_{};
    std::atomic<size_t> high_water_{0};
    std::atomic<uint64_t> global_epoch_{1};

    std::mutex overflow_mtx_;
    std::vector<std::unique_ptr<ReaderSlot>> overflow_slots_;

    ReaderEpochRegistry() = default;

public:
    static ReaderEpochRegistry& instance() {
        static ReaderEpochRegistry reg;
        return reg;
    }

    std::atomic<uint64_t>& global_epoch() noexcept {
        return global_epoch_;
    }

    SlotAssignment acquire() {
        for (size_t i = 0; i < MAX_FIXED_SLOTS; ++i) {
            if (!fixed_slots_[i].in_use.load(std::memory_order_relaxed)) {
                bool expected = false;
                if (fixed_slots_[i].in_use.compare_exchange_strong(
                        expected, true, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    size_t hw = high_water_.load(std::memory_order_relaxed);
                    while (hw <= i && !high_water_.compare_exchange_weak(
                                          hw, i + 1, std::memory_order_release, std::memory_order_relaxed)) {
                    }
                    fixed_slots_[i].epoch.store(0, std::memory_order_release);
                    return SlotAssignment{&fixed_slots_[i], i, false};
                }
            }
        }

        std::lock_guard<std::mutex> lk(overflow_mtx_);
        for (size_t i = 0; i < overflow_slots_.size(); ++i) {
            bool expected = false;
            if (overflow_slots_[i]->in_use.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                overflow_slots_[i]->epoch.store(0, std::memory_order_release);
                return SlotAssignment{overflow_slots_[i].get(), MAX_FIXED_SLOTS + i, true};
            }
        }
        auto slot = std::make_unique<ReaderSlot>();
        slot->in_use.store(true, std::memory_order_relaxed);
        slot->epoch.store(0, std::memory_order_relaxed);
        ReaderSlot* raw = slot.get();
        size_t idx = MAX_FIXED_SLOTS + overflow_slots_.size();
        overflow_slots_.push_back(std::move(slot));
        return SlotAssignment{raw, idx, true};
    }

    void release(ReaderSlot* slot) noexcept {
        if (!slot) return;
        slot->epoch.store(0, std::memory_order_release);
        slot->in_use.store(false, std::memory_order_release);
    }

    [[nodiscard]] uint64_t min_active_epoch() {
        // StoreLoad fence pairs with ReaderGuard's seq_cst fence so any active reader
        // that started before global_epoch advancement is observed here.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        uint64_t min_epoch = std::numeric_limits<uint64_t>::max();

        const size_t limit = std::min(high_water_.load(std::memory_order_acquire), MAX_FIXED_SLOTS);
        for (size_t i = 0; i < limit; ++i) {
            const uint64_t se = fixed_slots_[i].epoch.load(std::memory_order_acquire);
            if (se != 0 && se < min_epoch) {
                min_epoch = se;
            }
        }

        // Fast check before touching overflow mutex (overflow is only hit at >512 concurrent threads).
        if (!overflow_slots_empty_hint_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(overflow_mtx_);
            for (const auto& up : overflow_slots_) {
                const uint64_t se = up->epoch.load(std::memory_order_acquire);
                if (se != 0 && se < min_epoch) {
                    min_epoch = se;
                }
            }
        }
        return min_epoch;
    }

    void mark_overflow_present() noexcept {
        overflow_slots_empty_hint_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> overflow_slots_empty_hint_{true};
};

struct ThreadReaderState {
    ReaderEpochRegistry* registry{nullptr};
    ReaderSlot* slot{nullptr};
    uint8_t pool_id{0};
    uint32_t guard_depth{0};
    uint64_t rng_state{0};

    ThreadReaderState() {
        registry = &ReaderEpochRegistry::instance();
        SlotAssignment assigned = registry->acquire();
        slot = assigned.slot;
        if (assigned.is_overflow) {
            registry->mark_overflow_present();
        }
        pool_id = static_cast<uint8_t>(assigned.index % D1SkipList::NUM_POOL_SHARDS);

        uint64_t seed = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        seed ^= (assigned.index + 1ULL) * 0x9E3779B97F4A7C15ULL;
        // SplitMix64 mixing so rng_state is never 0
        seed += 0x9E3779B97F4A7C15ULL;
        seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBULL;
        seed ^= (seed >> 31);
        rng_state = (seed != 0) ? seed : 0x123456789ABCDEFULL;
    }

    ~ThreadReaderState() {
        if (registry && slot) {
            registry->release(slot);
        }
    }

    ThreadReaderState(const ThreadReaderState&) = delete;
    ThreadReaderState& operator=(const ThreadReaderState&) = delete;
};

inline ThreadReaderState& local_reader_state() {
    thread_local ThreadReaderState state;
    return state;
}

class ReaderGuard {
private:
    ThreadReaderState& state_;

public:
    ReaderGuard() noexcept : state_(local_reader_state()) {
        if (state_.guard_depth++ == 0) {
            auto& global_ep = state_.registry->global_epoch();
            uint64_t e = global_ep.load(std::memory_order_relaxed);
            while (true) {
                state_.slot->epoch.store(e, std::memory_order_release);
                // StoreLoad barrier guarantees slot->epoch is visible before any forward[] load.
                std::atomic_thread_fence(std::memory_order_seq_cst);
                const uint64_t e2 = global_ep.load(std::memory_order_acquire);
                if (e2 == e) break;
                e = e2;
            }
        }
    }

    ~ReaderGuard() noexcept {
        if (--state_.guard_depth == 0) {
            state_.slot->epoch.store(0, std::memory_order_release);
        }
    }

    ReaderGuard(const ReaderGuard&) = delete;
    ReaderGuard& operator=(const ReaderGuard&) = delete;

    [[nodiscard]] uint8_t pool_id() const noexcept { return state_.pool_id; }
};

inline uint8_t sample_skip_level() noexcept {
    ThreadReaderState& st = local_reader_state();
    uint64_t x = st.rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    st.rng_state = x;
    uint64_t r = x * 0x2545F4914F6CDD1DULL;

    // p = 0.25 geometric distribution keeps tower overhead low while preserving O(log N) search.
    uint8_t lvl = 1;
    while (lvl < D1SkipList::MAX_LEVEL && (r & 3ULL) == 0ULL) {
        ++lvl;
        r >>= 2;
    }
    return lvl;
}

} // namespace

// ============================================================================
// D1SkipList Implementation
// ============================================================================

D1SkipList::D1SkipList() {
    init_sentinel_and_tail_locked();
}

D1SkipList::~D1SkipList() noexcept {
    destroy_all_locked();
}

D1SkipList::BlockNode* D1SkipList::allocate_node(uint8_t pool_id, uint8_t level) {
    const uint8_t sid = static_cast<uint8_t>(pool_id % NUM_POOL_SHARDS);
    active_shard_mask_.fetch_or(uint64_t{1} << sid, std::memory_order_relaxed);
    PoolShard& shard = shards_[sid];
    BlockNode* raw = nullptr;
    {
        std::lock_guard<BlockSpinlock> lk(shard.lock);
        raw = shard.pool.allocate();
    }
    new (raw) BlockNode();
    raw->level = level;
    raw->pool_id = sid;
    return raw;
}

void D1SkipList::deallocate_node(BlockNode* node) noexcept {
    if (!node) return;
    const uint8_t sid = static_cast<uint8_t>(node->pool_id % NUM_POOL_SHARDS);
    std::destroy_at(node);
    PoolShard& shard = shards_[sid];
    std::lock_guard<BlockSpinlock> lk(shard.lock);
    shard.pool.deallocate(node);
}

bool D1SkipList::less_order(const BlockNode* node, double ub, uint64_t sub_order) noexcept {
    const double nub = node->upper_bound.load(std::memory_order_relaxed);
    if (nub != ub) return nub < ub;
    return node->sub_order < sub_order;
}

void D1SkipList::relabel_ub_run_locked(double ub) {
    // Caller holds tower_mtx_. Locate first node with upper_bound >= ub.
    BlockNode* x = head_;
    const int max_lvl = current_max_level_.load(std::memory_order_relaxed);
    for (int lvl = max_lvl - 1; lvl >= 0; --lvl) {
        BlockNode* next = x->forward[lvl].load(std::memory_order_relaxed);
        while (next != nullptr && next->upper_bound.load(std::memory_order_relaxed) < ub) {
            x = next;
            next = x->forward[lvl].load(std::memory_order_relaxed);
        }
    }

    BlockNode* first = x->forward[0].load(std::memory_order_relaxed);
    size_t count = 0;
    for (BlockNode* p = first; p != nullptr && p->upper_bound.load(std::memory_order_relaxed) == ub;
         p = p->forward[0].load(std::memory_order_relaxed)) {
        ++count;
    }
    if (count == 0) return;

    const uint64_t step = std::numeric_limits<uint64_t>::max() / (count + 2);
    size_t idx = 1;
    for (BlockNode* p = first; p != nullptr && p->upper_bound.load(std::memory_order_relaxed) == ub;
         p = p->forward[0].load(std::memory_order_relaxed)) {
        p->sub_order = idx * step;
        ++idx;
    }
}

uint64_t D1SkipList::assign_sub_order_at_end_of_ub_locked(double target_ub,
                                                           const BlockNode* exclude_node) {
    // Caller holds tower_mtx_. Find last existing node with upper_bound <= target_ub
    // strictly before exclude_node.
    BlockNode* x = head_;
    const int max_lvl = current_max_level_.load(std::memory_order_relaxed);
    for (int lvl = max_lvl - 1; lvl >= 0; --lvl) {
        BlockNode* next = x->forward[lvl].load(std::memory_order_relaxed);
        while (next != nullptr && next != exclude_node &&
               next->upper_bound.load(std::memory_order_relaxed) <= target_ub) {
            x = next;
            next = x->forward[lvl].load(std::memory_order_relaxed);
        }
    }

    if (x != head_ && x->upper_bound.load(std::memory_order_relaxed) == target_ub) {
        constexpr uint64_t kMaxSub = std::numeric_limits<uint64_t>::max();
        if (kMaxSub - x->sub_order < 2) {
            relabel_ub_run_locked(target_ub);
        }
        return x->sub_order + (kMaxSub - x->sub_order) / 2;
    }
    return std::numeric_limits<uint64_t>::max() / 2;
}

void D1SkipList::init_sentinel_and_tail_locked() {
    if (!head_) {
        head_ = allocate_node(0, MAX_LEVEL);
    } else {
        for (auto& ptr : head_->forward) {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
    }
    head_->upper_bound.store(-INF_DIST, std::memory_order_relaxed);
    head_->retired.store(false, std::memory_order_relaxed);
    head_->level = MAX_LEVEL;
    head_->sub_order = 0;

    BlockNode* tail = allocate_node(0, 1);
    tail->upper_bound.store(B_, std::memory_order_relaxed);
    tail->sub_order = std::numeric_limits<uint64_t>::max() / 2;
    head_->forward[0].store(tail, std::memory_order_release);
    current_max_level_.store(1, std::memory_order_release);
}

void D1SkipList::destroy_all_locked(bool release_memory) noexcept {
    if (head_) {
        BlockNode* curr = head_->forward[0].load(std::memory_order_relaxed);
        while (curr != nullptr) {
            BlockNode* next = curr->forward[0].load(std::memory_order_relaxed);
            std::destroy_at(curr);
            curr = next;
        }
        std::destroy_at(head_);
        head_ = nullptr;
    }

    for (BlockNode* node : retired_staging_) {
        std::destroy_at(node);
    }
    retired_staging_.clear();
    drain_unlinked_buf_.clear();
    reclaim_ready_buf_.clear();

    for (auto& entry : retired_nodes_) {
        std::destroy_at(entry.node);
    }
    retired_nodes_.clear();
    pending_retired_count_.store(0, std::memory_order_relaxed);

    if (release_memory) {
        active_shard_mask_.store(0, std::memory_order_relaxed);
        for (auto& shard : shards_) {
            std::lock_guard<BlockSpinlock> lk(shard.lock);
            shard.pool.clear();
        }
    } else {
        uint64_t mask = active_shard_mask_.exchange(0, std::memory_order_relaxed);
        while (mask != 0) {
            const unsigned sid = static_cast<unsigned>(__builtin_ctzll(mask));
            mask &= (mask - 1ULL);
            std::lock_guard<BlockSpinlock> lk(shards_[sid].lock);
            shards_[sid].pool.reset();
        }
    }
    current_max_level_.store(1, std::memory_order_relaxed);
}

void D1SkipList::init(size_t M, double B) {
    std::lock_guard<std::mutex> drain_lk(drain_mtx_);
    std::lock_guard<std::mutex> tower_lk(tower_mtx_);
    destroy_all_locked(false);
    M_ = std::max<size_t>(M, 1);
    B_ = B;
    init_sentinel_and_tail_locked();
}

void D1SkipList::clear() {
    std::lock_guard<std::mutex> drain_lk(drain_mtx_);
    std::lock_guard<std::mutex> tower_lk(tower_mtx_);
    destroy_all_locked(false);
    init_sentinel_and_tail_locked();
}

D1SkipList::BlockNode* D1SkipList::find_first_candidate_block(double dist) const noexcept {
    BlockNode* curr = head_;
    const int max_lvl = current_max_level_.load(std::memory_order_acquire);
    for (int lvl = max_lvl - 1; lvl >= 0; --lvl) {
        BlockNode* next = curr->forward[lvl].load(std::memory_order_acquire);
        while (next != nullptr && next->upper_bound.load(std::memory_order_acquire) < dist) {
            curr = next;
            next = curr->forward[lvl].load(std::memory_order_acquire);
        }
    }
    return curr->forward[0].load(std::memory_order_acquire);
}

void D1SkipList::handle_overflow_locked(BlockNode* curr,
                                        uint8_t pool_id,
                                        const AtomicVertexState* state_arr,
                                        size_t num_vertices) {
    overflow_events_.fetch_add(1, std::memory_order_relaxed);

    // Eager Cleanup: purge stale entries in-place before committing to a block split.
    if (enable_eager_cleanup_.load(std::memory_order_relaxed) && state_arr != nullptr) {
        size_t write_idx = 0;
        for (size_t i = 0; i < curr->items.size(); ++i) {
            const VertexDist& entry = curr->items[i];
            if (entry.v < num_vertices) {
                const double best = state_arr[entry.v].load_dist(std::memory_order_acquire);
                if (entry.dist > best) {
                    continue;
                }
            }
            curr->items[write_idx++] = entry;
        }
        curr->items.resize(write_idx);
        if (curr->items.size() <= M_) {
            eager_cleanup_saved_splits_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    actual_splits_.fetch_add(1, std::memory_order_relaxed);

    const size_t total = curr->items.size();
    const size_t mid = total / 2;

    std::nth_element(curr->items.begin(),
                     curr->items.begin() + static_cast<std::ptrdiff_t>(mid),
                     curr->items.end(),
                     [](const VertexDist& a, const VertexDist& b) noexcept {
                         if (a.dist != b.dist) return a.dist < b.dist;
                         return a.v < b.v;
                     });

    // nth_element does not sort [0, mid), so compute max distance across the left half
    // to ensure every item in left is <= median_ub <= every item in right.
    double median_ub = curr->items[0].dist;
    for (size_t i = 1; i < mid; ++i) {
        if (curr->items[i].dist > median_ub) {
            median_ub = curr->items[i].dist;
        }
    }

    const uint8_t right_level = sample_skip_level();

    BlockNode* right = allocate_node(pool_id, right_level);
    right->items.assign(curr->items.begin() + static_cast<std::ptrdiff_t>(mid), curr->items.end());
    curr->items.resize(mid);

    // Exact B-Link split order under tower_mtx_ (lock order: BlockNode::spinlock -> tower_mtx_):
    //   1) Initialize right->upper_bound = old_ub and right->forward[0] = curr->forward[0].
    //   2) Link curr->forward[0] = right BEFORE lowering curr->upper_bound to median_ub,
    //      so any concurrent lock-free reader sees right via forward[0] when it detects dist > median_ub.
    //   3) Splice right into skip-list towers 1 .. right->level - 1.
    std::lock_guard<std::mutex> tower_lk(tower_mtx_);

    const double old_ub = curr->upper_bound.load(std::memory_order_relaxed);
    const uint64_t old_sub = curr->sub_order;
    BlockNode* old_next = curr->forward[0].load(std::memory_order_relaxed);

    right->upper_bound.store(old_ub, std::memory_order_relaxed);

    if (median_ub < old_ub) {
        right->sub_order = old_sub;
        curr->sub_order = assign_sub_order_at_end_of_ub_locked(median_ub, curr);
    } else {
        // median_ub == old_ub: curr keeps old_sub, right sits immediately after curr in old_ub run.
        uint64_t next_sub = std::numeric_limits<uint64_t>::max();
        if (old_next != nullptr && old_next->upper_bound.load(std::memory_order_relaxed) == old_ub) {
            next_sub = old_next->sub_order;
        }
        if (next_sub - old_sub < 2) {
            relabel_ub_run_locked(old_ub);
            next_sub = (old_next != nullptr && old_next->upper_bound.load(std::memory_order_relaxed) == old_ub)
                           ? old_next->sub_order
                           : std::numeric_limits<uint64_t>::max();
        }
        right->sub_order = curr->sub_order + (next_sub - curr->sub_order) / 2;
    }

    right->forward[0].store(old_next, std::memory_order_release);
    curr->forward[0].store(right, std::memory_order_release);
    curr->upper_bound.store(median_ub, std::memory_order_release);

    if (right_level > 1) {
        std::array<BlockNode*, MAX_LEVEL> preds{};
        BlockNode* x = head_;
        const int max_lvl = current_max_level_.load(std::memory_order_relaxed);

        for (int lvl = max_lvl - 1; lvl >= 0; --lvl) {
            BlockNode* next = x->forward[lvl].load(std::memory_order_relaxed);
            while (next != nullptr && less_order(next, old_ub, right->sub_order)) {
                x = next;
                next = x->forward[lvl].load(std::memory_order_relaxed);
            }
            preds[lvl] = x;
        }
        for (int lvl = max_lvl; lvl < static_cast<int>(right_level); ++lvl) {
            preds[lvl] = head_;
        }

        for (int lvl = 1; lvl < static_cast<int>(right_level); ++lvl) {
            BlockNode* next = preds[lvl]->forward[lvl].load(std::memory_order_relaxed);
            right->forward[lvl].store(next, std::memory_order_release);
            preds[lvl]->forward[lvl].store(right, std::memory_order_release);
        }

        if (static_cast<int>(right_level) > max_lvl) {
            current_max_level_.store(static_cast<int>(right_level), std::memory_order_release);
        }
    }
}

void D1SkipList::insert(uint32_t v,
                        double dist,
                        const AtomicVertexState* state_arr,
                        size_t num_vertices) {
    if (std::isnan(dist) || dist > B_) {
        return;
    }

    if (state_arr != nullptr && v < num_vertices) {
        if (dist > state_arr[v].load_dist(std::memory_order_acquire)) {
            stale_rejected_on_insert_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    ReaderGuard guard;
    const uint8_t pool_id = guard.pool_id();

    BlockNode* curr = find_first_candidate_block(dist);
    while (true) {
        while (curr != nullptr && curr->upper_bound.load(std::memory_order_acquire) < dist) {
            curr = curr->forward[0].load(std::memory_order_acquire);
        }
        if (curr == nullptr) {
            curr = find_first_candidate_block(dist);
            continue;
        }

        curr->spinlock.lock();
        if (curr->retired.load(std::memory_order_relaxed)) {
            BlockNode* next = curr->forward[0].load(std::memory_order_acquire);
            curr->spinlock.unlock();
            curr = (next != nullptr) ? next : find_first_candidate_block(dist);
            continue;
        }

        if (dist > curr->upper_bound.load(std::memory_order_relaxed)) {
            // Concurrent split lowered curr->upper_bound below dist: follow B-Link forward[0].
            BlockNode* next = curr->forward[0].load(std::memory_order_acquire);
            curr->spinlock.unlock();
            curr = (next != nullptr) ? next : find_first_candidate_block(dist);
            continue;
        }

        curr->items.push_back(VertexDist{v, dist});
        if (curr->items.size() > M_) {
            handle_overflow_locked(curr, pool_id, state_arr, num_vertices);
        }
        curr->spinlock.unlock();
        return;
    }
}

void D1SkipList::insert_batch_sorted(std::span<const VertexDist> sorted_items,
                                     const AtomicVertexState* state_arr,
                                     size_t num_vertices) {
    if (sorted_items.empty()) return;

    ReaderGuard guard;
    const uint8_t pool_id = guard.pool_id();

    size_t idx = 0;
    while (idx < sorted_items.size() &&
           (std::isnan(sorted_items[idx].dist) || sorted_items[idx].dist > B_)) {
        ++idx;
    }
    if (idx >= sorted_items.size()) return;

    BlockNode* curr = find_first_candidate_block(sorted_items[idx].dist);
    while (idx < sorted_items.size()) {
        const double target_dist = sorted_items[idx].dist;
        if (std::isnan(target_dist) || target_dist > B_) {
            ++idx;
            continue;
        }

        while (curr != nullptr && curr->upper_bound.load(std::memory_order_acquire) < target_dist) {
            curr = curr->forward[0].load(std::memory_order_acquire);
        }
        if (curr == nullptr) {
            curr = find_first_candidate_block(target_dist);
            continue;
        }

        curr->spinlock.lock();
        if (curr->retired.load(std::memory_order_relaxed) ||
            target_dist > curr->upper_bound.load(std::memory_order_relaxed)) {
            BlockNode* next = curr->forward[0].load(std::memory_order_acquire);
            curr->spinlock.unlock();
            curr = (next != nullptr) ? next : find_first_candidate_block(target_dist);
            continue;
        }

        double cub = curr->upper_bound.load(std::memory_order_relaxed);
        while (idx < sorted_items.size()) {
            const VertexDist& item = sorted_items[idx];
            if (std::isnan(item.dist) || item.dist > B_) {
                ++idx;
                continue;
            }
            if (item.dist > cub) {
                break;
            }
            curr->items.push_back(item);
            ++idx;
            if (curr->items.size() > M_) {
                handle_overflow_locked(curr, pool_id, state_arr, num_vertices);
                cub = curr->upper_bound.load(std::memory_order_relaxed);
            }
        }

        BlockNode* next = curr->forward[0].load(std::memory_order_acquire);
        curr->spinlock.unlock();
        if (idx < sorted_items.size()) {
            curr = (next != nullptr) ? next : find_first_candidate_block(sorted_items[idx].dist);
        }
    }
}

void D1SkipList::unlink_node_locked(BlockNode* target) {
    const double target_ub = target->upper_bound.load(std::memory_order_relaxed);
    const uint64_t target_sub = target->sub_order;
    int max_lvl = current_max_level_.load(std::memory_order_relaxed);

    std::array<BlockNode*, MAX_LEVEL> preds{};
    BlockNode* x = head_;
    for (int lvl = max_lvl - 1; lvl >= 0; --lvl) {
        BlockNode* next = x->forward[lvl].load(std::memory_order_relaxed);
        while (next != nullptr && less_order(next, target_ub, target_sub)) {
            x = next;
            next = x->forward[lvl].load(std::memory_order_relaxed);
        }
        preds[lvl] = x;
    }

    // Unlink from top tower down to level 0; target's own forward pointers remain intact
    // so any reader currently traversing target can still follow forward[0] safely.
    for (int lvl = static_cast<int>(target->level) - 1; lvl >= 0; --lvl) {
        BlockNode* next = preds[lvl]->forward[lvl].load(std::memory_order_relaxed);
        if (next == target) {
            BlockNode* after = target->forward[lvl].load(std::memory_order_relaxed);
            preds[lvl]->forward[lvl].store(after, std::memory_order_release);
        }
    }

    while (max_lvl > 1 &&
           head_->forward[max_lvl - 1].load(std::memory_order_relaxed) == nullptr) {
        --max_lvl;
    }
    current_max_level_.store(max_lvl, std::memory_order_release);
}

void D1SkipList::collect_reclaimable_locked(std::vector<BlockNode*>& ready_to_free) {
    if (retired_nodes_.empty()) return;

    const uint64_t min_active = ReaderEpochRegistry::instance().min_active_epoch();
    size_t write = 0;
    for (size_t i = 0; i < retired_nodes_.size(); ++i) {
        if (retired_nodes_[i].unlink_epoch < min_active) {
            ready_to_free.push_back(retired_nodes_[i].node);
        } else {
            retired_nodes_[write++] = retired_nodes_[i];
        }
    }
    retired_nodes_.resize(write);
    pending_retired_count_.store(retired_nodes_.size(), std::memory_order_release);
}

void D1SkipList::retire_and_reclaim_batch(const std::vector<BlockNode*>& unlinked_nodes) {
    reclaim_ready_buf_.clear();
    {
        std::lock_guard<std::mutex> tower_lk(tower_mtx_);
        const uint64_t unlink_epoch =
            ReaderEpochRegistry::instance().global_epoch().fetch_add(1, std::memory_order_seq_cst);
        for (BlockNode* target : unlinked_nodes) {
            retired_nodes_.push_back(RetiredEntry{target, unlink_epoch});
        }
        pending_retired_count_.store(retired_nodes_.size(), std::memory_order_release);
        collect_reclaimable_locked(reclaim_ready_buf_);
    }

    for (BlockNode* node : reclaim_ready_buf_) {
        deallocate_node(node);
    }
}

void D1SkipList::try_reclaim_retired() {
    if (pending_retired_count_.load(std::memory_order_acquire) == 0) {
        return;
    }
    reclaim_ready_buf_.clear();
    {
        std::lock_guard<std::mutex> tower_lk(tower_mtx_);
        collect_reclaimable_locked(reclaim_ready_buf_);
    }
    for (BlockNode* node : reclaim_ready_buf_) {
        deallocate_node(node);
    }
}

D1SkipList::DrainResult D1SkipList::drain_valid_up_to(size_t target_count,
                                                      const AtomicVertexState* state_arr,
                                                      size_t num_vertices,
                                                      std::vector<VertexDist>& out,
                                                      uint64_t& stale_dropped_counter,
                                                      double dist_limit) {
    std::lock_guard<std::mutex> drain_lk(drain_mtx_);
    DrainResult result{};
    drain_unlinked_buf_.clear();
    bool hit_dist_limit = false;

    {
        ReaderGuard guard;
        BlockNode* curr = head_->forward[0].load(std::memory_order_acquire);
        while (curr != nullptr && result.added < target_count) {
            curr->spinlock.lock();
            if (curr->retired.load(std::memory_order_relaxed)) {
                BlockNode* next = curr->forward[0].load(std::memory_order_acquire);
                curr->spinlock.unlock();
                curr = next;
                continue;
            }

            const double cub = curr->upper_bound.load(std::memory_order_relaxed);
            if (dist_limit < INF_DIST) {
                size_t write = 0;
                for (const auto& item : curr->items) {
                    if (state_arr != nullptr && item.v < num_vertices) {
                        const double best = state_arr[item.v].load_dist(std::memory_order_acquire);
                        if (item.dist > best) {
                            ++stale_dropped_counter;
                            continue;
                        }
                    }
                    if (item.dist <= dist_limit) {
                        out.push_back(item);
                        ++result.added;
                    } else {
                        curr->items[write++] = item;
                    }
                }
                curr->items.resize(write);
            } else {
                for (const auto& item : curr->items) {
                    if (state_arr != nullptr && item.v < num_vertices) {
                        const double best = state_arr[item.v].load_dist(std::memory_order_acquire);
                        if (item.dist > best) {
                            ++stale_dropped_counter;
                            continue;
                        }
                    }
                    out.push_back(item);
                    ++result.added;
                }
                curr->items.clear();
            }

            BlockNode* next = curr->forward[0].load(std::memory_order_acquire);
            const bool stop_here = (result.added >= target_count) || (cub > dist_limit);
            if (curr->items.empty() && next != nullptr && !stop_here) {
                // Mark retired and unlink synchronously under tower_mtx_ before releasing
                // curr->spinlock so no concurrent split of a successor can cross a retired block.
                curr->retired.store(true, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> tower_lk(tower_mtx_);
                    unlink_node_locked(curr);
                }
                drain_unlinked_buf_.push_back(curr);
            }
            curr->spinlock.unlock();

            curr = next;
            if (cub > dist_limit) {
                hit_dist_limit = true;
                break;
            }
        }
        result.reached_end = (curr == nullptr) || hit_dist_limit;
    } // Exit ReaderGuard before checking EBR epochs so this thread does not pin itself.

    if (!drain_unlinked_buf_.empty()) {
        retired_staging_.insert(retired_staging_.end(),
                                drain_unlinked_buf_.begin(),
                                drain_unlinked_buf_.end());
        if (retired_staging_.size() >= 64) {
            retire_and_reclaim_batch(retired_staging_);
            retired_staging_.clear();
        }
    } else if (pending_retired_count_.load(std::memory_order_relaxed) >= 64) {
        try_reclaim_retired();
    }

    return result;
}

bool D1SkipList::empty() const noexcept {
    ReaderGuard guard;
    BlockNode* curr = head_->forward[0].load(std::memory_order_acquire);
    while (curr != nullptr) {
        curr->spinlock.lock();
        const bool is_retired = curr->retired.load(std::memory_order_relaxed);
        const bool has_items = !is_retired && !curr->items.empty();
        BlockNode* next = curr->forward[0].load(std::memory_order_acquire);
        curr->spinlock.unlock();
        if (has_items) {
            return false;
        }
        curr = next;
    }
    return true;
}

D1SkipList::Stats D1SkipList::get_stats() const noexcept {
    return Stats{
        overflow_events_.load(std::memory_order_relaxed),
        actual_splits_.load(std::memory_order_relaxed),
        eager_cleanup_saved_splits_.load(std::memory_order_relaxed),
        stale_rejected_on_insert_.load(std::memory_order_relaxed),
    };
}

void D1SkipList::reset_stats() noexcept {
    overflow_events_.store(0, std::memory_order_relaxed);
    actual_splits_.store(0, std::memory_order_relaxed);
    eager_cleanup_saved_splits_.store(0, std::memory_order_relaxed);
    stale_rejected_on_insert_.store(0, std::memory_order_relaxed);
}

} // namespace bmssp
