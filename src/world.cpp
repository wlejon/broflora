#include "broflora/world.h"

#include "broflora/light.h"
#include "broflora/vigor.h"
#include "broflora/development.h"
#include "broflora/spawning.h"
#include "broflora/senescence.h"
#include "bromath/spatial_hash.h"
#include "internal_spatial.h"

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
    return world.plants.back();
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

void step(WorldState& world, float dt) {
    // Build the per-tick spatial index from current module bboxes. Both
    // the light pass (f_collisions) and the spawn pass (gradient-descent
    // neighbour penalty) read from it; spawning also inserts newly-
    // settled siblings so subsequent siblings see them.
    bromath::SpatialHash3D index = buildSpatialIndex(world);

    // A. Light + collisions (paper §3.1).
    evaluateLightAndCollisions(world, index);

    // B. Vigor passes per plant (paper §3.2).
    for (auto& plant : world.plants) {
        runVigorPasses(plant);
    }

    // C. Develop modules per plant (paper §3.3).
    for (auto& plant : world.plants) {
        developModules(plant, dt);
    }

    // D. Spawn new modules per plant (paper §3.4).
    for (auto& plant : world.plants) {
        spawnModules(plant, world, index, world.rngState);
    }

    // E. Ecosystem-wide senescence + seeding (paper §3.5).
    ecosystemTick(world, dt, world.rngState);

    world.simTime += dt;
}

} // namespace broflora
