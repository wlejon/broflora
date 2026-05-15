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
    world.prototypes.push_back(proto);
    world.voronoi.push_back({0.5f, 0.5f, &world.prototypes.back()});
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
    root.prototype = &world.prototypes.front();
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
