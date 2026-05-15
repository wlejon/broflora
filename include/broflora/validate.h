#pragma once

// Runtime invariant checks. Intended for tests and debug builds — these
// walk the whole structure so they aren't free, but they catch the kind
// of state corruption (broken topological order, dangling prototype
// reference, out-of-range Voronoi index) that otherwise surfaces as a
// silent miscompute deep inside a simulation tick.
//
// Both functions return true if the structure is valid. On failure the
// optional `err` is set to a short human-readable description of the
// first invariant violation found.

#include "broflora/plant.h"
#include "broflora/world.h"

#include <string>

namespace broflora {

// Check Plant invariants:
//   - modules[0] is a root (parent == UINT32_MAX).
//   - For every i > 0, modules[i].parent < i (topological order).
//   - For every non-root module, modules[i].parent != UINT32_MAX.
//   - Every module's prototype pointer is non-null.
bool validate(const Plant& plant, std::string* err = nullptr);

// Check WorldState invariants:
//   - Every voronoi[i].prototypeIndex < prototypes.size().
//   - Every plant validates.
bool validate(const WorldState& world, std::string* err = nullptr);

} // namespace broflora
