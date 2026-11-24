#include "data_structure/concurrent_frontier_d.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace bmssp {

namespace {

inline bool m1_cmp_greater(const VertexDist& a, const VertexDist& b) noexcept {
    if (a.dist != b.dist) return a.dist > b.dist;
    return a.v > b.v;
}

inline bool cmp_vdist_asc(const VertexDist& a, const VertexDist& b) noexcept {
    if (a.dist != b.dist) return a.dist < b.dist;
    return a.v < b.v;
}

inline uint32_t hash_u32(uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

} // namespace

ConcurrentFrontierD::ConcurrentFrontierD(const AtomicVertexState* state_arr, size_t num_vertices) {
    set_state_array(state_arr, num_vertices);
}

void ConcurrentFrontierD::set_state_array(const AtomicVertexState* state_arr,
                                          size_t num_vertices) noexcept {
    num_vertices_.store(num_vertices, std::memory_order_release);
    state_arr_.store(state_arr, std::memory_order_release);
}

void ConcurrentFrontierD::set_eager_cleanup(bool enabled) noexcept {
    d1_.set_eager_cleanup(enabled);
}

ConcurrentFrontierD::FrontierStats ConcurrentFrontierD::get_stats() const noexcept {
    const D1SkipList::Stats d1_stats = d1_.get_stats();
    return FrontierStats{
        d1_stats.overflow_events,
        d1_stats.actual_splits,
        d1_stats.eager_cleanup_saved_splits,
        d1_stats.stale_rejected_on_insert + stale_rejected_m1_.load(std::memory_order_relaxed),
        stale_dropped_on_pull_.load(std::memory_order_relaxed),
    };
}

void ConcurrentFrontierD::reset_stats() noexcept {
    d1_.reset_stats();
    stale_rejected_m1_.store(0, std::memory_order_relaxed);
    stale_dropped_on_pull_.store(0, std::memory_order_relaxed);
}

void ConcurrentFrontierD::prepare_dedup_table(size_t min_capacity) {
    const size_t cap = std::bit_ceil(std::max<size_t>(64, min_capacity));
    if (dedup_table_.size() < cap) {
        dedup_table_.assign(cap, DedupSlot{});
        dedup_epoch_ = 1;
        return;
    }
    ++dedup_epoch_;
    if (dedup_epoch_ == 0) {
        for (auto& slot : dedup_table_) {
            slot.epoch = 0;
        }
        dedup_epoch_ = 1;
    }
}

void ConcurrentFrontierD::rehash_dedup_table(size_t new_pow2) {
    const size_t cap = std::bit_ceil(std::max<size_t>(64, new_pow2));
    dedup_table_.assign(cap, DedupSlot{});
    dedup_epoch_ = 1;
    const size_t mask = cap - 1;
    for (uint32_t idx = 0; idx < static_cast<uint32_t>(merged_buf_.size()); ++idx) {
        const auto& tc = merged_buf_[idx];
        size_t pos = static_cast<size_t>(hash_u32(tc.v)) & mask;
        while (dedup_table_[pos].epoch == dedup_epoch_) {
            pos = (pos + 1) & mask;
        }
        dedup_table_[pos] = DedupSlot{dedup_epoch_, tc.v, idx, static_cast<uint8_t>(1U << tc.source)};
    }
}

bool ConcurrentFrontierD::upsert_merged(uint32_t v, double dist, uint8_t source) {
    if (merged_buf_.size() * 2 >= dedup_table_.size()) {
        rehash_dedup_table(dedup_table_.size() * 2);
    }
    const size_t mask = dedup_table_.size() - 1;
    const uint8_t bit = static_cast<uint8_t>(1U << source);
    size_t pos = static_cast<size_t>(hash_u32(v)) & mask;
    while (dedup_table_[pos].epoch == dedup_epoch_) {
        if (dedup_table_[pos].v == v) {
            const bool first_in_source = (dedup_table_[pos].seen_mask & bit) == 0;
            dedup_table_[pos].seen_mask |= bit;
            TaggedCandidate& cur = merged_buf_[dedup_table_[pos].merged_idx];
            if (dist < cur.dist || (dist == cur.dist && source < cur.source)) {
                cur.dist = dist;
                cur.source = source;
            }
            return first_in_source;
        }
        pos = (pos + 1) & mask;
    }
    const uint32_t idx = static_cast<uint32_t>(merged_buf_.size());
    dedup_table_[pos] = DedupSlot{dedup_epoch_, v, idx, bit};
    merged_buf_.push_back(TaggedCandidate{v, dist, source});
    return true;
}

void ConcurrentFrontierD::init(size_t M, double B) {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    M_ = std::max<size_t>(M, 1);
    B_ = B;
    last_Bi_ = -INF_DIST;

    if (M_ == 1) {
        {
            std::lock_guard<BlockSpinlock> mlk(m1_lock_);
            m1_heap_.clear();
        }
        use_m1_heap_.store(true, std::memory_order_release);
        if (!d0_.empty()) {
            d0_.clear();
        }
        if (d1_dirty_) {
            d1_.clear();
            d1_dirty_ = false;
        }
        return;
    }

    if (use_m1_heap_.load(std::memory_order_relaxed)) {
        std::lock_guard<BlockSpinlock> mlk(m1_lock_);
        m1_heap_.clear();
        use_m1_heap_.store(false, std::memory_order_release);
    }

    d0_.init(M_, B_);
    d1_.init(M_, B_);
    d1_dirty_ = true;
    cand0_buf_.clear();
    cand1_buf_.clear();
    merged_buf_.clear();
    rem0_buf_.clear();
    rem1_buf_.clear();
}

void ConcurrentFrontierD::insert(uint32_t v, double dist) {
    const AtomicVertexState* arr = state_arr_.load(std::memory_order_acquire);
    const size_t n = num_vertices_.load(std::memory_order_acquire);

    if (use_m1_heap_.load(std::memory_order_acquire)) {
        if (std::isnan(dist) || dist > B_) {
            return;
        }
        if (arr != nullptr && v < n && dist > arr[v].load_dist(std::memory_order_acquire)) {
            stale_rejected_m1_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        std::lock_guard<BlockSpinlock> mlk(m1_lock_);
        m1_heap_.push_back(VertexDist{v, dist});
        std::push_heap(m1_heap_.begin(), m1_heap_.end(), m1_cmp_greater);
        return;
    }

    d1_.insert(v, dist, arr, n);
}

void ConcurrentFrontierD::insert_batch(std::span<const VertexDist> items) {
    if (items.empty()) return;
    const AtomicVertexState* arr = state_arr_.load(std::memory_order_acquire);
    const size_t n = num_vertices_.load(std::memory_order_acquire);

    if (use_m1_heap_.load(std::memory_order_acquire)) {
        uint64_t stale_cnt = 0;
        std::lock_guard<BlockSpinlock> mlk(m1_lock_);
        for (const auto& item : items) {
            if (std::isnan(item.dist) || item.dist > B_) continue;
            if (arr != nullptr && item.v < n && item.dist > arr[item.v].load_dist(std::memory_order_acquire)) {
                ++stale_cnt;
                continue;
            }
            m1_heap_.push_back(item);
            std::push_heap(m1_heap_.begin(), m1_heap_.end(), m1_cmp_greater);
        }
        if (stale_cnt > 0) {
            stale_rejected_m1_.fetch_add(stale_cnt, std::memory_order_relaxed);
        }
        return;
    }

    d1_.insert_batch_sorted(items, arr, n);
}

void ConcurrentFrontierD::batch_prepend(std::span<const VertexDist> K) {
    if (K.empty()) return;
    std::lock_guard<std::mutex> lk(pull_mtx_);

    if (use_m1_heap_.load(std::memory_order_relaxed)) {
        std::lock_guard<BlockSpinlock> mlk(m1_lock_);
        if (m1_heap_.empty() && K.size() >= 8) {
            m1_heap_.reserve(K.size());
            for (const auto& item : K) {
                if (!std::isnan(item.dist) && item.dist <= B_) {
                    m1_heap_.push_back(item);
                }
            }
            std::make_heap(m1_heap_.begin(), m1_heap_.end(), m1_cmp_greater);
        } else {
            for (const auto& item : K) {
                if (!std::isnan(item.dist) && item.dist <= B_) {
                    m1_heap_.push_back(item);
                    std::push_heap(m1_heap_.begin(), m1_heap_.end(), m1_cmp_greater);
                }
            }
        }
        return;
    }

    d0_.batch_prepend(K);
}

bool ConcurrentFrontierD::pull(std::vector<uint32_t>& S_i, double& B_i) {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    S_i.clear();

    const AtomicVertexState* arr = state_arr_.load(std::memory_order_acquire);
    const size_t n = num_vertices_.load(std::memory_order_acquire);

    if (use_m1_heap_.load(std::memory_order_relaxed)) {
        uint64_t pull_stale = 0;
        bool found = false;
        uint32_t chosen_v = 0;
        {
            std::lock_guard<BlockSpinlock> mlk(m1_lock_);
            while (!m1_heap_.empty()) {
                const VertexDist top = m1_heap_.front();
                std::pop_heap(m1_heap_.begin(), m1_heap_.end(), m1_cmp_greater);
                m1_heap_.pop_back();
                if (arr != nullptr && top.v < n &&
                    top.dist > arr[top.v].load_dist(std::memory_order_acquire)) {
                    ++pull_stale;
                    continue;
                }
                chosen_v = top.v;
                found = true;
                break;
            }

            if (found) {
                while (!m1_heap_.empty()) {
                    const VertexDist top = m1_heap_.front();
                    if (top.v == chosen_v) {
                        std::pop_heap(m1_heap_.begin(), m1_heap_.end(), m1_cmp_greater);
                        m1_heap_.pop_back();
                        continue;
                    }
                    if (arr != nullptr && top.v < n &&
                        top.dist > arr[top.v].load_dist(std::memory_order_acquire)) {
                        std::pop_heap(m1_heap_.begin(), m1_heap_.end(), m1_cmp_greater);
                        m1_heap_.pop_back();
                        ++pull_stale;
                        continue;
                    }
                    break;
                }
                B_i = m1_heap_.empty() ? B_ : m1_heap_.front().dist;
            } else {
                B_i = B_;
            }
        }
        if (pull_stale > 0) {
            stale_dropped_on_pull_.fetch_add(pull_stale, std::memory_order_relaxed);
        }
        last_Bi_ = B_i;
        if (!found) {
            return false;
        }
        S_i.push_back(chosen_v);
        return true;
    }

    const size_t target = M_ + 1;
    uint64_t pull_stale = 0;

    merged_buf_.clear();
    prepare_dedup_table(target * 8);

    // 1. Drain up to M_ + 1 distinct valid vertices from D_0 (globally sorted ascending).
    double d0_max_seen = -INF_DIST;
    while (!d0_.empty() && merged_buf_.size() < target) {
        const size_t needed = target - merged_buf_.size();
        cand0_buf_.clear();
        d0_.drain_valid_up_to(needed, arr, n, cand0_buf_, pull_stale);
        for (const auto& item : cand0_buf_) {
            if (item.dist > d0_max_seen) {
                d0_max_seen = item.dist;
            }
            static_cast<void>(upsert_merged(item.v, item.dist, 0));
        }
    }

    // If D_0 already supplied >= M_ + 1 distinct valid vertices, no item in D_1 strictly
    // above d0_max_seen can affect the top M_ or the (M_ + 1)-th separator B_i.
    const double dist_limit = (merged_buf_.size() >= target) ? d0_max_seen : INF_DIST;

    // 2. Drain up to M_ + 1 distinct valid vertices (<= dist_limit) from D_1.
    size_t d1_distinct = 0;
    bool d1_reached_end = false;
    while (!d1_reached_end && d1_distinct < target) {
        const size_t needed = target - d1_distinct;
        cand1_buf_.clear();
        const D1SkipList::DrainResult res =
            d1_.drain_valid_up_to(needed, arr, n, cand1_buf_, pull_stale, dist_limit);
        d1_reached_end = res.reached_end;
        for (const auto& item : cand1_buf_) {
            if (upsert_merged(item.v, item.dist, 1)) {
                ++d1_distinct;
            }
        }
    }

    if (pull_stale > 0) {
        stale_dropped_on_pull_.fetch_add(pull_stale, std::memory_order_relaxed);
    }

    if (merged_buf_.empty()) {
        B_i = B_;
        last_Bi_ = B_;
        return false;
    }

    if (merged_buf_.size() <= M_) {
        S_i.reserve(merged_buf_.size());
        for (const auto& item : merged_buf_) {
            S_i.push_back(item.v);
        }
        B_i = B_;
        last_Bi_ = B_;
        return true;
    }

    // Linear-time O(|merged|) selection instead of full O(|merged| log |merged|) sort.
    std::nth_element(merged_buf_.begin(),
                     merged_buf_.begin() + static_cast<std::ptrdiff_t>(M_),
                     merged_buf_.end(),
                     [](const TaggedCandidate& a, const TaggedCandidate& b) noexcept {
                         if (a.dist != b.dist) return a.dist < b.dist;
                         return a.v < b.v;
                     });

    S_i.reserve(M_);
    for (size_t i = 0; i < M_; ++i) {
        S_i.push_back(merged_buf_[i].v);
    }
    B_i = merged_buf_[M_].dist;
    last_Bi_ = B_i;

    rem0_buf_.clear();
    rem0_buf_.reserve(merged_buf_.size() - M_);
    for (size_t i = M_; i < merged_buf_.size(); ++i) {
        const TaggedCandidate& tc = merged_buf_[i];
        rem0_buf_.push_back(VertexDist{tc.v, tc.dist});
    }

    if (!rem0_buf_.empty()) {
        if (rem0_buf_.size() > 1) {
            std::sort(rem0_buf_.begin(), rem0_buf_.end(), cmp_vdist_asc);
        }
        d0_.push_front_sorted(rem0_buf_);
    }

    return true;
}

bool ConcurrentFrontierD::empty() const {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    if (use_m1_heap_.load(std::memory_order_relaxed)) {
        const AtomicVertexState* arr = state_arr_.load(std::memory_order_acquire);
        const size_t n = num_vertices_.load(std::memory_order_acquire);
        std::lock_guard<BlockSpinlock> mlk(m1_lock_);
        for (const auto& item : m1_heap_) {
            if (arr == nullptr || item.v >= n ||
                item.dist <= arr[item.v].load_dist(std::memory_order_acquire)) {
                return false;
            }
        }
        return true;
    }
    if (!d0_.empty()) return false;
    return d1_.empty();
}

void ConcurrentFrontierD::clear() {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    last_Bi_ = -INF_DIST;
    if (use_m1_heap_.load(std::memory_order_relaxed)) {
        std::lock_guard<BlockSpinlock> mlk(m1_lock_);
        m1_heap_.clear();
    }
    if (!d0_.empty()) {
        d0_.clear();
    }
    if (d1_dirty_) {
        d1_.clear();
        d1_dirty_ = false;
    }
    cand0_buf_.clear();
    cand1_buf_.clear();
    merged_buf_.clear();
    rem0_buf_.clear();
    rem1_buf_.clear();
}

double ConcurrentFrontierD::last_separator_bound() const noexcept {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    return last_Bi_;
}

} // namespace bmssp
