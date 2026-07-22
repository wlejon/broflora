#include "broflora/world.h"

#include "broflora/light.h"
#include "broflora/vigor.h"
#include "broflora/development.h"
#include "broflora/spawning.h"
#include "broflora/senescence.h"
#include "bromath/rng.h"
#include "bromath/scalar.h"
#include "bromath/spatial_hash.h"
#include "internal_env.h"
#include "internal_spatial.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace broflora {

uint32_t addPrototype(WorldState& world, BranchModulePrototype proto) {
    world.prototypes.push_back(std::move(proto));
    return static_cast<uint32_t>(world.prototypes.size() - 1);
}

void addVoronoiSite(WorldState& world,
                    uint32_t prototypeIndex,
                    float determinacy,
                    float apicalControl) {
    PrototypeVoronoiSite site;
    site.determinacy    = determinacy;
    site.apicalControl  = apicalControl;
    site.prototypeIndex = prototypeIndex;
    world.voronoi.push_back(site);
}

Plant& addPlant(WorldState& world, Plant plant) {
    world.plants.push_back(std::move(plant));
    Plant& added = world.plants.back();
    // Seed the root module's world position from the plant origin so any
    // emit before the first step() places the plant correctly. The first
    // development pass recomputes this (and every child's) anyway.
    if (!added.modules.empty()) {
        added.modules.front().worldPos = added.origin;
    }

    // Per-plant phenotype variation. Without it, same-species plants grow
    // into bit-identical clones — the sim is deterministic and its only
    // stochastic input (spawn-orientation jitter) is too small to cascade
    // into distinct structure, so a stand looks like one plant stamped N
    // times. Seed a per-plant RNG from the origin (reproducible, yet
    // distinct per individual) and give each plant its own azimuth plus a
    // small growth-parameter offset — applied to this plant's own species
    // copy, so the caller's Species and every sibling are untouched.
    const float var = added.species.individualVariation;
    if (var > 0.0f && !added.modules.empty()) {
        const float v = std::min(1.0f, var);
        auto hashf = [](float f, uint32_t salt) -> uint32_t {
            uint32_t x;
            std::memcpy(&x, &f, sizeof(x));
            x ^= salt;
            x ^= x >> 16; x *= 0x7feb352dU;
            x ^= x >> 15; x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        };
        uint64_t rng = (static_cast<uint64_t>(hashf(added.origin.x, 0x9E3779B9u)) << 32)
                     ^  static_cast<uint64_t>(hashf(added.origin.z, 0x85EBCA6Bu))
                     ^  0xD1B54A32D192ED03ULL;
        if (rng == 0) rng = 0x1234567ULL;

        // (a) Whole-plant azimuth so a whorl's arms don't fan at the same
        // world angles on every plant.
        added.modules.front().orientation.psi +=
            bromath::randFloat01(rng) * bromath::TWO_PI;

        // (b) Phenotype jitter on this plant's own species copy.
        Species& sp = added.species;
        sp.growthScale     *= 1.0f + v * 0.20f * bromath::randSigned(rng);
        sp.moduleMatureAge *= 1.0f + v * 0.20f * bromath::randSigned(rng);
        sp.orthotropy = std::min(1.0f, std::max(0.0f,
            sp.orthotropy + v * 0.15f * bromath::randSigned(rng)));
    }

    return added;
}

bool removePlant(WorldState& world, uint32_t plantIndex) {
    if (plantIndex >= world.plants.size()) return false;
    const size_t last = world.plants.size() - 1;
    if ((size_t)plantIndex != last) {
        world.plants[plantIndex] = std::move(world.plants[last]);
    }
    world.plants.pop_back();
    return true;
}

// Cell-size heuristic: mean bbox radius across every module, doubled, with
// a floor of 0.5. Doubling matches the typical broad-phase tuning where a
// query of radius r touches ~(2r/cell + 1)^3 cells — a cell ≈ mean radius
// keeps the dilation footprint small while leaving cells large enough
// that the hash map doesn't churn.
static float pickCellSize(const WorldState& world) {
    double sum = 0.0;
    size_t count = 0;
    for (const auto& pl : world.plants) {
        for (const auto& m : pl.modules) {
            if (m.bboxRadius > 0.0f) { sum += m.bboxRadius; ++count; }
        }
    }
    if (count == 0) return 1.0f;
    float mean = static_cast<float>(sum / static_cast<double>(count));
    return mean > 0.25f ? 2.0f * mean : 0.5f;
}

