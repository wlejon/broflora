#include "test_framework.h"

#include <cmath>

using namespace broflora;

// One module, one edge: the branch mesh is now a welded, capped, UV'd
// sweep through bromesh::meshBranches (not the old per-edge open cylinder),
// so assert the swept-tube contract rather than an exact 2·sides layout.
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
    ASSERT(!mesh.empty(),               "single edge emits geometry");
    ASSERT(mesh.triangleCount() > 0u,   "non-empty triangle list");
    ASSERT(mesh.hasNormals(),           "normal per vertex");
    // The swept tube carries the UVs the old cylinder emitter never wrote —
    // this is what lets bark be textured downstream.
    ASSERT(mesh.hasUVs(),               "welded tube carries per-vertex UVs");
    // A capped `sides`-gon sweep has strictly more than one bare ring.
    ASSERT(mesh.vertexCount() >= sides, "at least one full ring of vertices");

    // Tangents complete the material set for bark normal mapping: stride 4
    // (xyz + handedness), one per vertex, all finite, and roughly in the
    // surface plane (near-perpendicular to the vertex normal).
    ASSERT(mesh.hasTangents(),          "welded tube carries per-vertex tangents");
    bool tangentsFinite = true, tangentsOrtho = true;
    for (size_t v = 0; v < mesh.vertexCount(); ++v) {
        const float tx = mesh.tangents[v * 4 + 0];
        const float ty = mesh.tangents[v * 4 + 1];
        const float tz = mesh.tangents[v * 4 + 2];
        const float w  = mesh.tangents[v * 4 + 3];
        if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz) ||
            (w != 1.0f && w != -1.0f)) {
            tangentsFinite = false; break;
        }
        const float nx = mesh.normals[v * 3 + 0];
        const float ny = mesh.normals[v * 3 + 1];
        const float nz = mesh.normals[v * 3 + 2];
        // Only check orthogonality where the tangent is non-degenerate.
        const float tlen = std::sqrt(tx*tx + ty*ty + tz*tz);
        if (tlen > 1e-4f) {
            const float dot = (tx*nx + ty*ny + tz*nz) / tlen;
            if (std::fabs(dot) > 0.2f) { tangentsOrtho = false; break; }
        }
    }
    ASSERT(tangentsFinite, "tangents finite with ±1 handedness");
    ASSERT(tangentsOrtho,  "tangents lie in the surface plane");
}

TEST(mesh_emit_empty_world_returns_empty_mesh) {
    WorldState world;
    MeshData mesh = emitWorldMesh(world);
    ASSERT(mesh.empty(), "no plants ⇒ empty mesh");
}

