#include "test_framework.h"

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
