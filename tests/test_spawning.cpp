#include "test_framework.h"

using namespace broflora;

TEST(mature_module_spawns_children) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});  // root
    proto.nodes.push_back({{0.4f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});  // term A
    proto.nodes.push_back({{-0.4f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f}); // term B
    proto.edges.push_back({0, 1});
    proto.edges.push_back({0, 2});
    proto.rootNode = 0;
    proto.terminalNodes = {1, 2};

    WorldState world;
    world.shadow.width = world.shadow.height = world.shadow.depth = 8;
    world.shadow.cellSize = 1.0f;
    world.shadow.origin = {-4.0f, -4.0f, -4.0f};
    world.shadow.qg.assign(8 * 8 * 8, 1.0f);
    uint32_t protoIdx = addPrototype(world, proto);
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance = 1.0f;  // ignore overlap reductions
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;        // already mature
    root.vigor = 0.6f;
    root.light = 1.0f;
    p.modules.push_back(root);

    world.plants.push_back(p);

    size_t before = world.plants[0].modules.size();
    step(world, 0.1f);
    size_t after = world.plants[0].modules.size();

    ASSERT(after > before, "mature module spawned at least one child");
    ASSERT(after <= before + 2, "no more than #terminals children spawned");
}

TEST(immature_module_does_not_spawn) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, proto);
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 100.0f;  // never mature this tick
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 0.0f;
    root.vigor = 0.6f;
    root.light = 1.0f;
    p.modules.push_back(root);

    world.plants.push_back(p);
    step(world, 0.1f);

    ASSERT(world.plants[0].modules.size() == 1, "no spawn when below moduleMatureAge");
}
