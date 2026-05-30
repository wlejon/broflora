#include "test_framework.h"

#include <cmath>
#include <utility>
#include <vector>

using namespace broflora;

namespace {

// Build a TerrainMap covering [0, width·cs] × [0, depth·cs] with the
// given heights, row-major over (ix, iz) with stride `depth`.
TerrainMap makeTerrain(uint32_t width, uint32_t depth, float cellSize,
                       std::vector<float> heights) {
    TerrainMap t;
    t.footprint.origin   = {0.0f, 0.0f};
    t.footprint.cellSize = cellSize;
    t.footprint.width    = width;
    t.footprint.depth    = depth;
    t.height = std::move(heights);
    return t;
}

BranchModulePrototype simpleProto() {
    BranchModulePrototype p;
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    p.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    p.edges.push_back({0, 1});
    p.rootNode = 0;
    p.terminalNodes = {1};
    return p;
}

} // namespace

// 4×4 cells of cellSize=1, all height = 5. After one step, every plant
// origin.y should be snapped to 5, regardless of what the caller set.
TEST(terrain_origin_y_snaps_per_tick) {
    WorldState world;
    world.terrain = makeTerrain(4, 4, 1.0f, std::vector<float>(16, 5.0f));
    world.shadow.qg.assign(1, 1.0f);
    uint32_t pi = addPrototype(world, simpleProto());
    (void)pi;

    Plant p;
    p.species = {};
    p.species.shadeTolerance = 1.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.origin = {1.5f, -99.0f, 1.5f};  // wrong Y on purpose

    BranchModuleInstance m;
    m.prototype = &world.prototypes[pi];
    m.parent = UINT32_MAX;
    m.age = 0.0f; m.vigor = 0.5f; m.light = 1.0f;
    p.modules.push_back(m);

    addPlant(world, std::move(p));

    step(world, 0.1f);
    ASSERT(std::fabs(world.plants.front().origin.y - 5.0f) < 1e-5f,
           "origin.y snapped to terrain height");
}

// Plant outside the footprint must NOT be snapped — fallback preserves
// the caller's Y. Lets bro keep using broflora without a terrain for
// non-terrain worlds.
TEST(terrain_origin_y_unchanged_when_outside_footprint) {
    WorldState world;
    world.terrain = makeTerrain(4, 4, 1.0f, std::vector<float>(16, 5.0f));
    world.shadow.qg.assign(1, 1.0f);
    uint32_t pi = addPrototype(world, simpleProto());

    Plant p;
    p.species = {};
    p.species.shadeTolerance = 1.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.origin = {-10.0f, 7.5f, -10.0f};

    BranchModuleInstance m;
    m.prototype = &world.prototypes[pi];
    m.parent = UINT32_MAX;
    m.age = 0.0f; m.vigor = 0.5f; m.light = 1.0f;
    p.modules.push_back(m);

    addPlant(world, std::move(p));
    step(world, 0.1f);
    ASSERT(std::fabs(world.plants.front().origin.y - 7.5f) < 1e-5f,
           "out-of-footprint origin Y is preserved");
}

// Ramp heightfield along +x: dh/dx = 1, dh/dz = 0 → normal points
// toward (-1, 2, 0)/√5. With terrainAnchorWeight=1 the root module's
// orientation must align growth axis with that normal.
TEST(terrain_anchor_tilts_root_orientation) {
    std::vector<float> h(16);
    for (uint32_t ix = 0; ix < 4; ++ix) {
        for (uint32_t iz = 0; iz < 4; ++iz) {
            h[ix * 4 + iz] = static_cast<float>(ix);  // ramp along +x
        }
    }
    WorldState world;
    world.terrain = makeTerrain(4, 4, 1.0f, std::move(h));
    world.shadow.qg.assign(1, 1.0f);
    uint32_t pi = addPrototype(world, simpleProto());

    Plant p;
    p.species = {};
    p.species.shadeTolerance     = 1.0f;
    p.species.terrainAnchorWeight = 1.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.origin = {1.5f, 0.0f, 1.5f};

    BranchModuleInstance m;
    m.prototype = &world.prototypes[pi];
    m.parent = UINT32_MAX;
    m.age = 0.0f; m.vigor = 0.5f; m.light = 1.0f;
    p.modules.push_back(m);

    addPlant(world, std::move(p));
    step(world, 0.1f);

    // terrainNormalAt central-diffs over ±cellSize and forms
    // (-dhx, 2·cs, -dhz). With slope 1/unit and cs=1 that's (-2,2,0),
    // normalized (-1,1,0)/√2 → ny = 1/√2, nx = -1/√2.
    // θ_target = acos(1/√2) = π/4. With the pitch-then-yaw convention
    // (axis = (sinψ·sinθ, cosθ, cosψ·sinθ)), aligning the axis to the
    // normal needs ψ_target = atan2(nx, nz) = atan2(-1/√2, 0) = -π/2.
    const auto& root = world.plants.front().modules.front();
    float expectedTheta = 0.7853982f;   // π/4
    float expectedPsi   = -1.5707963f;  // -π/2
    ASSERT(std::fabs(root.orientation.theta - expectedTheta) < 1e-3f,
           "root pitch follows terrain slope");
    ASSERT(std::fabs(root.orientation.psi - expectedPsi) < 1e-3f,
           "root yaw follows terrain aspect");
}

