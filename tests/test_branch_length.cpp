#include "test_framework.h"

#include "bromath/vec.h"

#include <cmath>

using namespace broflora;
using bromath::vlen;

// Per-segment branch length grows from 0 → l_max as the module ages.
// We don't expose lengths directly; verify via the bbox radius, which
// is computed from the grown node-position cache.
TEST(branch_length_grows_with_module_age) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});  // root, a_n=0
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});  // terminal, a_n=0
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.shadeTolerance = 1.0f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 0.0f;
    m.vigor = 0.5f;
    m.light = 1.0f;
    plant.modules.push_back(m);

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.plants.push_back(plant);

    // First tick: small age increment ⇒ small bbox.
    step(world, 0.05f);
    float r0 = world.plants[0].modules[0].bboxRadius;
    ASSERT(r0 > 0.0f, "bbox radius positive after first tick");
    ASSERT(r0 < 0.5f, "bbox radius still small at low age");

    // Many ticks: should saturate near l_max / 2 (centre between root
    // and terminal, length 1 → distance from midpoint is 0.5).
    for (int i = 0; i < 200; ++i) step(world, 0.05f);
    float rMax = world.plants[0].modules[0].bboxRadius;
    ASSERT(rMax > r0, "bbox grew over time");
    ASSERT(rMax <= 0.5f + 1e-3f, "bbox saturates at proto extent");
    ASSERT(rMax >= 0.49f,        "bbox approaches proto extent");
}

// Segment whose ageAtBirth > module age stays at zero length (the node
// has not yet appeared in the development sequence).
TEST(branch_length_zero_before_node_appears) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 10.0f, 1.0f, 1.0f}); // appears late
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.shadeTolerance = 1.0f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 0.0f;
    m.vigor = 0.5f;
    m.light = 1.0f;
    plant.modules.push_back(m);

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.plants.push_back(plant);
    step(world, 0.05f);

    const auto& inst = world.plants[0].modules[0];
    ASSERT(inst.nodePositions.size() == 2, "node cache populated");
    // Terminal still pinned at root position — both nodes coincide.
    float d = vlen(inst.nodePositions[1] - inst.nodePositions[0]);
    ASSERT(d < 1e-5f, "late-appearing node hasn't grown yet");
}
