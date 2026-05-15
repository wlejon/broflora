#pragma once

// Step E — Ecosystem-level logic.
//
//   senescence:   if p_t >= p_max, linearly ramp effectiveRootVigorMax → 0.
//                 Drop modules where v̄ < v̄_min.
//
//   climate σ:    2D Gaussian over (T, P) vs (T_A, P_A) → adaptation prob.
//                 Scale effectiveRootVigorMax and seeding frequency by σ.
//
//   seeding:      F_eff = F_age · v̂_rootmax / v̄_root.
//                 If p_t > F_eff, drop seeds in a Gaussian disc of radius
//                 species.seedingRadius. Mark plant `flowering = true`
//                 and switch to mature (λ, D).
//
// Paper §3.5.

#include "broflora/plant.h"
#include "broflora/world.h"

#include <cstdint>

namespace broflora {

// Apply senescence + climate scaling + seeding to every plant in `world`.
// Removes dead plants in place at the end.
void ecosystemTick(WorldState& world, float dt, uint64_t& rng);

} // namespace broflora