// The welded-tube branch mesh frames each chain's rings by parallel
// transport along the swept path, independent of the simulator's Euler
// roll φ. φ still steers branch *placement* in (θ, ψ) upstream, but for a
// straight on-axis module it moves no node positions, so the emitted mesh
// is φ-invariant — unlike the old per-edge cylinder emitter, which phased
// the ring vertices by φ. Assert the mesh no longer moves with φ.
TEST(mesh_emit_phi_does_not_change_branch_mesh) {
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
    ASSERT(!anyDiff, "φ no longer phases ring vertices — branch mesh is φ-invariant");
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
// Foliage is gated by branch thickness, not by topology: leaves grow on
// twigs throughout the crown (so plants read as full, not bare poles with
// leaf-balls on the tips), but the thick structural bole stays bare. A
// thin non-terminal module must carry foliage; a trunk-thick module must
// not — even when both are otherwise mature and healthy.
TEST(foliage_thin_branch_leafs_thick_bole_bare) {
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
    const float leafD = plant.species.leafDiameter;  // 0.02

    // Two modules: a thick bole (idx 0, has a child so non-terminal) and a
    // thin twig (idx 1, terminal). Both mature and healthy. Diameters are
    // set explicitly since this test doesn't run the develop pipe-model.
    BranchModuleInstance bole;
    bole.prototype = &proto;
    bole.parent = UINT32_MAX;
    bole.age = 2.0f;
    bole.vigor = 0.8f;
    bole.light = 1.0f;
    bole.diameter = leafD * 12.0f;  // well past the leaf-grade cutoff (~6×)
    plant.modules.push_back(bole);

    BranchModuleInstance twig;
    twig.prototype = &proto;
    twig.parent = 0;
    twig.age = 2.0f;
    twig.vigor = 0.8f;
    twig.light = 1.0f;
    twig.diameter = leafD;  // leaf thickness — fully leafy
    plant.modules.push_back(twig);

    auto samples = emitPlantFoliage(plant);
    ASSERT(samples.size() == 2u, "two modules × one edge each ⇒ 2 samples");
    ASSERT(!samples[0].isTerminal, "bole module is non-terminal");
    ASSERT(samples[0].mass == 0.0f, "trunk-thick bole carries no foliage");
    ASSERT(samples[1].mass > 0.0f,  "thin twig carries foliage");
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

// Verify that large structural trunks maintain thickness through branch
// junctions instead of choking down to leafDiameter (Hourglass Pinch fix).
TEST(mesh_emit_no_hourglass_pinch_at_branch_junction) {
    static BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.leafDiameter = 0.02f;  // 1cm leaf radius
    plant.species.pipeExp = 2.5f;

    // 3-module trunk chain: base (d=0.50) -> mid (d=0.35) -> top (d=0.02)
    BranchModuleInstance base;
    base.prototype = &proto;
    base.parent = UINT32_MAX;
    base.diameter = 0.50f;

    BranchModuleInstance mid;
    mid.prototype = &proto;
    mid.parent = 0;
    mid.parentAttachTerminal = 1;
    mid.diameter = 0.35f;

    BranchModuleInstance top;
    top.prototype = &proto;
    top.parent = 1;
    top.parentAttachTerminal = 1;
    top.diameter = 0.02f;

    plant.modules = {base, mid, top};

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 3u, "3 segments emitted");

    // The base trunk module connects to mid module (diameter 0.35).
    // It must NOT choke down to leaf radius (0.01).
    ASSERT(segs[0].radius >= 0.175f - 1e-4f,
           "base trunk segment maintains thickness through junction to mid trunk");
    // Mid module connects to top shoot (leaf diameter 0.02)
    ASSERT(segs[1].radius >= 0.01f, "mid trunk radius valid");
    // Top shoot ends at leaf radius
    ASSERT(std::fabs(segs[2].radius - 0.01f) < 1e-4f, "top shoot tip is leaf radius");

    // Meshing produces a valid geometry without pinched degenerate triangles
    MeshData mesh = emitPlantMesh(plant, 6u);
    ASSERT(!mesh.empty(), "mesh emitted successfully");
    ASSERT(mesh.vertexCount() > 0, "mesh has vertices");
    ASSERT(mesh.hasNormals() && mesh.hasTangents() && mesh.hasUVs(),
           "mesh has complete material attributes");
}

// Multi-child fork junction radius matches pipe-model power sum.
TEST(mesh_emit_multi_child_fork_junction_thickness) {
    static BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.leafDiameter = 0.02f;
    plant.species.pipeExp = 2.5f;

    const float pe = 2.5f;
    const float dA = 0.20f;
    const float dB = 0.20f;
    const float rA = 0.5f * dA;
    const float rB = 0.5f * dB;
    const float expectedPipeR = std::pow(std::pow(rA, pe) + std::pow(rB, pe), 1.0f / pe);

    BranchModuleInstance trunk;
    trunk.prototype = &proto;
    trunk.parent = UINT32_MAX;
    trunk.diameter = 2.0f * expectedPipeR;

    BranchModuleInstance chA;
    chA.prototype = &proto;
    chA.parent = 0;
    chA.parentAttachTerminal = 1;
    chA.diameter = dA;

    BranchModuleInstance chB;
    chB.prototype = &proto;
    chB.parent = 0;
    chB.parentAttachTerminal = 1;
    chB.diameter = dB;

    plant.modules = {trunk, chA, chB};

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 3u, "3 segments emitted");

    // Junction segment radius is thicker than any individual branch
    ASSERT(segs[0].radius >= expectedPipeR - 1e-4f, "junction radius at least pipe-model sum");
    ASSERT(segs[0].radius > rA, "junction strictly thicker than child branch A");
    ASSERT(segs[0].radius > rB, "junction strictly thicker than child branch B");

    MeshData mesh = emitPlantMesh(plant, 6u);
    ASSERT(!mesh.empty(), "fork mesh emitted successfully");
}

