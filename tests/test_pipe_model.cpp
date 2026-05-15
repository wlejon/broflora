#include "test_framework.h"

using namespace broflora;

// Three-module chain: root → mid → leaf. Pipe-model requires
//   d_root >= d_mid >= d_leaf  (sums of pe-th powers).
TEST(pipe_model_diameters_nondecreasing_toward_root) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;
    plant.species.leafDiameter = 0.02f;
    plant.species.pipeExp = 2.5f;

    // Chain of three modules — root, mid, leaf.
    for (int i = 0; i < 3; ++i) {
        BranchModuleInstance m;
        m.prototype = &proto;
        m.parent = (i == 0) ? UINT32_MAX : static_cast<uint32_t>(i - 1);
        m.light = 1.0f;
        m.vigor = 0.5f;
        plant.modules.push_back(m);
    }

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.plants.push_back(plant);

    step(world, 0.1f);

    const auto& p = world.plants.front();
    ASSERT(p.modules.size() == 3, "three modules retained");
    ASSERT(p.modules[2].diameter == p.species.leafDiameter, "leaf diameter == species.leafDiameter");
    ASSERT(p.modules[1].diameter >= p.modules[2].diameter - 1e-6f, "mid >= leaf");
    ASSERT(p.modules[0].diameter >= p.modules[1].diameter - 1e-6f, "root >= mid");
}

// Two terminals → root should be bigger than either single child.
TEST(pipe_model_branching_thickens_parent) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species = {};
    plant.species.leafDiameter = 0.02f;
    plant.species.pipeExp = 2.5f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    // Root with two leaf children.
    BranchModuleInstance root;  root.prototype = &proto;  root.parent = UINT32_MAX;
    BranchModuleInstance a;     a.prototype    = &proto;  a.parent    = 0;
    BranchModuleInstance b;     b.prototype    = &proto;  b.parent    = 0;
    for (auto* m : {&root, &a, &b}) { m->light = 1.0f; m->vigor = 0.5f; }
    plant.modules = {root, a, b};

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.plants.push_back(plant);

    step(world, 0.1f);

    const auto& p = world.plants.front();
    float child = p.modules[1].diameter;
    float rootD = p.modules[0].diameter;
    // d_root = (2 · child^2.5)^(1/2.5) > child
    ASSERT(rootD > child, "branching parent strictly thicker than one child");
}