// terrainAnchorWeight=0 must leave the root orientation alone (default
// behaviour, backward compat).
TEST(terrain_anchor_disabled_leaves_orientation_alone) {
    std::vector<float> h(16);
    for (uint32_t ix = 0; ix < 4; ++ix) {
        for (uint32_t iz = 0; iz < 4; ++iz) {
            h[ix * 4 + iz] = static_cast<float>(ix);
        }
    }
    WorldState world;
    world.terrain = makeTerrain(4, 4, 1.0f, std::move(h));
    world.shadow.qg.assign(1, 1.0f);
    uint32_t pi = addPrototype(world, simpleProto());

    Plant p;
    p.species = {};
    p.species.shadeTolerance      = 1.0f;
    p.species.terrainAnchorWeight = 0.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.origin = {1.5f, 0.0f, 1.5f};

    BranchModuleInstance m;
    m.prototype = &world.prototypes[pi];
    m.parent = UINT32_MAX;
    m.age = 0.0f; m.vigor = 0.5f; m.light = 1.0f;
    m.orientation.theta = 0.25f;
    m.orientation.psi   = -0.5f;
    p.modules.push_back(m);

    addPlant(world, std::move(p));
    step(world, 0.1f);

    const auto& root = world.plants.front().modules.front();
    ASSERT(std::fabs(root.orientation.theta - 0.25f) < 1e-5f,
           "θ preserved with anchor weight 0");
    ASSERT(std::fabs(root.orientation.psi + 0.5f) < 1e-5f,
           "ψ preserved with anchor weight 0");
}

// Steep ramp (dh/dx = 4 per cell) puts terrain slope at atan(4/2) ≈
// 1.107 rad. Set maxSeedingSlope=0.5 → seeding origins on the ramp
// should be rejected, so no seedling ever appears even when the parent
// plant tries hard.
TEST(terrain_steep_slope_rejects_seeding) {
    std::vector<float> h(16);
    for (uint32_t ix = 0; ix < 4; ++ix) {
        for (uint32_t iz = 0; iz < 4; ++iz) {
            h[ix * 4 + iz] = 4.0f * static_cast<float>(ix);
        }
    }
    BranchModulePrototype proto = simpleProto();
    proto.nodes[1].ageAtBirth = 0.5f;

    WorldState world;
    world.terrain = makeTerrain(4, 4, 1.0f, std::move(h));
    world.shadow.qg.assign(1, 1.0f);
    uint32_t pi = addPrototype(world, std::move(proto));
    addVoronoiSite(world, pi, 0.5f, 0.5f);
    world.rngState = 42ULL;

    Plant p;
    p.species = {};
    p.species.floweringAge    = 0.0f;
    p.species.seedingRadius   = 0.5f;
    p.species.maxAge          = 1000.0f;
    p.species.moduleMatureAge = 1000.0f;
    p.species.maxSeedingSlope = 0.5f;  // ~28.6°; ramp is ~63°
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.origin = {1.5f, 0.0f, 1.5f};
    p.age = 1.0f;
    p.flowering = true;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, pi);
    root.parent = UINT32_MAX;
    root.age = 0.0f; root.light = 1.0f;
    root.vigor = p.species.rootVigorMax;
    p.modules.push_back(root);
    addPlant(world, std::move(p));

    for (int i = 0; i < 100; ++i) step(world, 1.0f);

    ASSERT(world.plants.size() == 1u,
           "no seedlings on slopes steeper than maxSeedingSlope");
}
