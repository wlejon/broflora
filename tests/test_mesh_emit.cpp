#include "test_framework.h"

#include <cmath>

using namespace broflora;

// One module, one edge: expect a single cylinder of `sides` segments —
// 2·sides vertices and 2·sides triangles.
TEST(mesh_emit_single_module_single_edge) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
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

    // Run enough ticks for the segment to grow some length.
    for (int i = 0; i < 30; ++i) step(world, 0.1f);

    const uint32_t sides = 6;
    MeshData mesh = emitPlantMesh(world.plants.front(), sides);
    ASSERT(mesh.vertexCount() == 2u * sides,    "2·sides vertices");
    ASSERT(mesh.triangleCount() == 2u * sides,  "2·sides triangles");
    ASSERT(mesh.normals.size() == mesh.positions.size(), "normal per vertex");
}

TEST(mesh_emit_empty_world_returns_empty_mesh) {
    WorldState world;
    MeshData mesh = emitWorldMesh(world);
    ASSERT(mesh.empty(), "no plants ⇒ empty mesh");
}

// φ on BranchModuleInstance::orientation rotates the cylinder ring as a
// phase offset around the segment axis. The two meshes should have the
// same vertex count and the same set of radii, but a non-zero φ must
// produce at least one ring vertex that differs from the φ=0 mesh — i.e.
// the renderer-visible tangent frame actually moves with φ.
TEST(mesh_emit_phi_rotates_ring_around_axis) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    auto buildMesh = [&](float phi) {
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
        m.orientation.phi = phi;
        plant.modules.push_back(m);

        WorldState world;
        world.shadow.qg.assign(1, 1.0f);
        world.plants.push_back(plant);
        for (int i = 0; i < 30; ++i) step(world, 0.1f);
        return emitPlantMesh(world.plants.front(), 6u);
    };

    MeshData a = buildMesh(0.0f);
    MeshData b = buildMesh(0.5f);  // ~28.6°
    ASSERT(a.positions.size() == b.positions.size(),
           "φ must not change vertex count");

    bool anyDiff = false;
    for (size_t i = 0; i < a.positions.size(); ++i) {
        if (std::fabs(a.positions[i] - b.positions[i]) > 1e-4f) {
            anyDiff = true; break;
        }
    }
    ASSERT(anyDiff, "non-zero φ must rotate ring vertices");
}

// emitPlantSegments — empty plant should yield no segments.
TEST(segments_empty_plant_returns_empty) {
    Plant plant;
    auto segs = emitPlantSegments(plant);
    ASSERT(segs.empty(), "no modules ⇒ no segments");
}

TEST(segments_empty_world_returns_empty) {
    WorldState world;
    auto segs = emitWorldSegments(world);
    ASSERT(segs.empty(), "no plants ⇒ no segments");
}

// Single-module Y prototype: 1 root + 2 terminals, 2 edges out of the
// root. Both edges start at the module's root node and the module has no
// parent module, so both segments must be roots (parent = -1, depth = 0).
TEST(segments_y_module_both_edges_are_roots) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{ 0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{ 0.3f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{-0.3f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.edges.push_back({0, 2});
    proto.rootNode = 0;
    proto.terminalNodes = {1, 2};

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
    for (int i = 0; i < 30; ++i) step(world, 0.1f);

    auto segs = emitPlantSegments(world.plants.front());
    ASSERT(segs.size() == 2u, "Y prototype emits 2 segments");
    ASSERT(segs[0].parent == -1, "edge 0 has no parent");
    ASSERT(segs[1].parent == -1, "edge 1 has no parent");
    ASSERT(segs[0].depth == 0,   "edge 0 is a root segment");
    ASSERT(segs[1].depth == 0,   "edge 1 is a root segment");
    // Tip radii are positive and ≤ stem radius (taper toward leafDiameter).
    ASSERT(segs[0].radius > 0.0f, "non-zero tip radius");
    ASSERT(segs[1].radius > 0.0f, "non-zero tip radius");
}

// Chain prototype: root → mid → tip via two edges. The second segment
// should reference the first as its parent and have depth 1.
TEST(segments_chain_links_parent_within_module) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 0.5f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.edges.push_back({1, 2});
    proto.rootNode = 0;
    proto.terminalNodes = {2};

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
    for (int i = 0; i < 30; ++i) step(world, 0.1f);

    auto segs = emitPlantSegments(world.plants.front());
    ASSERT(segs.size() == 2u, "chain emits 2 segments");
    ASSERT(segs[0].parent == -1, "first segment is a root");
    ASSERT(segs[0].depth == 0,   "first segment depth is 0");
    ASSERT(segs[1].parent == 0,  "second segment chains to first");
    ASSERT(segs[1].depth == 1,   "second segment depth is 1");
}

// emitWorldSegments must continue counting parent indices across plant
// boundaries — the per-plant parent linkage stays valid but no segment
// should reference an earlier plant's segments.
TEST(segments_world_continues_indices_across_plants) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    auto makePlant = [&](float x) {
        Plant plant;
        plant.species = {};
        plant.species.shadeTolerance = 1.0f;
        plant.effectiveRootVigorMax = plant.species.rootVigorMax;
        plant.origin = {x, 0.0f, 0.0f};

        BranchModuleInstance m;
        m.prototype = &proto;
        m.parent = UINT32_MAX;
        m.age = 0.0f;
        m.vigor = 0.5f;
        m.light = 1.0f;
        plant.modules.push_back(m);
        return plant;
    };

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.plants.push_back(makePlant(0.0f));
    world.plants.push_back(makePlant(2.0f));
    for (int i = 0; i < 30; ++i) step(world, 0.1f);

    auto segs = emitWorldSegments(world);
    ASSERT(segs.size() == 2u, "one segment per plant");
    ASSERT(segs[0].parent == -1, "plant-0 segment is a root");
    ASSERT(segs[1].parent == -1, "plant-1 segment is a root");
    // Each plant's segment must originate at its own plant.origin.x.
    ASSERT(std::fabs(segs[0].from.x - 0.0f) < 1e-4f, "plant-0 anchored at x=0");
    ASSERT(std::fabs(segs[1].from.x - 2.0f) < 1e-4f, "plant-1 anchored at x=2");
}

