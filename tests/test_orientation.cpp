#include "test_framework.h"

#include <cmath>

using namespace broflora;

// A heavy neighbour sphere placed on +X next to the parent should push
// the gradient descent to orient the new module away from +X. The
// growth axis ends up with a substantially negative x component
// relative to what the seed (fan-yaw on +X) would have given.
TEST(orientation_avoids_neighbour) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});  // root
    proto.nodes.push_back({{1.0f, 1.0f, 0.0f}, 0.0f, 1.5f, 1.0f});  // single terminal at +X+Y
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, proto);
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance = 1.0f;
    // Soften tropism so the collision term dominates the choice.
    p.species.distributionWeightCollisions = 4.0f;
    p.species.distributionWeightTropism    = 0.1f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;     // mature
    root.vigor = 0.6f;
    root.light = 1.0f;
    // Pre-populated bbox so light eval doesn't reset it; develop will
    // recompute anyway.
    root.bboxCenter = {0.0f, 0.5f, 0.0f};
    root.bboxRadius = 0.8f;
    p.modules.push_back(root);

    world.plants.push_back(p);

    // Inject a "phantom" neighbour module on another plant blocking +X.
    Plant blocker;
    blocker.species = p.species;
    blocker.origin = {2.0f, 1.0f, 0.0f};  // off to +X
    BranchModuleInstance ghost;
    ghost.prototype = prototypeAt(world, protoIdx);
    ghost.parent = UINT32_MAX;
    ghost.age = 1.0f;
    ghost.vigor = 0.6f;
    ghost.light = 1.0f;
    ghost.bboxCenter = {2.0f, 1.0f, 0.0f};
    ghost.bboxRadius = 2.0f;            // large, hard to miss
    blocker.modules.push_back(ghost);
    world.plants.push_back(blocker);

    step(world, 0.1f);

    // First plant should have spawned a child whose growth axis is not
    // pointed at +X.
    const auto& plant0 = world.plants[0];
    ASSERT(plant0.modules.size() == 2, "parent spawned a child");
    const auto& child  = plant0.modules[1];

    // Compute child growth axis from prototype + orientation.
    float cy = std::cos(child.orientation.psi),  sy = std::sin(child.orientation.psi);
    float cp = std::cos(child.orientation.theta), sp = std::sin(child.orientation.theta);
    Vec3 a = {1.0f, 1.0f, 0.0f};  // root → terminal in proto
    Vec3 a1 = { cy * a.x + sy * a.z, a.y, -sy * a.x + cy * a.z };
    Vec3 a2 = { a1.x, cp * a1.y - sp * a1.z, sp * a1.y + cp * a1.z };
    Vec3 axis = v3_normalize(a2);

    // With the blocker on +X, settle should have rotated the axis away
    // from +X — its x component should be no larger than the seed pose's
    // (and ideally clearly less).
    ASSERT(axis.x < 0.6f, "growth axis turned away from blocking neighbour");
}

// Tropism-dominated case: with no neighbours, the descent should drive
// the growth axis close to the up axis (-tropismDir, default +Y).
TEST(orientation_aligns_with_tropism_when_unblocked) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{1.0f, 0.2f, 0.0f}, 0.0f, 1.5f, 1.0f});  // initial axis nearly horizontal
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, proto);
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance = 1.0f;
    p.species.distributionWeightCollisions = 0.1f;
    p.species.distributionWeightTropism    = 4.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;
    root.vigor = 0.6f;
    root.light = 1.0f;
    p.modules.push_back(root);

    world.plants.push_back(p);

    step(world, 0.1f);

    ASSERT(world.plants[0].modules.size() == 2, "parent spawned a child");
    const auto& child = world.plants[0].modules[1];

    // Recompute world axis.
    float cy = std::cos(child.orientation.psi),  sy = std::sin(child.orientation.psi);
    float cp = std::cos(child.orientation.theta), sp = std::sin(child.orientation.theta);
    Vec3 a = {1.0f, 0.2f, 0.0f};
    Vec3 a1 = { cy * a.x + sy * a.z, a.y, -sy * a.x + cy * a.z };
    Vec3 a2 = { a1.x, cp * a1.y - sp * a1.z, sp * a1.y + cp * a1.z };
    Vec3 axis = v3_normalize(a2);

    // Initial axis y is 0.2/sqrt(1.04) ≈ 0.196; settled axis should be
    // visibly more vertical.
    ASSERT(axis.y > 0.5f, "growth axis pulled toward up");
}
