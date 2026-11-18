#pragma once

#include "types.hpp"
#include <vector>
#include <span>
#include <mutex>
#include <map>
#include <algorithm>

namespace bmssp {

/**
 * @brief Abstract interface for the Data Structure D specified in Lemma 3.3.
 * Manages frontier vertices in blocks of size <= M with bounded pull operations.
 */
class IFrontierD {
public:
    virtual ~IFrontierD() = default;

    /**
     * @brief Initializes structure with block size M and global distance upper bound B.
     */
    virtual void init(size_t M, double B) = 0;

    /**
     * @brief Inserts or relaxes a vertex v with distance dist.
     * Must support concurrent calls from multiple relaxation threads.
     */
    virtual void insert(uint32_t v, double dist) = 0;

    /**
     * @brief Batch prepends a sorted set of vertices K.
     * Invoked sequentially on the Master thread after barrier synchronization.
     */
    virtual void batch_prepend(std::span<const VertexDist> K) = 0;

    /**
     * @brief Extracts up to M vertices with the smallest distances into S_i
     * and returns the upper bound separator distance B_i.
     * 
     * @param[out] S_i Destination buffer for extracted vertices.
     * @param[out] B_i Upper bound separator distance.
     * @return true if vertices were extracted, false if structure is empty.
     */
    virtual bool pull(std::vector<uint32_t>& S_i, double& B_i) = 0;

    /**
     * @brief Returns true if no active vertices remain.
     */
    [[nodiscard]] virtual bool empty() const = 0;

    /**
     * @brief Clears all internal containers and resets state.
     */
    virtual void clear() = 0;
};

/**
 * @brief Thread-safe Mockup implementation of IFrontierD using std::mutex.
 * Serves as an unblocking fixture for Algorithm Engineer (Person 3) while
 * Concurrent Skip-List D_1 is being built by Data Structure Engineer (Person 2).
 */
class MutexFrontierD : public IFrontierD {
private:
    mutable std::mutex mtx_;
    size_t M_{16};
    double B_{INF_DIST};
    std::multimap<double, uint32_t> elements_; // Sorted by distance ascending

public:
    MutexFrontierD() = default;
    ~MutexFrontierD() override = default;

    void init(size_t M, double B) override {
        std::lock_guard<std::mutex> lock(mtx_);
        M_ = std::max(M, size_t{1});
        B_ = B;
        elements_.clear();
    }

    void insert(uint32_t v, double dist) override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (dist > B_) return; // Exceeds upper bound
        elements_.emplace(dist, v);
    }

    void batch_prepend(std::span<const VertexDist> K) override {
        if (K.empty()) return;
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& item : K) {
            if (item.dist <= B_) {
                elements_.emplace(item.dist, item.v);
            }
        }
    }

    bool pull(std::vector<uint32_t>& S_i, double& B_i) override {
        std::lock_guard<std::mutex> lock(mtx_);
        S_i.clear();
        if (elements_.empty()) {
            B_i = B_;
            return false;
        }

        size_t count = 0;
        double last_dist = elements_.begin()->first;
        auto it = elements_.begin();

        while (it != elements_.end() && count < M_) {
            S_i.push_back(it->second);
            last_dist = it->first;
            it = elements_.erase(it);
            count++;
        }

        // B_i is the key of the next remaining element or last pulled distance
        if (!elements_.empty()) {
            B_i = elements_.begin()->first;
        } else {
            B_i = last_dist;
        }

        return !S_i.empty();
    }

    [[nodiscard]] bool empty() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        return elements_.empty();
    }

    void clear() override {
        std::lock_guard<std::mutex> lock(mtx_);
        elements_.clear();
    }
};

} // namespace bmssp
