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

TEST(spawning_monopodial_primary_apical_terminal_identification) {
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, monopodialLeaderModule(2, 0.7f));
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance = 1.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;
    root.vigor = 0.8f;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    step(world, 0.1f);

    const auto& mods = world.plants[0].modules;
    ASSERT(mods.size() == 4u, "spawned 3 children off monopodial root");
    const auto& terms = root.prototype->terminalNodes;
    uint32_t apicalTerm = terms[0];
    for (size_t i = 1; i < mods.size(); ++i) {
        if (mods[i].parentAttachTerminal == apicalTerm) {
            ASSERT(mods[i].isMainChild, "child at apical terminal is marked as main meristem");
        } else {
            ASSERT(!mods[i].isMainChild, "lateral child is not marked as main meristem");
        }
    }
}

TEST(spawning_sympodial_primary_arm_identification) {
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, sympodialForkModule());
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance = 1.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;
    root.vigor = 0.8f;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    step(world, 0.1f);

    const auto& mods = world.plants[0].modules;
    ASSERT(mods.size() == 3u, "spawned 2 children off sympodial root");
    const auto& terms = root.prototype->terminalNodes;
    uint32_t primaryTerm = terms[0];
    for (size_t i = 1; i < mods.size(); ++i) {
        if (mods[i].parentAttachTerminal == primaryTerm) {
            ASSERT(mods[i].isMainChild, "primary arm child is marked as main meristem");
        } else {
            ASSERT(!mods[i].isMainChild, "secondary arm child is not main meristem");
        }
    }
}

TEST(spawning_horizontal_tier_respects_arm_headings) {
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, horizontalTierModule(4, 0.85f));
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance = 1.0f;
    p.species.orthotropy = 0.1f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;
    root.vigor = 0.9f;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    step(world, 0.1f);

    const auto& mods = world.plants[0].modules;
    ASSERT(mods.size() == 5u, "spawned 4 children off 4-arm horizontal tier");

    float xMin = 1e9f, xMax = -1e9f, zMin = 1e9f, zMax = -1e9f;
    for (size_t i = 1; i < mods.size(); ++i) {
        float psi = mods[i].orientation.psi;
        float theta = mods[i].orientation.theta;
        float x = std::sin(psi) * std::sin(theta);
        float z = std::cos(psi) * std::sin(theta);
        xMin = std::min(xMin, x); xMax = std::max(xMax, x);
        zMin = std::min(zMin, z); zMax = std::max(zMax, z);
    }
    ASSERT(xMax - xMin > 0.5f, "tier children aim outward along opposing X headings");
    ASSERT(zMax - zMin > 0.5f, "tier children aim outward along opposing Z headings");
}

