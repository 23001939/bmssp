#include "data_structure/d0_deque.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bmssp {

namespace {

inline bool cmp_vertex_dist(const VertexDist& a, const VertexDist& b) noexcept {
    if (a.dist != b.dist) return a.dist < b.dist;
    return a.v < b.v;
}

} // namespace

void D0Deque::init(size_t M, double B) {
    M_ = std::max<size_t>(M, 1);
    B_ = B;
    blocks_.clear();
    scratch_.clear();
    approx_size_ = 0;
}

void D0Deque::push_front_sorted_span(std::span<const VertexDist> sorted_items) {
    const size_t n = sorted_items.size();
    if (n == 0) return;

    const size_t num_blocks = (n + M_ - 1) / M_;
    // Push from back to front so the smallest-distance block lands at blocks_.front().
    for (size_t b = num_blocks; b > 0; --b) {
        const size_t idx = b - 1;
        const size_t start = idx * M_;
        const size_t end = std::min(start + M_, n);

        D0Block blk;
        blk.items.assign(sorted_items.begin() + static_cast<std::ptrdiff_t>(start),
                         sorted_items.begin() + static_cast<std::ptrdiff_t>(end));
        blk.upper_bound = blk.items.back().dist;
        approx_size_ += blk.items.size();
        blocks_.push_front(std::move(blk));
    }
}

void D0Deque::batch_prepend(std::span<const VertexDist> K) {
    if (K.empty()) return;

    bool all_valid = true;
    bool is_sorted = true;
    double prev_dist = -INF_DIST;
    size_t valid_count = 0;

    for (const auto& item : K) {
        if (std::isnan(item.dist) || item.dist > B_) {
            all_valid = false;
            continue;
        }
        if (valid_count > 0 && item.dist < prev_dist) {
            is_sorted = false;
        }
        prev_dist = item.dist;
        ++valid_count;
    }

    if (valid_count == 0) return;

    // Fast path: K is already filtered, sorted ascending, and <= existing front min.
    if (all_valid && is_sorted &&
        (blocks_.empty() || K.back().dist <= blocks_.front().items.front().dist)) {
        push_front_sorted_span(K);
        return;
    }

    // Slow/adversarial path: filter valid items, sort if needed, and merge any
    // overlapping front blocks to maintain global ascending block order in D_0.
    scratch_.clear();
    scratch_.reserve(valid_count);
    for (const auto& item : K) {
        if (!std::isnan(item.dist) && item.dist <= B_) {
            scratch_.push_back(item);
        }
    }

    if (!is_sorted) {
        std::sort(scratch_.begin(), scratch_.end(), cmp_vertex_dist);
    }

    const double max_k = scratch_.back().dist;
    bool merged_existing = false;
    while (!blocks_.empty() && blocks_.front().items.front().dist < max_k) {
        D0Block& front = blocks_.front();
        approx_size_ -= front.items.size();
        scratch_.insert(scratch_.end(), front.items.begin(), front.items.end());
        blocks_.pop_front();
        merged_existing = true;
    }

    if (merged_existing) {
        std::sort(scratch_.begin(), scratch_.end(), cmp_vertex_dist);
    }

    push_front_sorted_span(scratch_);
}

size_t D0Deque::drain_valid_up_to(size_t target_count,
                                  const AtomicVertexState* state_arr,
                                  size_t num_vertices,
                                  std::vector<VertexDist>& out,
                                  uint64_t& stale_dropped_counter) {
    size_t added = 0;
    while (!blocks_.empty() && added < target_count) {
        D0Block& front = blocks_.front();
        approx_size_ -= front.items.size();

        for (const auto& item : front.items) {
            if (state_arr != nullptr && item.v < num_vertices) {
                const double best = state_arr[item.v].load(std::memory_order_acquire).dist;
                if (item.dist > best) {
                    ++stale_dropped_counter;
                    continue;
                }
            }
            out.push_back(item);
            ++added;
        }
        blocks_.pop_front();
    }
    return added;
}

void D0Deque::push_front_block(D0Block&& block) {
    if (block.items.empty()) return;

    if (block.items.size() <= M_) {
        if (!std::is_sorted(block.items.begin(), block.items.end(), cmp_vertex_dist)) {
            std::sort(block.items.begin(), block.items.end(), cmp_vertex_dist);
        }
        block.upper_bound = block.items.back().dist;
        approx_size_ += block.items.size();
        blocks_.push_front(std::move(block));
        return;
    }

    if (!std::is_sorted(block.items.begin(), block.items.end(), cmp_vertex_dist)) {
        std::sort(block.items.begin(), block.items.end(), cmp_vertex_dist);
    }
    push_front_sorted_span(block.items);
}

void D0Deque::push_front_sorted(std::span<const VertexDist> sorted_items) {
    if (sorted_items.empty()) return;

    if (blocks_.empty() || sorted_items.back().dist <= blocks_.front().items.front().dist) {
        push_front_sorted_span(sorted_items);
        return;
    }

    // Defensive fallback if remaining items overlap an already-present front block.
    scratch_.assign(sorted_items.begin(), sorted_items.end());
    const double max_item = scratch_.back().dist;
    while (!blocks_.empty() && blocks_.front().items.front().dist < max_item) {
        D0Block& front = blocks_.front();
        approx_size_ -= front.items.size();
        scratch_.insert(scratch_.end(), front.items.begin(), front.items.end());
        blocks_.pop_front();
    }
    std::sort(scratch_.begin(), scratch_.end(), cmp_vertex_dist);
    push_front_sorted_span(scratch_);
}

double D0Deque::peek_front_upper_bound() const noexcept {
    if (blocks_.empty()) return INF_DIST;
    return blocks_.front().upper_bound;
}

double D0Deque::peek_front_min_dist() const noexcept {
    if (blocks_.empty()) return INF_DIST;
    return blocks_.front().items.front().dist;
}

bool D0Deque::empty() const noexcept {
    return blocks_.empty();
}

void D0Deque::clear() noexcept {
    blocks_.clear();
    scratch_.clear();
    approx_size_ = 0;
}

size_t D0Deque::block_count() const noexcept {
    return blocks_.size();
}

size_t D0Deque::approx_size() const noexcept {
    return approx_size_;
}

} // namespace bmssp
