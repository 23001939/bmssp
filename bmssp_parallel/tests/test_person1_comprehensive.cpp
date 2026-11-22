#include "core/types.hpp"
#include "core/graph.hpp"
#include "core/memory_pool.hpp"
#include "core/frontier_interface.hpp"
#include "core/primitives.hpp"
#include "baseline/bmssp_seq.hpp"

#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>
#include <iomanip>
#include <cstdlib>

using namespace bmssp;

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        g_tests_total++; \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            std::exit(1); \
        } else { \
            g_tests_passed++; \
        } \
    } while (0)

// ============================================================================
// TEST SUITE 1: 128-bit Atomic VertexState & Memory Integrity
// ============================================================================
void test_atomic_vertex_state() {
    std::cout << ">>> Running Test Suite 1: 128-bit Atomic VertexState & Memory Integrity..." << std::endl;

    TEST_ASSERT(sizeof(VertexState) == 16, "VertexState size must be 16 bytes");
    TEST_ASSERT(alignof(VertexState) == 16, "VertexState alignment must be 16 bytes");
    TEST_ASSERT(AtomicVertexState::is_lock_free(), "AtomicVertexState must be hardware lock-free");

    // Stress test: 16 threads concurrently relax the same vertex with distinct (dist, pred) pairs
    AtomicVertexState target;
    target.store({1000000.0, NO_PRED, 0}, std::memory_order_relaxed);

    constexpr int NUM_THREADS = 16;
    constexpr int ITERS_PER_THREAD = 10000;
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&target, t]() {
            for (int i = 0; i < ITERS_PER_THREAD; ++i) {
                // Predecessor is tightly coupled with distance: pred = static_cast<uint32_t>(dist * 10)
                double dist = 1000.0 + static_cast<double>(t * ITERS_PER_THREAD + i) * 0.01;
                uint32_t pred = static_cast<uint32_t>(dist * 10);
                relax_edge_atomic(target, dist, pred, 1);
            }
        });
    }

    for (auto& th : threads) th.join();

    VertexState final_state = target.load(std::memory_order_relaxed);
    TEST_ASSERT(final_state.dist <= 1000.0 + 0.1, "Final distance must be the minimum attempted");
    
    // Check torn read/write: pred must correspond exactly to dist
    uint32_t expected_pred = static_cast<uint32_t>(final_state.dist * 10);
    TEST_ASSERT(final_state.pred == expected_pred, "No torn write allowed: pred must strictly match dist");

    std::cout << "    [PASS] 128-bit Atomic CAS & Torn-Write Prevention verified." << std::endl;
}

// ============================================================================
// TEST SUITE 2: CSRGraph & Gadget Constant-Degree Transformation
// ============================================================================
void test_gadget_constant_degree() {
    std::cout << ">>> Running Test Suite 2: CSRGraph & Gadget Constant-Degree Transform..." << std::endl;

    // Create high-degree star-like graph:
    // Vertex 0 has 50 outgoing edges; Vertex 1 has 50 incoming edges
    constexpr uint32_t N = 60;
    std::vector<Edge> edges;
    for (uint32_t v = 2; v < N; ++v) {
        edges.push_back({0, v, 1.5 + v * 0.1});
        edges.push_back({v, 1, 2.0 + v * 0.05});
    }

    CSRGraph original = CSRGraph::from_edge_list(N, edges);
    TEST_ASSERT(original.max_out_degree() >= 50, "Original graph must have high out-degree");

    TransformedGraphInfo info = transform_to_constant_degree(original);
    
    // Verify transformed graph degree bound: max degree <= 2
    TEST_ASSERT(info.graph.max_out_degree() <= 2, "Transformed graph max out-degree must be <= 2");

    // Verify distance preservation:
    // Run Dijkstra on original graph
    SSSPResult orig_res = dijkstra_sequential(original, 0);

    // Run Dijkstra on transformed graph starting from source's representative
    uint32_t transformed_src = info.orig_to_transformed_repr[0];
    SSSPResult trans_res = dijkstra_sequential(info.graph, transformed_src);

    // Restore distances back to original vertices
    std::vector<double> restored_dist;
    restore_distances(trans_res.dist, restored_dist, info);

    // Check exact match on all reachable vertices
    for (uint32_t u = 0; u < N; ++u) {
        double want = orig_res.dist[u];
        double got = restored_dist[u];
        if (std::isinf(want)) {
            TEST_ASSERT(std::isinf(got), "Unreachable vertices must match");
        } else {
            TEST_ASSERT(std::abs(want - got) < 1e-12, "Restored distance must match original within 1e-12");
        }
    }

    std::cout << "    [PASS] Gadget Transformation max degree <= 2 and exact distance restoration verified." << std::endl;
}