// emitPlantFoliage — empty plant should yield no samples.
TEST(foliage_empty_plant_returns_empty) {
    Plant plant;
    auto samples = emitPlantFoliage(plant);
    ASSERT(samples.empty(), "no modules ⇒ no samples");
}

// Lockstep contract: foliage sample count must equal segment count for
// every plant, and indices align. Walk shape is module → edges, same
// order for both functions — this is the invariant downstream code relies
// on to zip the two outputs.
TEST(foliage_samples_match_segments_one_for_one) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 0.5f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.edges.push_back({1, 2});
    proto.rootNode = 0;
    proto.terminalNodes = {2};

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
    for (int i = 0; i < 30; ++i) step(world, 0.1f);

    auto segs    = emitPlantSegments(world.plants.front());
    auto samples = emitPlantFoliage(world.plants.front());
    ASSERT(segs.size() == samples.size(), "one sample per segment");
}

// Default mass policy: a single mature terminal module with healthy
// vigor must yield mass > 0 on all its segments.
TEST(foliage_mature_terminal_has_positive_mass) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.shadeTolerance = 1.0f;
    plant.species.moduleMatureAge = 0.5f;  // mature fast
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 2.0f;          // well past mature
    m.vigor = 0.8f;        // healthy
    m.light = 1.0f;
    plant.modules.push_back(m);

    auto samples = emitPlantFoliage(plant);
    ASSERT(samples.size() == 1u, "one segment ⇒ one sample");
    ASSERT(samples[0].isTerminal,    "single module is terminal");
    ASSERT(samples[0].mass > 0.0f,   "mature healthy terminal carries foliage");
    ASSERT(samples[0].age01 >= 1.0f, "age past mature gate");
}

// Non-terminal modules carry no foliage under the default policy. A
// parent module with a child attached must have mass = 0 on its
// segments even when otherwise mature.
TEST(foliage_non_terminal_module_has_zero_mass) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.shadeTolerance = 1.0f;
    plant.species.moduleMatureAge = 0.5f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    // Two modules: parent (idx 0) and child (idx 1). Both fully mature.
    BranchModuleInstance parent;
    parent.prototype = &proto;
    parent.parent = UINT32_MAX;
    parent.age = 2.0f;
    parent.vigor = 0.8f;
    parent.light = 1.0f;
    plant.modules.push_back(parent);

    BranchModuleInstance child;
    child.prototype = &proto;
    child.parent = 0;
    child.age = 2.0f;
    child.vigor = 0.8f;
    child.light = 1.0f;
    plant.modules.push_back(child);

    auto samples = emitPlantFoliage(plant);
    ASSERT(samples.size() == 2u, "two modules × one edge each ⇒ 2 samples");
    ASSERT(!samples[0].isTerminal, "parent module is non-terminal");
    ASSERT(samples[0].mass == 0.0f, "non-terminal carries no foliage");
    ASSERT(samples[1].isTerminal,  "child module is terminal");
    ASSERT(samples[1].mass > 0.0f, "terminal child carries foliage");
}

