#pragma once

// Internal — shared prototype selection. Both spawning (Step D) and
// seeding inside senescence (Step E) pick a juvenile prototype by
// nearest Voronoi site in (D, λ) space. Centralised here so the two
// sites stay in sync; not part of the public API.

#include "broflora/world.h"

namespace broflora::internal {

// Nearest Voronoi site in (D, λ) parameter space. Returns nullptr if
// the world has no valid prototype/site registered.
const BranchModulePrototype* pickPrototype(const WorldState& world,
                                           float dPrime, float lambda);

} // namespace broflora::internal
