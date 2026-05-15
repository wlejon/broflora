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

#include <cstdint>

namespace broflora {

// Spawn modules onto a single plant. `world` is needed for the prototype
// Voronoi and for neighbour spheres in the collision penalty.
void spawnModules(Plant& plant, WorldState& world, uint64_t& rng);

} // namespace broflora
