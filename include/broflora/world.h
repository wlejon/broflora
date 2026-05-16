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
#include <functional>
#include <vector>

namespace broflora {

// Module-selection parameter space (paper §3.4): each prototype occupies
// a Voronoi cell in (D, λ) coordinates. At spawn time the local (D', λ)
// picks the nearest cell. References the prototype by index into
// `WorldState::prototypes` — using an index (not a pointer) means
// growing the prototype list never silently invalidates the Voronoi.
struct PrototypeVoronoiSite {
    float    determinacy    = 0.5f;        // D coordinate of this site
    float    apicalControl  = 0.5f;        // λ coordinate of this site
    uint32_t prototypeIndex = UINT32_MAX;  // index into WorldState::prototypes
};

// Invariants (enforced by `validate(const WorldState&)`):
//   - Every PrototypeVoronoiSite::prototypeIndex is < prototypes.size().
//   - Every BranchModuleInstance::prototype points to one of the
//     prototypes in this world. Because instances hold raw pointers,
//     prototypes must be registered before any plant referencing them
//     is added; use `addPrototype` / `addVoronoiSite` builders to
//     guarantee a sane construction order.
//   - Each Plant::modules is topologically sorted (parents precede
//     children).
struct WorldState {
    TerrainMap     terrain;
    SoilMap        soil;
    ShadowGrid     shadow;
    GlobalClimate  climate;

    // Prototype library. Owned by-value here; instances reference these
    // by pointer (see invariants above). Reserve capacity up-front if
    // you intend to grow this after creating module instances.
    std::vector<BranchModulePrototype> prototypes;
    std::vector<PrototypeVoronoiSite>  voronoi;

    std::vector<Plant> plants;

    // Deterministic rng seed; advanced internally each step.
    uint64_t rngState = 0x9E3779B97F4A7C15ULL;

    // Total simulated time since construction.
    double simTime = 0.0;
};

// --- Builders --------------------------------------------------------
// These exist so callers don't have to reason about the
// raw-pointer-into-vector invariants. Prefer them over direct
// push_back to `prototypes` / `voronoi` / `plants`.

// Register a prototype and return its index. Stable across subsequent
// calls; safe to keep and pass to `addVoronoiSite`.
uint32_t addPrototype(WorldState& world, BranchModulePrototype proto);

// Register a Voronoi site at (determinacy, apicalControl) pointing at
// the given prototype index. The index must come from `addPrototype`
// on this same world.
void addVoronoiSite(WorldState& world,
                    uint32_t prototypeIndex,
                    float determinacy,
                    float apicalControl);

// Append a plant to the world. Returns a reference to the stored plant.
// All module instances in `plant` must reference prototypes owned by
// this world (typically registered first via `addPrototype`).
Plant& addPlant(WorldState& world, Plant plant);

// Look up a prototype by index. Returns nullptr for out-of-range.
inline const BranchModulePrototype* prototypeAt(const WorldState& world,
                                                uint32_t index) {
    return index < world.prototypes.size() ? &world.prototypes[index] : nullptr;
}

// Optional per-phase observer for callers (typically `bro`) that want
// to snapshot intermediate state for visualisation, recording, or
// debugging. Every callback is optional — leave the unused ones null.
// Callbacks run synchronously inside `stepWithObserver` between the
// numbered phases of paper §3. State passed in is the world at the
// moment of the callback (e.g. `postVigor` runs after the acropetal
// pass but before development), so reading module.vigor / module.age /
// etc. from it observes that exact slice. Do not mutate `world` from
// inside a callback — the simulation is mid-step.
struct StepObserver {
    std::function<void(const WorldState&)> postLight;
    std::function<void(const WorldState&)> postVigor;
    std::function<void(const WorldState&)> postDevelopment;
    std::function<void(const WorldState&)> preSpawn;
    std::function<void(const WorldState&)> postSpawn;
    std::function<void(const WorldState&)> postSenescence;
};

// Advance the world by `dt` (seconds, or whatever your time unit is —
// the paper uses dimensionless "frames"; downstream you pick the
// mapping). Runs steps A–E of paper §3 in order:
//
//   A. evaluateLightAndCollisions     → observer.postLight
//   B. vigorPasses    (basipetal + acropetal)
//                                     → observer.postVigor
//   C. developModules (age, geometry, tropism)
//                                     → observer.postDevelopment
//                                     → observer.preSpawn
//   D. spawnModules   (mature terminals → new modules)
//                                     → observer.postSpawn
//   E. ecosystemTick  (senescence, climate, seeding)
//                                     → observer.postSenescence
void step(WorldState& world, float dt);

// As above, but invokes the given observer callbacks between phases.
// `step(w, dt)` is equivalent to `stepWithObserver(w, dt, {})`.
void stepWithObserver(WorldState& world, float dt, const StepObserver& observer);

} // namespace broflora