static bromath::SpatialHash3D buildSpatialIndex(const WorldState& world) {
    bromath::SpatialHash3D index(pickCellSize(world));
    for (size_t p = 0; p < world.plants.size(); ++p) {
        const auto& pl = world.plants[p];
        for (size_t i = 0; i < pl.modules.size(); ++i) {
            const auto& m = pl.modules[i];
            if (m.bboxRadius > 0.0f) {
                index.insert(bromath::Sphere{m.bboxCenter, m.bboxRadius},
                             internal::packEntryId(static_cast<uint32_t>(p),
                                                   static_cast<uint32_t>(i)));
            }
        }
    }
    return index;
}

void stepWithObserver(WorldState& world, float dt, const StepObserver& obs) {
    // --- Terrain coupling (broflora extension to paper §3).
    //
    // Snap every plant's origin Y to the terrain heightfield. Out-of-
    // footprint plants keep their previous Y (terrainHeightAt falls
    // back). This handles dynamic terrain (sculpt tools, erosion)
    // without requiring `bro` to chase plant positions itself.
    //
    // Then, for species with terrainAnchorWeight > 0, blend the root
    // module's orientation toward the terrain normal at the plant
    // origin. Yaw ψ rotates around +Y so we want enough tilt that the
    // root's growth axis = rotateYawPitch({0,1,0}, ψ, θ) aligns with
    // the surface normal. With our pitch-then-yaw convention:
    //     normal = (sin ψ · sin θ,   cos θ,   cos ψ · sin θ)
    //   (rotateYawPitch applied to +Y), so for a normal (nx, ny, nz):
    //     θ_target = acos(ny)
    //     ψ_target = atan2(nx, nz)
    // Apply linearly weighted toward the default (θ_default = 0,
    // ψ_default = original). Root only — children inherit the tilt
    // implicitly through the parent's frame.
    for (auto& plant : world.plants) {
        plant.origin.y = internal::terrainHeightAt(world.terrain, plant.origin,
                                                   plant.origin.y);
        const float w = plant.species.terrainAnchorWeight;
        if (w > 0.0f && !plant.modules.empty()) {
            bromath::Vec3 n = internal::terrainNormalAt(world.terrain, plant.origin);
            float ny = std::max(-1.0f, std::min(1.0f, n.y));
            float thetaTarget = std::acos(ny);
            float psiTarget   = std::atan2(n.x, n.z);
            auto& root = plant.modules.front();
            // Blend from current toward target. ψ wraps; blend the
            // shortest arc by going through atan2 of weighted unit
            // vectors rather than naively lerping the angle.
            float c0 = std::cos(root.orientation.psi);
            float s0 = std::sin(root.orientation.psi);
            float c1 = std::cos(psiTarget);
            float s1 = std::sin(psiTarget);
            float cx = (1.0f - w) * c0 + w * c1;
            float sx = (1.0f - w) * s0 + w * s1;
            root.orientation.psi   = std::atan2(sx, cx);
            root.orientation.theta = (1.0f - w) * root.orientation.theta + w * thetaTarget;
        }
    }

    // Build the per-tick spatial index from current module bboxes. Both
    // the light pass (f_collisions) and the spawn pass (gradient-descent
    // neighbour penalty) read from it; spawning also inserts newly-
    // settled siblings so subsequent siblings see them.
    bromath::SpatialHash3D index = buildSpatialIndex(world);

    // A. Light + collisions (paper §3.1).
    evaluateLightAndCollisions(world, index);
    if (obs.postLight) obs.postLight(world);

    // B. Vigor passes per plant (paper §3.2).
    const int plantCount = static_cast<int>(world.plants.size());
    #pragma omp parallel for schedule(static) if(plantCount > 4)
    for (int i = 0; i < plantCount; ++i) {
        runVigorPasses(world.plants[static_cast<size_t>(i)]);
    }
    if (obs.postVigor) obs.postVigor(world);

    // C. Develop modules per plant (paper §3.3).
    #pragma omp parallel for schedule(static) if(plantCount > 4)
    for (int i = 0; i < plantCount; ++i) {
        developModules(world.plants[static_cast<size_t>(i)], dt);
    }
    if (obs.postDevelopment) obs.postDevelopment(world);

    // D. Spawn new modules per plant (paper §3.4).
    if (obs.preSpawn) obs.preSpawn(world);
    for (auto& plant : world.plants) {
        spawnModules(plant, world, index, world.rngState);
    }
    if (obs.postSpawn) obs.postSpawn(world);

    // E. Ecosystem-wide senescence + seeding (paper §3.5).
    ecosystemTick(world, dt, world.rngState);
    if (obs.postSenescence) obs.postSenescence(world);

    world.simTime += dt;
}

void step(WorldState& world, float dt) {
    static const StepObserver none{};
    stepWithObserver(world, dt, none);
}

} // namespace broflora
