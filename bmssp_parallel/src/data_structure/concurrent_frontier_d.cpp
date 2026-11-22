#include "data_structure/concurrent_frontier_d.hpp"

#include <algorithm>

namespace bmssp {

namespace {

void deduplicate_min_dist(std::vector<VertexDist>& items) {
    if (items.size() <= 1) return;
    std::sort(items.begin(), items.end(), [](const VertexDist& a, const VertexDist& b) noexcept {
        if (a.v != b.v) return a.v < b.v;
        return a.dist < b.dist;
    });
    size_t write = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (write == 0 || items[i].v != items[write - 1].v) {
            items[write++] = items[i];
        }
    }
    items.resize(write);
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
        d1_stats.stale_rejected_on_insert,
        stale_dropped_on_pull_.load(std::memory_order_relaxed),
    };
}

void ConcurrentFrontierD::reset_stats() noexcept {
    d1_.reset_stats();
    stale_dropped_on_pull_.store(0, std::memory_order_relaxed);
}

void ConcurrentFrontierD::init(size_t M, double B) {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    M_ = std::max<size_t>(M, 1);
    B_ = B;
    last_Bi_ = -INF_DIST;
    d0_.init(M_, B_);
    d1_.init(M_, B_);
    cand0_buf_.clear();
    cand1_buf_.clear();
    merged_buf_.clear();
    rem0_buf_.clear();
    rem1_buf_.clear();
}

void ConcurrentFrontierD::insert(uint32_t v, double dist) {
    const AtomicVertexState* arr = state_arr_.load(std::memory_order_acquire);
    const size_t n = num_vertices_.load(std::memory_order_acquire);
    d1_.insert(v, dist, arr, n);
}

void ConcurrentFrontierD::insert_batch(std::span<const VertexDist> items) {
    if (items.empty()) return;
    const AtomicVertexState* arr = state_arr_.load(std::memory_order_acquire);
    const size_t n = num_vertices_.load(std::memory_order_acquire);
    d1_.insert_batch_sorted(items, arr, n);
}

void ConcurrentFrontierD::batch_prepend(std::span<const VertexDist> K) {
    if (K.empty()) return;
    std::lock_guard<std::mutex> lk(pull_mtx_);
    d0_.batch_prepend(K);
}

bool ConcurrentFrontierD::pull(std::vector<uint32_t>& S_i, double& B_i) {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    S_i.clear();

    const AtomicVertexState* arr = state_arr_.load(std::memory_order_acquire);
    const size_t n = num_vertices_.load(std::memory_order_acquire);
    const size_t target = M_ + 1;
    uint64_t pull_stale = 0;

    // Drain up to M_+1 distinct valid vertices from D_0 and D_1 so we both extract
    // the M_ smallest elements and observe the smallest remaining distance (if any).
    cand0_buf_.clear();
    while (!d0_.empty() && cand0_buf_.size() < target) {
        const size_t needed = target - cand0_buf_.size();
        d0_.drain_valid_up_to(needed, arr, n, cand0_buf_, pull_stale);
        deduplicate_min_dist(cand0_buf_);
    }

    cand1_buf_.clear();
    bool d1_reached_end = false;
    while (!d1_reached_end && cand1_buf_.size() < target) {
        const size_t needed = target - cand1_buf_.size();
        const D1SkipList::DrainResult res =
            d1_.drain_valid_up_to(needed, arr, n, cand1_buf_, pull_stale);
        d1_reached_end = res.reached_end;
        deduplicate_min_dist(cand1_buf_);
    }

    if (pull_stale > 0) {
        stale_dropped_on_pull_.fetch_add(pull_stale, std::memory_order_relaxed);
    }

    if (cand0_buf_.empty() && cand1_buf_.empty()) {
        B_i = B_;
        last_Bi_ = B_;
        return false;
    }

    merged_buf_.clear();
    merged_buf_.reserve(cand0_buf_.size() + cand1_buf_.size());
    for (const auto& item : cand0_buf_) {
        merged_buf_.push_back(TaggedCandidate{item.v, item.dist, 0});
    }
    for (const auto& item : cand1_buf_) {
        merged_buf_.push_back(TaggedCandidate{item.v, item.dist, 1});
    }

    // Merge duplicates across D_0 and D_1 keeping minimum distance per vertex.
    std::sort(merged_buf_.begin(), merged_buf_.end(),
              [](const TaggedCandidate& a, const TaggedCandidate& b) noexcept {
                  if (a.v != b.v) return a.v < b.v;
                  if (a.dist != b.dist) return a.dist < b.dist;
                  return a.source < b.source;
              });
    size_t write = 0;
    for (size_t i = 0; i < merged_buf_.size(); ++i) {
        if (write == 0 || merged_buf_[i].v != merged_buf_[write - 1].v) {
            merged_buf_[write++] = merged_buf_[i];
        }
    }
    merged_buf_.resize(write);

    std::sort(merged_buf_.begin(), merged_buf_.end(),
              [](const TaggedCandidate& a, const TaggedCandidate& b) noexcept {
                  if (a.dist != b.dist) return a.dist < b.dist;
                  return a.v < b.v;
              });

    if (merged_buf_.size() <= M_) {
        // Because |A U B| >= max(|A|, |B|), <= M_ implies neither D_0 nor D_1 had
        // un-drained valid vertices remaining: both tiers are now exhausted.
        S_i.reserve(merged_buf_.size());
        for (const auto& item : merged_buf_) {
            S_i.push_back(item.v);
        }
        B_i = B_;
        last_Bi_ = B_;
        return true;
    }

    S_i.reserve(M_);
    for (size_t i = 0; i < M_; ++i) {
        S_i.push_back(merged_buf_[i].v);
    }
    B_i = merged_buf_[M_].dist;
    last_Bi_ = B_i;

    rem0_buf_.clear();
    rem1_buf_.clear();
    for (size_t i = M_; i < merged_buf_.size(); ++i) {
        const TaggedCandidate& tc = merged_buf_[i];
        if (tc.source == 0) {
            rem0_buf_.push_back(VertexDist{tc.v, tc.dist});
        } else {
            rem1_buf_.push_back(VertexDist{tc.v, tc.dist});
        }
    }

    if (!rem0_buf_.empty()) {
        d0_.push_front_sorted(rem0_buf_);
    }
    if (!rem1_buf_.empty()) {
        d1_.insert_batch_sorted(rem1_buf_, arr, n);
    }

    return true;
}

bool ConcurrentFrontierD::empty() const {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    if (!d0_.empty()) return false;
    return d1_.empty();
}

void ConcurrentFrontierD::clear() {
    std::lock_guard<std::mutex> lk(pull_mtx_);
    last_Bi_ = -INF_DIST;
    d0_.clear();
    d1_.clear();
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
