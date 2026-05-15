#pragma once

// World — the top of the simulation graph. Owns the environment,
// the plant prototype library, the list of plants, and the rng seed.
// `step(dt)` advances the whole ecosystem one tick.
//
// Paper §3 (full simulation loop).

#include "broflora/environment.h"
#include "broflora/module.h"
#include "broflora/plant.h"

#include <cstdint>
#include <vector>

namespace broflora {

// Module-selection parameter space (paper §3.4): each prototype occupies
// a Voronoi cell in (D, λ) coordinates. At spawn time the local (D', λ)
// picks the nearest cell.
struct PrototypeVoronoiSite {
    float determinacy   = 0.5f;   // D coordinate of this site
    float apicalControl = 0.5f;   // λ coordinate of this site
    const BranchModulePrototype* prototype = nullptr;
};

struct WorldState {
    TerrainMap     terrain;
    SoilMap        soil;
    ShadowGrid     shadow;
    GlobalClimate  climate;

    // Prototype library — non-owning pointers held inside `voronoi`.
    std::vector<BranchModulePrototype> prototypes;
    std::vector<PrototypeVoronoiSite>  voronoi;

    std::vector<Plant> plants;

    // Deterministic rng seed; advanced internally each step.
    uint64_t rngState = 0x9E3779B97F4A7C15ULL;

    // Total simulated time since construction.
    double simTime = 0.0;
};

// Advance the world by `dt` (seconds, or whatever your time unit is —
// the paper uses dimensionless "frames"; downstream you pick the
// mapping). Runs steps A–E of paper §3 in order:
//
//   A. evaluateLightAndCollisions
//   B. vigorPasses           (basipetal + acropetal)
//   C. developModules        (age, geometry, tropism)
//   D. spawnModules          (mature terminals → new modules)
//   E. ecosystemTick         (senescence, climate, seeding)
//
// Currently a skeleton — see TODO markers inside the per-step headers.
void step(WorldState& world, float dt);

} // namespace broflora