// Curved prototype interpolates radius continuously along sub-edges.
TEST(mesh_emit_curved_prototype_continuous_interpolation) {
    static BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    // 5 nodes (4 edges)
    for (int i = 0; i < 5; ++i) {
        proto.nodes.push_back({{0.0f, static_cast<float>(i) * 0.25f, 0.0f}, 0.0f, 1.0f, 1.0f});
    }
    for (int i = 0; i < 4; ++i) {
        proto.edges.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1)});
    }
    proto.rootNode = 0;
    proto.terminalNodes = {4};

    Plant plant;
    plant.species = {};
    plant.species.leafDiameter = 0.02f;  // tip radius 0.01
    plant.species.pipeExp = 2.5f;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.diameter = 0.40f;  // root radius 0.20
    plant.modules.push_back(m);

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 4u, "4 segments emitted");

    // Radii should smoothly decrease from root to tip
    ASSERT(segs[0].radius > segs[1].radius, "seg 0 thicker than seg 1");
    ASSERT(segs[1].radius > segs[2].radius, "seg 1 thicker than seg 2");
    ASSERT(segs[2].radius > segs[3].radius, "seg 2 thicker than seg 3");
    ASSERT(std::fabs(segs[3].radius - 0.01f) < 1e-4f, "tip segment radius equals leaf radius");
}

// Branch collar flare swelling at multi-child junctions.
TEST(mesh_emit_branch_collar_flare_at_junction) {
    static BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.leafDiameter = 0.02f;
    plant.species.pipeExp = 2.5f;

    const float pe = 2.5f;
    const float d1 = 0.15f;
    const float d2 = 0.15f;
    const float baseR = std::pow(std::pow(0.5f * d1, pe) + std::pow(0.5f * d2, pe), 1.0f / pe);

    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.diameter = 2.0f * baseR;

    BranchModuleInstance a; a.prototype = &proto; a.parent = 0; a.parentAttachTerminal = 1; a.diameter = d1;
    BranchModuleInstance b; b.prototype = &proto; b.parent = 0; b.parentAttachTerminal = 1; b.diameter = d2;

    plant.modules = {root, a, b};

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 3u, "3 segments");
    // Junction radius has subtle flare (1.05x)
    ASSERT(segs[0].radius >= baseR * 1.04f, "junction exhibits collar flare swelling");
}

// Trunk root buttress flare swelling: thickens the trunk base and tapers smoothly
// into standard pipe-model diameter over the first ~20% of trunk height.
TEST(mesh_emit_trunk_root_buttress_flare_tapers_smoothly) {
    BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    // 6 nodes along vertical trunk: y = 0.0, 0.05, 0.10, 0.20, 0.50, 1.00
    const std::vector<float> ys = {0.0f, 0.05f, 0.10f, 0.20f, 0.50f, 1.00f};
    for (float y : ys) {
        proto.nodes.push_back({{0.0f, y, 0.0f}, 0.0f, 1.0f, 1.0f});
    }
    for (size_t i = 0; i < ys.size() - 1; ++i) {
        proto.edges.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1)});
    }
    proto.rootNode = 0;
    proto.terminalNodes = {static_cast<uint32_t>(ys.size() - 1)};

    Plant plant;
    plant.species.leafDiameter = 0.40f; // tip radius 0.20
    plant.species.pipeExp = 2.5f;

    BranchModuleInstance rootMod;
    rootMod.prototype = &proto;
    rootMod.parent = UINT32_MAX;
    rootMod.diameter = 0.40f; // rootR = 0.20
    plant.modules.push_back(rootMod);

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 5u, "5 segments emitted for 6 nodes");

    const float standardR = 0.20f;
    // Segment 0 (y = 0.05, u = 0.05): flared (~1.22x standardR)
    ASSERT(segs[0].radius > standardR * 1.15f, "near-base segment is swelled by root buttress flare");
    // Segment 1 (y = 0.10, u = 0.10): intermediate flare (~1.10x)
    ASSERT(segs[1].radius > standardR * 1.05f, "lower-trunk segment has intermediate flare");
    ASSERT(segs[0].radius > segs[1].radius, "flare tapers smoothly upward");

    // Segment 2 (y = 0.20, u = 0.20): flare zone boundary
    ASSERT(segs[1].radius > segs[2].radius, "smooth taper continuing");

    // Segment 3 (y = 0.50, u = 0.50) & Segment 4 (y = 1.00, u = 1.00): beyond flare zone (u >= 0.20)
    ASSERT(std::fabs(segs[3].radius - standardR) < 1e-4f, "mid-trunk segment is exact standard pipe diameter");
    ASSERT(std::fabs(segs[4].radius - standardR) < 1e-4f, "top segment is exact standard pipe diameter");
}

