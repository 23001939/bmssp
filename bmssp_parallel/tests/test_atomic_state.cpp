#include "core/types.hpp"

#include <barrier>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using bmssp::AtomicVertexState;
using bmssp::VertexState;

static_assert(sizeof(VertexState) == 16 && alignof(VertexState) == 16);
static_assert(sizeof(AtomicVertexState) == 16 && alignof(AtomicVertexState) == 16);

VertexState state_for_rank(uint32_t rank) {
    return {static_cast<double>(rank), 3 * rank + 1, rank ^ 0xa5a5a5a5u};
}

void print_state(const VertexState& state) {
    std::cerr << "(dist=" << state.dist << ", pred=" << state.pred
              << ", epoch=" << state.epoch << ')';
}

bool verify_relaxation(uint32_t thread_count) {
    constexpr uint32_t rounds = 128;
    const uint32_t initial_rank = rounds * thread_count + 1;
    AtomicVertexState target(state_for_rank(initial_rank));
    std::barrier phase(static_cast<std::ptrdiff_t>(thread_count) + 1);

    std::vector<std::thread> writers;
    writers.reserve(thread_count);
    for (uint32_t writer = 0; writer < thread_count; ++writer) {
        writers.emplace_back([&, writer] {
            for (uint32_t round = 0; round < rounds; ++round) {
                const uint32_t smallest = (rounds - 1 - round) * thread_count + 1;
                const VertexState candidate = state_for_rank(smallest + writer);
                phase.arrive_and_wait();
                bmssp::relax_edge_atomic(target, candidate.dist, candidate.pred, candidate.epoch);
                phase.arrive_and_wait();
            }
        });
    }

    bool passed = true;
    for (uint32_t round = 0; round < rounds; ++round) {
        const uint32_t smallest = (rounds - 1 - round) * thread_count + 1;
        phase.arrive_and_wait();
        const VertexState snapshot = target.load(std::memory_order_acquire);
        phase.arrive_and_wait();
        const VertexState final = target.load(std::memory_order_acquire);
        const VertexState expected = state_for_rank(smallest);

        // During the round the reader may see a writer's update or the previous minimum.
        bool coherent = false;
        for (uint32_t rank = smallest; rank <= smallest + thread_count; ++rank) {
            coherent |= snapshot == state_for_rank(rank);
        }
        if (passed && (!coherent || !(final == expected))) {
            std::cerr << "[FAIL] atomic relaxation threads=" << thread_count << " round=" << round
                      << " coherent_snapshot=" << coherent << " snapshot=";
            print_state(snapshot);
            std::cerr << " expected=";
            print_state(expected);
            std::cerr << " actual=";
            print_state(final);
            std::cerr << '\n';
            passed = false;
        }
    }

    for (auto& writer : writers) {
        writer.join();
    }
    if (passed) {
        std::cout << "[PASS] atomic relaxation with " << thread_count << " writers, " << rounds
                  << " rounds, and acquire snapshots\n";
    }
    return passed;
}

} // namespace

int main() {
    if (!AtomicVertexState::is_lock_free()) {
        std::cerr << "[FAIL] AtomicVertexState must be lock-free\n";
        return EXIT_FAILURE;
    }
    const bool eight_writers = verify_relaxation(8);
    const bool sixteen_writers = verify_relaxation(16);
    return eight_writers && sixteen_writers ? EXIT_SUCCESS : EXIT_FAILURE;
}
