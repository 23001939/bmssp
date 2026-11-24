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

D0Block D0Deque::acquire_block() {
    if (!free_blocks_.empty()) {
        D0Block blk = std::move(free_blocks_.back());
        free_blocks_.pop_back();
        blk.items.clear();
        blk.head = 0;
        blk.upper_bound = INF_DIST;
        return blk;
    }
    return D0Block{};
}

void D0Deque::recycle_front_block() {
    if (blocks_.empty()) return;
    D0Block blk = std::move(blocks_.front());
    blocks_.pop_front();
    blk.items.clear();
    blk.head = 0;
    if (free_blocks_.size() < 256) {
        free_blocks_.push_back(std::move(blk));
    }
}

void D0Deque::init(size_t M, double B) {
    M_ = std::max<size_t>(M, 1);
    B_ = B;
    while (!blocks_.empty()) {
        recycle_front_block();
    }
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

        D0Block blk = acquire_block();
        blk.items.assign(sorted_items.begin() + static_cast<std::ptrdiff_t>(start),
                         sorted_items.begin() + static_cast<std::ptrdiff_t>(end));
        blk.head = 0;
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
        (blocks_.empty() || K.back().dist <= peek_front_min_dist())) {
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
    while (!blocks_.empty() && peek_front_min_dist() < max_k) {
        D0Block& front = blocks_.front();
        const size_t rem = front.items.size() - front.head;
        approx_size_ -= rem;
        scratch_.insert(scratch_.end(),
                        front.items.begin() + static_cast<std::ptrdiff_t>(front.head),
                        front.items.end());
        recycle_front_block();
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
        while (front.head < front.items.size() && added < target_count) {
            const VertexDist item = front.items[front.head++];
            --approx_size_;
            if (state_arr != nullptr && item.v < num_vertices) {
                const double best = state_arr[item.v].load_dist(std::memory_order_acquire);
                if (item.dist > best) {
                    ++stale_dropped_counter;
                    continue;
                }
            }
            out.push_back(item);
            ++added;
        }
        if (front.head >= front.items.size()) {
            recycle_front_block();
        }
    }
    return added;
}

void D0Deque::push_front_block(D0Block&& block) {
    if (block.head >= block.items.size()) return;
    if (block.head > 0) {
        block.items.erase(block.items.begin(),
                          block.items.begin() + static_cast<std::ptrdiff_t>(block.head));
        block.head = 0;
    }

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

    if (blocks_.empty() || sorted_items.back().dist <= peek_front_min_dist()) {
        push_front_sorted_span(sorted_items);
        return;
    }

    // Defensive fallback if remaining items overlap an already-present front block.
    scratch_.assign(sorted_items.begin(), sorted_items.end());
    const double max_item = scratch_.back().dist;
    while (!blocks_.empty() && peek_front_min_dist() < max_item) {
        D0Block& front = blocks_.front();
        const size_t rem = front.items.size() - front.head;
        approx_size_ -= rem;
        scratch_.insert(scratch_.end(),
                        front.items.begin() + static_cast<std::ptrdiff_t>(front.head),
                        front.items.end());
        recycle_front_block();
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
    const D0Block& front = blocks_.front();
    if (front.head >= front.items.size()) return INF_DIST;
    return front.items[front.head].dist;
}

bool D0Deque::empty() const noexcept {
    return blocks_.empty();
}

void D0Deque::clear() noexcept {
    while (!blocks_.empty()) {
        recycle_front_block();
    }
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