// ============================================================================
// TEST SUITE 3: MemoryPool (ThreadLocalBlockPool)
// ============================================================================
void test_memory_pool() {
    std::cout << ">>> Running Test Suite 3: Thread-Local Memory Pool..." << std::endl;

    struct TestItem {
        uint32_t id;
        double val;
    };

    ThreadLocalBlockPool<TestItem, 128> pool;

    std::vector<TestItem*> allocated;
    for (uint32_t i = 0; i < 1000; ++i) {
        TestItem* item = pool.allocate();
        item->id = i;
        item->val = i * 1.5;
        allocated.push_back(item);
    }

    for (uint32_t i = 0; i < 1000; ++i) {
        TEST_ASSERT(allocated[i]->id == i, "Memory pool content integrity");
    }

    // Recycle half the items to free-list
    for (size_t i = 0; i < 500; ++i) {
        pool.deallocate(allocated[i]);
    }

    // Reallocate and ensure stability
    for (size_t i = 0; i < 500; ++i) {
        TestItem* realloc_item = pool.allocate();
        TEST_ASSERT(realloc_item != nullptr, "Reallocated pointer must not be null");
    }

    pool.clear();
    std::cout << "    [PASS] Thread-Local Memory Pool allocation and recycling verified." << std::endl;
}

// ============================================================================
// TEST SUITE 4: MutexFrontierD (Mockup Interface)
// ============================================================================
void test_mutex_frontier() {
    std::cout << ">>> Running Test Suite 4: MutexFrontierD Mockup Interface..." << std::endl;

    MutexFrontierD frontier;
    frontier.init(4, 100.0);
    TEST_ASSERT(frontier.empty(), "Frontier must be empty after init");

    // Insert elements concurrently from multiple threads
    constexpr int NUM_THREADS = 8;
    std::vector<std::thread> workers;
    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back([&frontier, t]() {
            for (int i = 0; i < 20; ++i) {
                frontier.insert(t * 100 + i, 10.0 + (t * 20 + i) * 0.5);
            }
        });
    }
    for (auto& w : workers) w.join();

    TEST_ASSERT(!frontier.empty(), "Frontier must not be empty after inserts");

    // Pull repeatedly, verifying monotonicity of B_i
    double prev_B = 0.0;
    size_t total_pulled = 0;
    while (!frontier.empty()) {
        std::vector<uint32_t> S_i;
        double B_i = 0.0;
        bool ok = frontier.pull(S_i, B_i);
        if (!ok) break;

        TEST_ASSERT(!S_i.empty(), "Pulled batch S_i must not be empty");
        TEST_ASSERT(S_i.size() <= 4, "Pulled batch size must not exceed M = 4");
        TEST_ASSERT(B_i >= prev_B, "Upper bound B_i sequence must be monotonically non-decreasing");

        prev_B = B_i;
        total_pulled += S_i.size();
    }

    TEST_ASSERT(total_pulled == NUM_THREADS * 20, "All inserted elements must be extracted");
    TEST_ASSERT(frontier.empty(), "Frontier must be empty at the end");

    std::cout << "    [PASS] MutexFrontierD concurrent insertions and pull monotonicity verified." << std::endl;
}

