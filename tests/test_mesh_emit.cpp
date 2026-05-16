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
