#include "broflora/world.h"

#include "broflora/light.h"
#include "broflora/vigor.h"
#include "broflora/development.h"
#include "broflora/spawning.h"
#include "broflora/senescence.h"

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

void step(WorldState& world, float dt) {
    // A. Light + collisions (paper §3.1).
    evaluateLightAndCollisions(world);

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
        spawnModules(plant, world, world.rngState);
    }

    // E. Ecosystem-wide senescence + seeding (paper §3.5).
    ecosystemTick(world, dt, world.rngState);

    world.simTime += dt;
}

} // namespace broflora
