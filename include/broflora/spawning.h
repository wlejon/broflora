#pragma once

// Step D — Spawn new modules at mature terminal nodes.
//
//   per-terminal vigor: q(n_i) = Q(u) / #n
//   if q(n_i) > v̄_min: attach a new module
//
//   prototype pick:  D' = v̄(u_parent) · D / v̄_max, then nearest
//                    Voronoi site in (D', λ).
//
//   orient via gradient descent on
//     f_distribution(u) = ω1 · f_collisions + ω2 · f_tropism
//
// Paper §3.4.

#include "broflora/plant.h"
#include "broflora/world.h"
#include "bromath/spatial_hash.h"

#include <cstdint>

namespace broflora {

// Spawn modules onto a single plant. `world` is needed for the prototype
// Voronoi. `index` is the per-tick spatial hash from world.cpp::step;
// newly-settled siblings are inserted back into it so subsequent
// siblings' gradient descent sees them as collision neighbours.
void spawnModules(Plant& plant,
                  WorldState& world,
                  bromath::SpatialHash3D& index,
                  uint64_t& rng);

} // namespace broflora