// ============================================================================
// TEST SUITE 5: Parallel Primitives
// ============================================================================
void test_parallel_primitives() {
    std::cout << ">>> Running Test Suite 5: Parallel Primitives (Scan, Sort, Epoch Deduplication)..." << std::endl;

    // 1. Test EpochDeduplicator
    constexpr size_t N_NODES = 500;
    EpochDeduplicator dedup(N_NODES);

    // 16 threads all attempt to claim the exact same node 42 in epoch 5
    std::atomic<int> success_count{0};
    constexpr int THREADS = 16;
    std::vector<std::thread> claim_threads;
    for (int t = 0; t < THREADS; ++t) {
        claim_threads.emplace_back([&dedup, &success_count]() {
            if (dedup.try_claim(42, 5)) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : claim_threads) th.join();

    TEST_ASSERT(success_count.load() == 1, "Exactly one thread must win the epoch claim for node 42");

    // Next epoch should allow a new claim
    TEST_ASSERT(dedup.try_claim(42, 6) == true, "Claiming next epoch must succeed");

    // 2. Test Parallel Prefix Scan on multiple sizes (including edge sizes: 0, 1, prime, large).
    // TSan instruments every access, so keep the parallel case just above the 2048 cutoff.
#if defined(__SANITIZE_THREAD__)
    const std::vector<size_t> scan_sizes{0, 1, 2, 7, 64, 2048, 4096};
#else
    const std::vector<size_t> scan_sizes{0, 1, 2, 7, 64, 2048, 100000};
#endif
    for (size_t test_sz : scan_sizes) {
        std::vector<int64_t> data(test_sz, 1);
        parallel_prefix_scan<int64_t>(data);
        for (size_t i = 0; i < test_sz; ++i) {
            TEST_ASSERT(data[i] == static_cast<int64_t>(i + 1), "Prefix scan correctness");
        }
    }

    // 3. Test Parallel Sample Sort
#if defined(__SANITIZE_THREAD__)
    constexpr size_t SORT_SZ = 4096;
#else
    constexpr size_t SORT_SZ = 50000;
#endif
    std::vector<VertexDist> items(SORT_SZ);
    std::mt19937_64 rng(20261118);
    for (size_t i = 0; i < SORT_SZ; ++i) {
        items[i] = {static_cast<uint32_t>(i), static_cast<double>(rng() % 1000000) * 0.001};
    }

    parallel_sample_sort(items);
    for (size_t i = 1; i < SORT_SZ; ++i) {
        TEST_ASSERT(items[i - 1].dist <= items[i].dist, "Parallel sample sort must produce non-decreasing order");
    }

    std::cout << "    [PASS] Epoch deduplication, parallel scan, and sample sort verified." << std::endl;
}

// ============================================================================
// TEST SUITE 6: Baseline BMSSP Sequential Against Dijkstra Oracle
// ============================================================================
void test_bmssp_seq_against_dijkstra() {
    std::cout << ">>> Running Test Suite 6: Baseline BMSSP Sequential Against Dijkstra Oracle..." << std::endl;

    // Case 1: 18-vertex demo graph with zero-weight, parallel edges and cycles
    std::vector<Edge> demo_edges = {
        {0,1,4},{0,5,2},{0,6,9},{1,2,3},{1,6,2},{2,3,2},
        {2,7,4},{3,4,3},{3,8,2},{4,9,1},{5,6,1},{5,10,5},
        {6,7,2},{6,11,3},{7,8,1},{7,12,4},{8,9,2},{8,13,3},
        {9,14,2},{10,11,1},{10,15,3},{11,12,0},{11,6,1},
        {12,13,2},{12,16,5},{13,14,1},{14,16,2},{15,16,2},
        {0,1,7},{7,12,6}
    };
    CSRGraph demo_graph = CSRGraph::from_edge_list(18, demo_edges);

    SSSPResult dijk_demo = dijkstra_sequential(demo_graph, 0);
    SSSPResult bmssp_demo = bmssp_sequential(demo_graph, 0);

    for (uint32_t u = 0; u < 18; ++u) {
        if (std::isinf(dijk_demo.dist[u])) {
            TEST_ASSERT(std::isinf(bmssp_demo.dist[u]), "Unreachable node 17 must be infinity");
        } else {
            TEST_ASSERT(std::abs(dijk_demo.dist[u] - bmssp_demo.dist[u]) < 1e-9,
                        "BMSSP distance must match Dijkstra within 1e-9");
        }
    }

    // Case 2: Random directed grid with 100 vertices
    constexpr uint32_t GRID_N = 100;
    std::vector<Edge> grid_edges;
    std::mt19937 rng(20261114);
    for (uint32_t r = 0; r < 10; ++r) {
        for (uint32_t c = 0; c < 10; ++c) {
            uint32_t u = r * 10 + c;
            if (c + 1 < 10) grid_edges.push_back({u, u + 1, static_cast<double>(rng() % 20 + 1)});
            if (r + 1 < 10) grid_edges.push_back({u, u + 10, static_cast<double>(rng() % 20 + 1)});
            if (r + 1 < 10 && c + 1 < 10) grid_edges.push_back({u, (r + 1) * 10 + c + 1, static_cast<double>(rng() % 20 + 1)});
        }
    }
    CSRGraph grid_graph = CSRGraph::from_edge_list(GRID_N, grid_edges);

    SSSPResult dijk_grid = dijkstra_sequential(grid_graph, 0);
    SSSPResult bmssp_grid = bmssp_sequential(grid_graph, 0);

    for (uint32_t u = 0; u < GRID_N; ++u) {
        TEST_ASSERT(std::abs(dijk_grid.dist[u] - bmssp_grid.dist[u]) < 1e-9,
                    "Grid graph distances must match Dijkstra within 1e-9");
    }

    std::cout << "    [PASS] Sequential BMSSP produces 100% exact match with Dijkstra oracle." << std::endl;
}

int main() {
    std::cout << "==================================================================" << std::endl;
    std::cout << "  BMSSP Parallel - Person 1 Comprehensive Verification Test Suite " << std::endl;
    std::cout << "==================================================================" << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    test_atomic_vertex_state();
    test_gadget_constant_degree();
    test_memory_pool();
    test_mutex_frontier();
    test_parallel_primitives();
    test_bmssp_seq_against_dijkstra();

    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "==================================================================" << std::endl;
    std::cout << "  ALL PERSON 1 TESTS PASSED SUCCESSFULLY! (" << g_tests_passed << "/" << g_tests_total << " checks)" << std::endl;
    std::cout << "  Total execution time: " << std::fixed << std::setprecision(2) << elapsed_ms << " ms" << std::endl;
    std::cout << "==================================================================" << std::endl;

    return 0;
}
