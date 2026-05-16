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
