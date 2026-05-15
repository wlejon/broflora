#include "test_framework.h"

using namespace broflora;

// Build a tiny prototype: one root node, two terminals. Just enough to
// hang module instances off of.
static BranchModulePrototype tinyPrototype() {
    BranchModulePrototype p;
    p.name = "tiny";
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});  // root
    p.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});  // term A
    p.nodes.push_back({{0.5f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});  // term B
    p.edges.push_back({0, 1});
    p.edges.push_back({0, 2});
    p.rootNode = 0;
    p.terminalNodes = {1, 2};
    return p;
}

TEST(world_step_is_a_noop_when_empty) {
    WorldState world;
    step(world, 1.0f / 60.0f);
    ASSERT(world.simTime > 0.0, "simTime advanced");
    ASSERT(world.plants.empty(), "no plants spawned spontaneously");
}

TEST(single_plant_two_module_vigor_pass) {
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);  // 1x1x1 shadow grid stub

    static BranchModulePrototype proto = tinyPrototype();

    Plant plant;
    plant.species = {};
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;
    plant.origin = {0.0f, 0.0f, 0.0f};

    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.light = 1.0f;
    plant.modules.push_back(root);

    BranchModuleInstance child;
    child.prototype = &proto;
    child.parent = 0;
    child.light = 1.0f;
    plant.modules.push_back(child);

    world.plants.push_back(plant);

    step(world, 0.1f);

    const auto& p = world.plants.front();
    ASSERT(p.modules.size() == 2, "both modules retained after one tick");
    ASSERT(p.modules[0].vigor > 0.0f, "root received vigor");
    ASSERT(p.age > 0.0f, "plant aged");
}

TEST(senescence_drops_dead_modules) {
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    static BranchModulePrototype proto = tinyPrototype();

    Plant plant;
    plant.species = {};
    plant.species.maxAge = 0.0f;          // begin senescing immediately
    plant.species.minVigor = 1e9f;        // every module is "dead"
    plant.age = 1.0f;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 1.0f;
    m.vigor = 0.0f;
    plant.modules.push_back(m);

    world.plants.push_back(plant);

    step(world, 1.0f);

    // After step: senescence ramp pushed effectiveRootVigorMax to zero,
    // module was below vmin so it was shed, plant should be removed.
    ASSERT(world.plants.empty(), "fully senesced plant was removed");
}