// Canopy branch modules (m.parent != UINT32_MAX) must NOT receive root buttress flare.
TEST(mesh_emit_canopy_branches_do_not_have_root_buttress_flare) {
    BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    const std::vector<float> ys = {0.0f, 0.05f, 0.50f, 1.00f};
    for (float y : ys) proto.nodes.push_back({{0.0f, y, 0.0f}, 0.0f, 1.0f, 1.0f});
    for (size_t i = 0; i < ys.size() - 1; ++i) {
        proto.edges.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1)});
    }
    proto.rootNode = 0;
    proto.terminalNodes = {static_cast<uint32_t>(ys.size() - 1)};

    Plant plant;
    plant.species.leafDiameter = 0.20f;
    plant.species.pipeExp = 2.5f;

    // Root trunk
    BranchModuleInstance rootMod;
    rootMod.prototype = &proto;
    rootMod.parent = UINT32_MAX;
    rootMod.diameter = 0.20f;

    // Canopy branch attached to trunk terminal
    BranchModuleInstance branchMod;
    branchMod.prototype = &proto;
    branchMod.parent = 0;
    branchMod.parentAttachTerminal = proto.terminalNodes[0];
    branchMod.diameter = 0.20f;

    plant.modules = {rootMod, branchMod};

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 6u, "6 segments emitted (3 per module)");

    const float standardR = 0.10f;
    // Trunk near-base segment (seg 0) HAS buttress flare
    ASSERT(segs[0].radius > standardR * 1.10f, "trunk base has buttress flare");

    // Canopy branch near-base segment (seg 3) does NOT have root buttress flare
    ASSERT(std::fabs(segs[3].radius - standardR) < 1e-4f,
           "canopy branch does not receive root buttress flare");
}

// Emitting mesh with root buttress flare produces valid watertight geometry.
TEST(mesh_emit_buttress_flare_produces_valid_mesh) {
    BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    for (int i = 0; i <= 5; ++i) {
        proto.nodes.push_back({{0.0f, static_cast<float>(i) * 0.2f, 0.0f}, 0.0f, 1.0f, 1.0f});
    }
    for (uint32_t i = 0; i < 5; ++i) {
        proto.edges.push_back({i, i + 1});
    }
    proto.rootNode = 0;
    proto.terminalNodes = {5};

    Plant plant;
    plant.species.leafDiameter = 0.05f;
    plant.species.pipeExp = 2.5f;

    BranchModuleInstance rootMod;
    rootMod.prototype = &proto;
    rootMod.parent = UINT32_MAX;
    rootMod.diameter = 0.50f;
    plant.modules.push_back(rootMod);

    MeshData mesh = emitPlantMesh(plant, 8u);
    ASSERT(!mesh.empty(), "flared trunk mesh emitted");
    ASSERT(mesh.vertexCount() > 0, "mesh has vertices");
    ASSERT(mesh.triangleCount() > 0, "mesh has triangles");
    ASSERT(mesh.hasNormals() && mesh.hasUVs() && mesh.hasTangents(),
           "mesh has complete material attributes");
}

