#include "test_framework.h"

#include <string>

using namespace broflora;

namespace {

// One-node prototype: a trivial root-only module. Enough to drive
// addPlant / removePlant without exercising spawning / development.
BranchModulePrototype makeTrivialProto(const char* name) {
    BranchModulePrototype p;
    p.name = name;
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 0.05f});
    p.nodes.push_back({{0.0f, 0.5f, 0.0f}, 0.4f, 1.0f, 0.04f});
    p.edges.push_back({0, 1});
    p.rootNode = 0;
    p.terminalNodes = {1};
    return p;
}

uint32_t plantAt(WorldState& world, uint32_t protoIdx, bromath::Vec3 origin) {
    Plant pl;
    pl.species = {};
    pl.origin  = origin;
    pl.effectiveRootVigorMax = pl.species.rootVigorMax;
    BranchModuleInstance root;
    root.prototype = &world.prototypes[protoIdx];
    root.parent    = UINT32_MAX;
    root.vigor     = pl.species.minVigor * 2.0f;
    root.light     = 1.0f;
    pl.modules.push_back(root);
    addPlant(world, std::move(pl));
    return static_cast<uint32_t>(world.plants.size() - 1);
}

} // namespace

TEST(remove_plant_out_of_range_returns_false) {
    WorldState world;
    ASSERT(removePlant(world, 0) == false, "remove on empty world is false");
    ASSERT(removePlant(world, 42) == false, "remove far out of range is false");
}

TEST(remove_plant_basic_swap_and_pop) {
    WorldState world;
    addPrototype(world, makeTrivialProto("a"));

    const uint32_t i0 = plantAt(world, 0, {0.0f, 0.0f, 0.0f});
    const uint32_t i1 = plantAt(world, 0, {10.0f, 0.0f, 0.0f});
    const uint32_t i2 = plantAt(world, 0, {20.0f, 0.0f, 0.0f});
    (void)i0; (void)i1; (void)i2;
    ASSERT(world.plants.size() == 3, "three plants registered");

    // Remove the middle plant — last slot should swap into slot 1.
    const float lastX = world.plants[2].origin.x;
    ASSERT(removePlant(world, 1) == true, "remove middle succeeds");
    ASSERT(world.plants.size() == 2, "size dropped to 2");
    ASSERT(world.plants[1].origin.x == lastX,
           "former last plant moved into slot 1");

    // Remove the last plant directly — no swap needed.
    ASSERT(removePlant(world, 1) == true, "remove last succeeds");
    ASSERT(world.plants.size() == 1, "size dropped to 1");

    // Remove the only remaining plant.
    ASSERT(removePlant(world, 0) == true, "remove sole plant succeeds");
    ASSERT(world.plants.empty(), "world is empty");

    // Validate world structure post-removal.
    std::string err;
    ASSERT(validate(world, &err), ("validate post-removal: " + err).c_str());
}

TEST(remove_plant_add_remove_step_no_crash) {
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.shadow.cellSize = 1.0f;
    world.shadow.width = world.shadow.height = world.shadow.depth = 1;

    addPrototype(world, makeTrivialProto("p"));
    addVoronoiSite(world, 0, 0.5f, 0.5f);

    // Add, remove, add again, step — exercises the slot reuse path.
    plantAt(world, 0, {0.0f, 0.0f, 0.0f});
    plantAt(world, 0, {5.0f, 0.0f, 0.0f});
    plantAt(world, 0, {-5.0f, 0.0f, 0.0f});
    ASSERT(removePlant(world, 0) == true, "remove plant 0");
    plantAt(world, 0, {2.0f, 0.0f, 0.0f});
    ASSERT(world.plants.size() == 3, "still 3 after add-after-remove");

    // Stepping after a removal must not crash and must keep invariants.
    for (int i = 0; i < 5; ++i) step(world, 0.1f);

    std::string err;
    ASSERT(validate(world, &err), ("validate after step: " + err).c_str());

    // Now drain the world via removePlant.
    while (!world.plants.empty()) {
        ASSERT(removePlant(world, 0) == true, "drain plant 0");
    }
    ASSERT(world.plants.empty(), "drained to empty");
    step(world, 0.1f);  // step empty world must be safe
    ASSERT(validate(world, &err), ("validate empty: " + err).c_str());
}
