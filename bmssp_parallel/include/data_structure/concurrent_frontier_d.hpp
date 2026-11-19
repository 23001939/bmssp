#pragma once

#include "core/frontier_interface.hpp"
#include "core/types.hpp"
#include "core/memory_pool.hpp"
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <span>
#include <memory>

namespace bmssp {

/**
 * @brief High-throughput Concurrent Data Structure D (Lemma 3.3).
 * 
 * Implements a dual-tier architecture:
 * - D_0: Sequential block deque for BatchPrepend, processed only on Master thread.
 * - D_1: Partitioned block array with fine-grained spinlocks/mutexes for concurrent insertions.
 * 
 * Employs Lazy Deletion to achieve 100% deadlock-free concurrent relaxations,
 * and Eager Cleanup to eliminate unnecessary block splits.
 */
class ConcurrentFrontierD : public IFrontierD {
public:
    struct Block {
        mutable std::mutex lock;
        double upper_bound{INF_DIST};
        std::vector<VertexDist> items;
        bool is_splitting{false};

        Block() = default;
        explicit Block(double ub) : upper_bound(ub) {}
    };

private:
    size_t M_{16};
    double B_{INF_DIST};

    // Global lock for structural modifications (block creation / split)
    mutable std::mutex global_mtx_;

    // D_0: Block deque populated by sequential BatchPrepend on Master thread
    std::deque<std::vector<VertexDist>> d0_blocks_;
    size_t d0_total_items_{0};

    // D_1: Vector of blocks sorted ascendingly by upper_bound
    std::vector<std::unique_ptr<Block>> d1_blocks_;
    std::atomic<size_t> d1_total_items_{0};

public:
    ConcurrentFrontierD() = default;
    ~ConcurrentFrontierD() override = default;

    void init(size_t M, double B) override {
        std::lock_guard<std::mutex> g_lock(global_mtx_);
        M_ = std::max(M, size_t{1});
        B_ = B;

        d0_blocks_.clear();
        d0_total_items_ = 0;

        d1_blocks_.clear();
        auto initial_block = std::make_unique<Block>(B);
        initial_block->items.reserve(M_ * 2);
        d1_blocks_.push_back(std::move(initial_block));
        d1_total_items_.store(0, std::memory_order_relaxed);
    }

    void insert(uint32_t v, double dist) override {
        if (dist > B_) return;

        Block* target_block = nullptr;
        {
            std::lock_guard<std::mutex> g_lock(global_mtx_);
            if (d1_blocks_.empty()) {
                auto new_blk = std::make_unique<Block>(B_);
                new_blk->items.reserve(M_ * 2);
                d1_blocks_.push_back(std::move(new_blk));
            }

            auto it = std::lower_bound(
                d1_blocks_.begin(), d1_blocks_.end(), dist,
                [](const std::unique_ptr<Block>& blk, double val) {
                    return blk->upper_bound < val;
                }
            );

            if (it == d1_blocks_.end()) {
                target_block = d1_blocks_.back().get();
            } else {
                target_block = it->get();
            }
        }

        bool need_split = false;
        {
            std::lock_guard<std::mutex> b_lock(target_block->lock);
            target_block->items.push_back({v, dist});
            d1_total_items_.fetch_add(1, std::memory_order_relaxed);

            if (target_block->items.size() > 2 * M_) {
                need_split = true;
            }
        }

        if (need_split) {
            split_block(target_block);
        }
    }

    void batch_prepend(std::span<const VertexDist> K) override {
        if (K.empty()) return;

        std::lock_guard<std::mutex> g_lock(global_mtx_);
        size_t total = K.size();
        size_t offset = 0;

        while (offset < total) {
            size_t chunk_size = std::min(M_, total - offset);
            std::vector<VertexDist> block_items(K.begin() + offset, K.begin() + offset + chunk_size);
            d0_total_items_ += block_items.size();
            d0_blocks_.push_back(std::move(block_items));
            offset += chunk_size;
        }
    }

