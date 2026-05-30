#include "test_framework.h"

using namespace broflora;

TEST(flowering_plant_eventually_drops_seeds) {
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
    world.rngState = 42ULL;

    Plant p;
    p.species = {};
    p.species.floweringAge   = 0.0f;     // mature immediately
    p.species.seedingRadius  = 2.0f;
    p.species.maxAge         = 1000.0f;  // not senescing
    p.species.moduleMatureAge = 1000.0f; // no spawning, focus on seeding
    p.effectiveRootVigorMax  = p.species.rootVigorMax;
    p.age = 1.0f;
    p.flowering = true;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 0.0f;
    root.vigor = p.species.rootVigorMax;  // high root vigor → small F_eff
    root.light = 1.0f;
    p.modules.push_back(root);

    world.plants.push_back(p);

    // Step until at least one seedling appears (or give up).
    bool seeded = false;
    for (int i = 0; i < 200 && !seeded; ++i) {
        step(world, 1.0f);
        seeded = world.plants.size() > 1;
    }
    ASSERT(seeded, "flowering plant produced a seedling within 200 ticks");
}

// Seeding must stay inside the shadow-grid footprint. Before containment a
// seed landing outside the grid grew in permanent full sun (no shadow
// cells to occlude it) and seeded again, so the population diverged
// outward without bound. Shade tolerance is maxed here to isolate the
// containment rule from the light-gated recruitment rule.
TEST(seeding_stays_within_shadow_footprint) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    world.shadow.origin   = {0.0f, 0.0f, 0.0f};
    world.shadow.cellSize = 1.0f;
    world.shadow.width = world.shadow.height = world.shadow.depth = 8;  // footprint [0,8]
    world.shadow.qg.assign(8u * 8u * 8u, 1.0f);
    const uint32_t protoIdx = addPrototype(world, proto);
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);
    world.rngState = 7ULL;

    Plant p;
    p.species = {};
    p.species.floweringAge    = 0.0f;
    p.species.seedingRadius   = 20.0f;    // scatter far — most candidates land outside
    p.species.maxAge          = 1e6f;
    p.species.moduleMatureAge = 1e6f;     // no spawning; isolate seeding
    p.species.shadeTolerance  = 1.0f;     // recruitment never light-gated here
    p.effectiveRootVigorMax   = p.species.rootVigorMax;
    p.origin = {4.0f, 0.0f, 4.0f};        // centre of the footprint
    p.age = 1.0f;
    p.flowering = true;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.vigor = p.species.rootVigorMax;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    for (int i = 0; i < 400; ++i) step(world, 1.0f);

    ASSERT(world.plants.size() > 1u, "seeder established at least one recruit");
    bool allInside = true;
    for (const auto& pl : world.plants) {
        if (pl.origin.x < 0.0f || pl.origin.x > 8.0f ||
            pl.origin.z < 0.0f || pl.origin.z > 8.0f) { allInside = false; break; }
    }
    ASSERT(allInside, "every plant origin stayed within the shadow footprint");
}
