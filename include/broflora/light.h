#pragma once

// Step A — Spatial constraints and light.
//
//   1. f_collisions(u) = Σ_w V_intersect(B_u, B_w) over neighbours.
//   2. Local light:  Q(u) = exp(-f_collisions(u))
//   3. Global shadowing: for the module's grid cell, decrease Q_G of
//      every cell vertically below it.
//   4. Effective light: Q_eff = lerp(s_tol, 1, Q · Q_G).
//
// Paper §3.1.

#include "broflora/plant.h"
#include "broflora/world.h"
#include "bromath/spatial_hash.h"

namespace broflora {

// Recompute module bounding spheres, evaluate collisions + light, and
// stamp shadows into `world.shadow`. Resets shadow.qg to 1.0 at the
// start of each tick — caller does not need to clear it.
//
// `index` is a uniform-grid spatial hash built by the caller from every
// module's current bbox sphere, with the entry id packed as
// (plantIndex << 20) | moduleIndex. Used to broad-phase the f_collisions
// sum down from O(N²) to O(N · k) where k is the average occupied
// neighbourhood.
void evaluateLightAndCollisions(WorldState& world,
                                const bromath::SpatialHash3D& index);

} // namespace broflora
