#include "test_framework.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace broflora;

namespace {

// Minimal one-prototype world with one seedling, enough to exercise
// every phase (light, vigor, development, spawn, senescence) at least
// once per tick.
WorldState makeWorld() {
    BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t pi = addPrototype(world, std::move(proto));
    addVoronoiSite(world, pi, 0.5f, 0.55f);

    Plant plant;
    plant.species = {};
    plant.species.shadeTolerance = 1.0f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance m;
    m.prototype = &world.prototypes[pi];
    m.parent = UINT32_MAX;
    m.age = 0.0f;
    m.vigor = 0.5f;
    m.light = 1.0f;
    plant.modules.push_back(m);

    addPlant(world, std::move(plant));
    return world;
}

} // namespace

TEST(step_observer_fires_phases_in_documented_order) {
    WorldState world = makeWorld();

    std::vector<std::string> order;
    StepObserver obs;
    obs.postLight       = [&](const WorldState&) { order.emplace_back("light"); };
    obs.postVigor       = [&](const WorldState&) { order.emplace_back("vigor"); };
    obs.postDevelopment = [&](const WorldState&) { order.emplace_back("development"); };
    obs.preSpawn        = [&](const WorldState&) { order.emplace_back("preSpawn"); };
    obs.postSpawn       = [&](const WorldState&) { order.emplace_back("postSpawn"); };
    obs.postSenescence  = [&](const WorldState&) { order.emplace_back("senescence"); };

    stepWithObserver(world, 0.1f, obs);

    ASSERT(order.size() == 6, "every callback fires once");
    ASSERT(order[0] == "light",       "phase A first");
    ASSERT(order[1] == "vigor",       "phase B second");
    ASSERT(order[2] == "development", "phase C third");
    ASSERT(order[3] == "preSpawn",    "preSpawn fires before postSpawn");
    ASSERT(order[4] == "postSpawn",   "phase D fourth");
    ASSERT(order[5] == "senescence",  "phase E last");
}

TEST(step_observer_postVigor_sees_distributed_vigor) {
    WorldState world = makeWorld();

    float vigorAtPostLight = -1.0f;
    float vigorAtPostVigor = -1.0f;
    StepObserver obs;
    obs.postLight = [&](const WorldState& w) {
        vigorAtPostLight = w.plants.front().modules.front().vigor;
    };
    obs.postVigor = [&](const WorldState& w) {
        vigorAtPostVigor = w.plants.front().modules.front().vigor;
    };

    stepWithObserver(world, 0.1f, obs);

    // The vigor pass overwrites root vigor with the root-vigor cap
    // (paper §3.2), so the value visible to postVigor must differ from
    // the value visible to postLight (which is whatever was set at
    // seeding time, 0.5f for the species default in makeWorld).
    ASSERT(vigorAtPostLight > 0.0f, "postLight observes the pre-vigor state");
    ASSERT(vigorAtPostVigor > 0.0f, "postVigor observes the post-vigor state");
    ASSERT(std::fabs(vigorAtPostVigor - vigorAtPostLight) > 1e-6f,
           "vigor must change between postLight and postVigor");
}

TEST(step_without_observer_matches_step_with_empty_observer) {
    WorldState a = makeWorld();
    WorldState b = makeWorld();

    for (int i = 0; i < 20; ++i) {
        step(a, 0.1f);
        stepWithObserver(b, 0.1f, {});
    }

    ASSERT(a.plants.size() == b.plants.size(), "plant count matches");
    ASSERT(a.plants.front().modules.size() == b.plants.front().modules.size(),
           "module count matches");
    ASSERT(std::fabs(a.simTime - b.simTime) < 1e-6, "simTime matches");
    ASSERT(a.rngState == b.rngState, "rng state matches");
}
