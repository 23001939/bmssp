#pragma once

#include "core/types.hpp"

namespace bmssp::detail {

enum class EdgeRoute { None, Insert, Prepend };

inline EdgeRoute relax_and_route_edge(AtomicVertexState& state, double distance,
                                     uint32_t from, uint32_t epoch, double bound,
                                     double trying_bound, double complete_bound,
                                     const uint8_t& completed) {
    if (distance >= bound) {
        return EdgeRoute::None;
    }

    const double current = state.load_dist();
    if (distance < current) {
        if (relax_edge_atomic(state, distance, from, epoch)) {
            // Preserve the conservative requeue of strict improvements below the child bound.
            return distance >= trying_bound ? EdgeRoute::Insert : EdgeRoute::Prepend;
        }
    } else if (distance == current && !completed) {
        if (distance >= trying_bound) {
            return EdgeRoute::Insert;
        }
        if (distance >= complete_bound) {
            return EdgeRoute::Prepend;
        }
    }
    return EdgeRoute::None;
}

} // namespace bmssp::detail
