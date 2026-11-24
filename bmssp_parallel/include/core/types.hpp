#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <cstring>
#include <atomic>

namespace bmssp {

constexpr double INF_DIST = std::numeric_limits<double>::infinity();
constexpr uint32_t NO_PRED = std::numeric_limits<uint32_t>::max();
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t DEFAULT_GRANULARITY_CUTOFF = 2048;

/**
 * @brief 128-bit aligned vertex state struct (16 bytes).
 * Encapsulates distance, predecessor, and relax epoch.
 */
struct alignas(16) VertexState {
    double dist;        // 8 bytes: current best shortest path estimate
    uint32_t pred;      // 4 bytes: predecessor vertex index
    uint32_t epoch;     // 4 bytes: relaxation generation / iteration counter (ABA prevention)

    constexpr bool operator==(const VertexState& other) const noexcept {
        return (dist == other.dist || (std::isnan(dist) && std::isnan(other.dist))) &&
               pred == other.pred &&
               epoch == other.epoch;
    }
};

static_assert(sizeof(VertexState) == 16, "VertexState must be exactly 16 bytes for 128-bit atomic CAS");
static_assert(alignof(VertexState) == 16, "VertexState must be 16-byte aligned");

/**
 * @brief Hardware lock-free 128-bit atomic wrapper around VertexState using x86-64 cmpxchg16b.
 * Bypasses Windows MinGW libatomic limitations to guarantee true hardware lock-free semantics.
 */
class AtomicVertexState {
private:
    alignas(16) VertexState state_;

    static inline bool raw_cmpxchg16b(VertexState* dest, VertexState* expected, VertexState desired) noexcept {
        char result;
        uint64_t des_lo, des_hi;
        std::memcpy(&des_lo, &desired.dist, 8);
        uint64_t pred_64 = desired.pred;
        uint64_t epoch_64 = desired.epoch;
        des_hi = (epoch_64 << 32) | pred_64;

        uint64_t exp_lo, exp_hi;
        std::memcpy(&exp_lo, &expected->dist, 8);
        uint64_t exp_pred_64 = expected->pred;
        uint64_t exp_epoch_64 = expected->epoch;
        exp_hi = (exp_epoch_64 << 32) | exp_pred_64;

        __asm__ __volatile__(
            "lock cmpxchg16b %1\n\t"
            "setz %0"
            : "=q"(result), "+m"(*dest), "+a"(exp_lo), "+d"(exp_hi)
            : "b"(des_lo), "c"(des_hi)
            : "cc", "memory"
        );

        if (!result) {
            std::memcpy(&expected->dist, &exp_lo, 8);
            expected->pred = static_cast<uint32_t>(exp_hi & 0xFFFFFFFF);
            expected->epoch = static_cast<uint32_t>(exp_hi >> 32);
        }
        return result != 0;
    }

public:
    constexpr AtomicVertexState() noexcept : state_{INF_DIST, NO_PRED, 0} {}
    constexpr explicit AtomicVertexState(VertexState initial) noexcept : state_(initial) {}

    AtomicVertexState(const AtomicVertexState&) = delete;
    AtomicVertexState& operator=(const AtomicVertexState&) = delete;

    [[nodiscard]] static constexpr bool is_lock_free() noexcept {
        return true;
    }

    [[nodiscard]] VertexState load(std::memory_order = std::memory_order_relaxed) const noexcept {
        VertexState curr{0.0, 0, 0};
        // Atomic 128-bit load via cmpxchg16b with itself
        auto* mutable_this = const_cast<AtomicVertexState*>(this);
        raw_cmpxchg16b(&mutable_this->state_, &curr, curr);
        return curr;
    }

    /**
     * @brief Fast 64-bit atomic distance load without issuing lock cmpxchg16b.
     * Avoids cache-line ownership invalidation (RFO) on read-only checks.
     */
    [[nodiscard]] double load_dist(std::memory_order order = std::memory_order_relaxed) const noexcept {
        static_assert(offsetof(VertexState, dist) == 0, "VertexState::dist must be at offset 0");
        static_assert(sizeof(double) == 8, "double must be 8 bytes");
        const auto* ptr = reinterpret_cast<const uint64_t*>(&state_.dist);
        uint64_t bits = 0;
        if (order == std::memory_order_acquire || order == std::memory_order_acq_rel ||
            order == std::memory_order_consume) {
            bits = __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
        } else if (order == std::memory_order_seq_cst) {
            bits = __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
        } else {
            bits = __atomic_load_n(ptr, __ATOMIC_RELAXED);
        }
        double dist = 0.0;
        std::memcpy(&dist, &bits, sizeof(dist));
        return dist;
    }

    /**
     * @brief Fast 32-bit atomic predecessor load without issuing lock cmpxchg16b.
     */
    [[nodiscard]] uint32_t load_pred(std::memory_order order = std::memory_order_relaxed) const noexcept {
        static_assert(offsetof(VertexState, pred) == 8, "VertexState::pred must be at offset 8");
        const auto* ptr = &state_.pred;
        if (order == std::memory_order_acquire || order == std::memory_order_acq_rel ||
            order == std::memory_order_consume) {
            return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
        }
        if (order == std::memory_order_seq_cst) {
            return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
        }
        return __atomic_load_n(ptr, __ATOMIC_RELAXED);
    }

    /**
     * @brief Non-locked initialization for exclusive single-writer setup before concurrent phases.
     */
    void init_raw(double dist, uint32_t pred, uint32_t epoch = 0) noexcept {
        state_.dist = dist;
        state_.pred = pred;
        state_.epoch = epoch;
    }

    void store(VertexState desired, std::memory_order = std::memory_order_relaxed) noexcept {
        VertexState curr = load();
        while (!raw_cmpxchg16b(&state_, &curr, desired)) {
            // spin until successful CAS
        }
    }

    bool compare_exchange_weak(VertexState& expected,
                               VertexState desired,
                               std::memory_order = std::memory_order_release,
                               std::memory_order = std::memory_order_relaxed) noexcept {
        return raw_cmpxchg16b(&state_, &expected, desired);
    }

    bool compare_exchange_strong(VertexState& expected,
                                 VertexState desired,
                                 std::memory_order = std::memory_order_release,
                                 std::memory_order = std::memory_order_relaxed) noexcept {
        return raw_cmpxchg16b(&state_, &expected, desired);
    }
};

/**
 * @brief Atomically relaxes a vertex distance and predecessor using hardware 128-bit CAS.
 * Guarantees that distance and predecessor are committed atomically with zero torn writes.
 */
inline bool relax_edge_atomic(AtomicVertexState& target,
                              double new_dist,
                              uint32_t from_u,
                              uint32_t current_epoch) noexcept {
    if (new_dist >= target.load_dist()) {
        return false;
    }
    VertexState curr = target.load(std::memory_order_relaxed);
    while (new_dist < curr.dist) {
        VertexState next;
        next.dist = new_dist;
        next.pred = from_u;
        next.epoch = current_epoch;

        if (target.compare_exchange_weak(curr, next,
                                         std::memory_order_release,
                                         std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Key-value pair for frontier vertices and priority buckets.
 */
struct VertexDist {
    uint32_t v;
    double dist;

    constexpr bool operator<(const VertexDist& other) const noexcept {
        return dist < other.dist;
    }
    constexpr bool operator>(const VertexDist& other) const noexcept {
        return dist > other.dist;
    }
};

} // namespace bmssp
