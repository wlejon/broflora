#include "test_framework.h"

using namespace broflora;

// Two heavily-overlapping spheres should drive Q < 1 for both, then
// after shade-tolerance lerp Q_eff < 1 too.
TEST(overlapping_modules_reduce_light) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    world.shadow.width = world.shadow.height = world.shadow.depth = 4;
    world.shadow.cellSize = 2.0f;
    world.shadow.origin = {-4.0f, -4.0f, -4.0f};
    world.shadow.qg.assign(4 * 4 * 4, 1.0f);

    // Two plants whose modules will share a bounding sphere.
    auto makePlant = [&](bromath::Vec3 origin) {
        Plant p;
        p.species = {};
        p.species.shadeTolerance = 0.0f;  // any shadow propagates straight through
        p.species.moduleMatureAge = 100.0f;
        p.effectiveRootVigorMax = p.species.rootVigorMax;
        p.origin = origin;
        BranchModuleInstance m;
        m.prototype = &proto;
        m.parent = UINT32_MAX;
        m.age = 5.0f;     // mature enough to have full bbox
        m.vigor = 0.5f;
        m.light = 1.0f;
        p.modules.push_back(m);
        return p;
    };

    world.plants.push_back(makePlant({0.0f, 0.0f, 0.0f}));
    world.plants.push_back(makePlant({0.1f, 0.0f, 0.0f}));  // near-coincident

    // First step seeds bboxes (development runs after light); second
    // step is the one whose light pass actually sees the overlap.
    step(world, 0.1f);
    step(world, 0.1f);

    float l0 = world.plants[0].modules[0].light;
    float l1 = world.plants[1].modules[0].light;
    ASSERT(l0 < 1.0f, "overlapping module 0 has reduced light");
    ASSERT(l1 < 1.0f, "overlapping module 1 has reduced light");
}

TEST(isolated_module_keeps_full_light) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    world.shadow.width = world.shadow.height = world.shadow.depth = 4;
    world.shadow.cellSize = 2.0f;
    world.shadow.origin = {-4.0f, -4.0f, -4.0f};
    world.shadow.qg.assign(4 * 4 * 4, 1.0f);

    Plant p;
    p.species = {};
    p.species.shadeTolerance = 0.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 5.0f;
    m.vigor = 0.5f;
    m.light = 1.0f;
    p.modules.push_back(m);
    world.plants.push_back(p);

    step(world, 0.1f);
    step(world, 0.1f);
    float l = world.plants[0].modules[0].light;
    ASSERT(l >= 0.999f, "isolated module gets ~full light");
}