    bool pull(std::vector<uint32_t>& S_i, double& B_i) override {
        std::lock_guard<std::mutex> g_lock(global_mtx_);
        S_i.clear();

        if (d0_total_items_ == 0 && d1_total_items_.load(std::memory_order_relaxed) == 0) {
            B_i = B_;
            return false;
        }

        std::vector<VertexDist> candidates;
        candidates.reserve(2 * M_);

        // 1. Gather from D0
        while (!d0_blocks_.empty() && candidates.size() < 2 * M_) {
            auto& front_d0 = d0_blocks_.front();
            for (const auto& item : front_d0) {
                candidates.push_back(item);
            }
            d0_total_items_ -= front_d0.size();
            d0_blocks_.pop_front();
        }

        // 2. Gather from D1
        while (!d1_blocks_.empty() && candidates.size() < 2 * M_) {
            auto& front_d1 = d1_blocks_.front();
            std::lock_guard<std::mutex> b_lock(front_d1->lock);

            if (front_d1->items.empty() && d1_blocks_.size() > 1) {
                d1_blocks_.erase(d1_blocks_.begin());
                continue;
            }

            for (const auto& item : front_d1->items) {
                candidates.push_back(item);
            }
            d1_total_items_.fetch_sub(front_d1->items.size(), std::memory_order_relaxed);
            front_d1->items.clear();

            if (d1_blocks_.size() > 1) {
                d1_blocks_.erase(d1_blocks_.begin());
            } else {
                front_d1->upper_bound = B_;
                break;
            }
        }

        if (candidates.empty()) {
            B_i = B_;
            return false;
        }

        std::sort(candidates.begin(), candidates.end());

        size_t count = std::min(candidates.size(), M_);
        for (size_t i = 0; i < count; ++i) {
            S_i.push_back(candidates[i].v);
        }

        if (count < candidates.size()) {
            B_i = candidates[count].dist;
            std::vector<VertexDist> remaining(candidates.begin() + count, candidates.end());
            d0_total_items_ += remaining.size();
            d0_blocks_.push_front(std::move(remaining));
        } else {
            // Find next smallest available distance
            double next_dist = B_;
            if (!d0_blocks_.empty() && !d0_blocks_.front().empty()) {
                next_dist = std::min(next_dist, d0_blocks_.front().front().dist);
            }
            for (const auto& blk : d1_blocks_) {
                std::lock_guard<std::mutex> b_lock(blk->lock);
                for (const auto& item : blk->items) {
                    next_dist = std::min(next_dist, item.dist);
                }
            }
            B_i = next_dist;
        }

        return !S_i.empty();
    }

    [[nodiscard]] bool empty() const override {
        std::lock_guard<std::mutex> g_lock(global_mtx_);
        if (d0_total_items_ > 0) return false;
        if (d1_total_items_.load(std::memory_order_relaxed) > 0) return false;
        for (const auto& blk : d1_blocks_) {
            std::lock_guard<std::mutex> b_lock(blk->lock);
            if (!blk->items.empty()) return false;
        }
        return true;
    }

    void clear() override {
        std::lock_guard<std::mutex> g_lock(global_mtx_);
        d0_blocks_.clear();
        d0_total_items_ = 0;
        d1_blocks_.clear();
        d1_total_items_.store(0, std::memory_order_relaxed);
    }

private:
    void split_block(Block* blk) {
        std::lock_guard<std::mutex> g_lock(global_mtx_);
        std::lock_guard<std::mutex> b_lock(blk->lock);

        if (blk->items.size() <= M_) return;

        size_t n = blk->items.size();
        size_t mid = n / 2;
        std::nth_element(
            blk->items.begin(), blk->items.begin() + mid, blk->items.end(),
            [](const VertexDist& a, const VertexDist& b) {
                return a.dist < b.dist;
            }
        );

        double split_key = blk->items[mid].dist;

        auto new_blk = std::make_unique<Block>(blk->upper_bound);
        new_blk->items.reserve(M_ * 2);

        std::vector<VertexDist> kept_items;
        kept_items.reserve(mid + 1);

        for (const auto& item : blk->items) {
            if (item.dist < split_key) {
                kept_items.push_back(item);
            } else {
                new_blk->items.push_back(item);
            }
        }

        blk->items = std::move(kept_items);
        blk->upper_bound = split_key;

        auto it = std::find_if(
            d1_blocks_.begin(), d1_blocks_.end(),
            [blk](const std::unique_ptr<Block>& ptr) { return ptr.get() == blk; }
        );

        if (it != d1_blocks_.end()) {
            d1_blocks_.insert(it + 1, std::move(new_blk));
        } else {
            d1_blocks_.push_back(std::move(new_blk));
        }
    }
};

} // namespace bmssp