// Senescence ramp: 0 below maxAge, climbs linearly over the next 20%,
// 1 thereafter. Verified at three points.
TEST(foliage_senescence_ramp_engages_past_max_age) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    auto sampleAt = [&](float plantAge) {
        Plant plant;
        plant.species = {};
        plant.species.maxAge = 10.0f;
        plant.species.moduleMatureAge = 0.5f;
        plant.effectiveRootVigorMax = plant.species.rootVigorMax;
        plant.age = plantAge;

        BranchModuleInstance m;
        m.prototype = &proto;
        m.parent = UINT32_MAX;
        m.age = 2.0f;
        m.vigor = 0.8f;
        m.light = 1.0f;
        plant.modules.push_back(m);

        auto samples = emitPlantFoliage(plant);
        return samples.empty() ? FoliageSample{} : samples[0];
    };

    auto young  = sampleAt( 5.0f);    // well before maxAge
    auto peak   = sampleAt(11.0f);    // 50% into the 20% window
    auto past   = sampleAt(13.0f);    // past the window

    ASSERT(young.senescence01 == 0.0f, "below maxAge: senescence = 0");
    ASSERT(peak.senescence01 > 0.4f && peak.senescence01 < 0.6f,
           "mid-window: senescence ≈ 0.5");
    ASSERT(past.senescence01 == 1.0f, "past window: senescence saturates at 1");
}

// emitPlantBloomAnchors — empty plant should yield no anchors.
TEST(bloom_empty_plant_returns_empty) {
    Plant plant;
    auto anchors = emitPlantBloomAnchors(plant);
    ASSERT(anchors.empty(), "no modules ⇒ no anchors");
}

// Non-flowering plant: even with mature terminal modules, no anchors.
// Flowering gate is a hard prerequisite.
TEST(bloom_non_flowering_plant_yields_no_anchors) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{ 0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{ 0.3f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{-0.3f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.edges.push_back({0, 2});
    proto.rootNode = 0;
    proto.terminalNodes = {1, 2};

    Plant plant;
    plant.species = {};
    plant.species.moduleMatureAge = 0.5f;
    plant.flowering = false;          // hard gate
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 2.0f;
    m.vigor = 0.8f;
    m.light = 1.0f;
    plant.modules.push_back(m);

    auto anchors = emitPlantBloomAnchors(plant);
    ASSERT(anchors.empty(), "!flowering ⇒ no anchors");
}

// Flowering Y plant: one terminal module with two terminal nodes —
// expect two anchors, one per terminal. Positions should match the
// terminal-node world positions exactly (same worldNodePos consumer as
// the segment emitter).
TEST(bloom_flowering_y_module_emits_per_terminal_node) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{ 0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{ 0.3f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{-0.3f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.edges.push_back({0, 2});
    proto.rootNode = 0;
    proto.terminalNodes = {1, 2};

    Plant plant;
    plant.species = {};
    plant.species.moduleMatureAge = 0.5f;
    plant.flowering = true;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 2.0f;
    m.vigor = 0.8f;
    m.light = 1.0f;
    plant.modules.push_back(m);

    auto anchors = emitPlantBloomAnchors(plant);
    ASSERT(anchors.size() == 2u, "2 terminal nodes ⇒ 2 anchors");

    // Default species has gravity tropism = -Y. With age 2 the τ
    // displacement is non-trivial, so anchors won't be at the static
    // prototype positions exactly, but the two anchors should still be
    // on opposite sides in X.
    ASSERT(anchors[0].position.x > 0.0f && anchors[1].position.x < 0.0f,
           "Y terminals land on opposite X sides");
    ASSERT(anchors[0].vigor01 > 0.0f && anchors[1].vigor01 > 0.0f,
           "vigor scalar populated");

    // Normal must be unit-length and point "outward" — y-component
    // positive since both terminals are above the root.
    auto isUnit = [](const bromath::Vec3& v) {
        const float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        return std::fabs(len - 1.0f) < 1e-3f;
    };
    ASSERT(isUnit(anchors[0].normal),    "normal is unit-length");
    ASSERT(isUnit(anchors[1].normal),    "normal is unit-length");
    ASSERT(anchors[0].normal.y > 0.0f,   "outward normal points up-ish");
    ASSERT(anchors[1].normal.y > 0.0f,   "outward normal points up-ish");
}

// Two-module plant: parent (non-terminal) has terminals, but no anchors
// should land on its terminal nodes — only on the child (terminal)
// module's terminals.
TEST(bloom_non_terminal_module_skipped) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.moduleMatureAge = 0.5f;
    plant.flowering = true;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance parent;
    parent.prototype = &proto;
    parent.parent = UINT32_MAX;
    parent.age = 2.0f;
    parent.vigor = 0.8f;
    parent.light = 1.0f;
    plant.modules.push_back(parent);

    BranchModuleInstance child;
    child.prototype = &proto;
    child.parent = 0;
    child.age = 2.0f;
    child.vigor = 0.8f;
    child.light = 1.0f;
    plant.modules.push_back(child);

    auto anchors = emitPlantBloomAnchors(plant);
    ASSERT(anchors.size() == 1u, "only the terminal child contributes");
}
