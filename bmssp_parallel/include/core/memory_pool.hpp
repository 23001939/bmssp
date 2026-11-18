#pragma once

#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>

namespace bmssp {

/**
 * @brief High-performance Thread-Local Arena / Free-List Memory Pool.
 * Designed to eliminate global heap contention (malloc/free locks)
 * when allocating nodes in concurrent block structures (Lemma 3.3).
 */
template <typename T, size_t ChunkCapacity = 4096>
class ThreadLocalBlockPool {
private:
    union Node {
        alignas(alignof(T)) char storage[sizeof(T)];
        Node* next_free;
    };

    struct Chunk {
        std::unique_ptr<Node[]> data;
        explicit Chunk(size_t cap) : data(std::make_unique<Node[]>(cap)) {}
    };

    std::vector<Chunk> chunks_;
    Node* free_head_{nullptr};
    size_t current_chunk_idx_{0};
    size_t current_slot_{ChunkCapacity}; // Force initial chunk creation

    void allocate_new_chunk() {
        if (current_chunk_idx_ + 1 < chunks_.size()) {
            current_chunk_idx_++;
            current_slot_ = 0;
        } else {
            chunks_.emplace_back(ChunkCapacity);
            current_chunk_idx_ = chunks_.size() - 1;
            current_slot_ = 0;
        }
    }

public:
    ThreadLocalBlockPool() = default;
    ~ThreadLocalBlockPool() = default;

    // Non-copyable, movable
    ThreadLocalBlockPool(const ThreadLocalBlockPool&) = delete;
    ThreadLocalBlockPool& operator=(const ThreadLocalBlockPool&) = delete;
    ThreadLocalBlockPool(ThreadLocalBlockPool&&) noexcept = default;
    ThreadLocalBlockPool& operator=(ThreadLocalBlockPool&&) noexcept = default;

    /**
     * @brief Allocates raw memory for one object T.
     */
    [[nodiscard]] T* allocate() {
        if (free_head_ != nullptr) {
            Node* node = free_head_;
            free_head_ = free_head_->next_free;
            return reinterpret_cast<T*>(node);
        }

        if (current_slot_ >= ChunkCapacity) {
            allocate_new_chunk();
        }

        Node* node = &chunks_[current_chunk_idx_].data[current_slot_++];
        return reinterpret_cast<T*>(node);
    }

    /**
     * @brief Recycles a previously allocated pointer into thread-local free-list.
     */
    void deallocate(T* ptr) noexcept {
        if (!ptr) return;
        auto* node = reinterpret_cast<Node*>(ptr);
        node->next_free = free_head_;
        free_head_ = node;
    }

    /**
     * @brief Resets all allocated chunks without releasing underlying chunk memory.
     */
    void reset() noexcept {
        free_head_ = nullptr;
        current_chunk_idx_ = 0;
        current_slot_ = chunks_.empty() ? ChunkCapacity : 0;
    }

    /**
     * @brief Releases all allocated memory back to OS.
     */
    void clear() noexcept {
        chunks_.clear();
        free_head_ = nullptr;
        current_chunk_idx_ = 0;
        current_slot_ = ChunkCapacity;
    }
};

} // namespace bmssp
