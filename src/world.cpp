#include "broflora/world.h"

#include "broflora/light.h"
#include "broflora/vigor.h"
#include "broflora/development.h"
#include "broflora/spawning.h"
#include "broflora/senescence.h"

namespace broflora {

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
