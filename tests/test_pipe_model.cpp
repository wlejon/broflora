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

// Trunk maintaining thickness through branch junctions:
// Parent module terminating at a child module attachment must not pinch down
// to leafDiameter.
TEST(pipe_model_trunk_maintains_thickness_through_branch_junction) {
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

    // Trunk module 0 (thick bole, d=0.40) with child module 1 (d=0.40) attached at terminal node 1.
    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.diameter = 0.40f;

    BranchModuleInstance child;
    child.prototype = &proto;
    child.parent = 0;
    child.parentAttachTerminal = 1;
    child.diameter = 0.40f;

    plant.modules = {root, child};

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 2u, "two segments emitted");

    // Parent segment's tip radius (at node 1) must maintain thickness (0.5 * 0.40 = 0.20),
    // NOT choking down to leaf radius (0.01).
    ASSERT(segs[0].radius >= 0.20f - 1e-4f, "trunk parent segment maintains thickness (no pinch to leaf radius)");
    ASSERT(segs[0].radius >= segs[1].radius - 1e-4f, "parent segment radius >= child segment radius");
}

// Multi-child junction radius matching pipe-model sum:
// tipR(terminal) = ( sum_{child attached to terminal} (0.5 * child.diameter)^pipeExp )^(1 / pipeExp)
TEST(pipe_model_multi_child_junction_radius_matches_power_sum) {
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

    const float pe = plant.species.pipeExp;
    const float d1 = 0.20f;
    const float d2 = 0.15f;
    const float r1 = 0.5f * d1;
    const float r2 = 0.5f * d2;
    const float expectedPipeR = std::pow(std::pow(r1, pe) + std::pow(r2, pe), 1.0f / pe);

    // Parent module 0 with two children attached at terminal node 1
    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.diameter = 2.0f * expectedPipeR;

    BranchModuleInstance chA;
    chA.prototype = &proto;
    chA.parent = 0;
    chA.parentAttachTerminal = 1;
    chA.diameter = d1;

    BranchModuleInstance chB;
    chB.prototype = &proto;
    chB.parent = 0;
    chB.parentAttachTerminal = 1;
    chB.diameter = d2;

    plant.modules = {root, chA, chB};

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 3u, "3 segments emitted");

    // The parent junction segment radius matches the pipe-model sum (plus subtle junction flare)
    ASSERT(segs[0].radius >= expectedPipeR - 1e-4f, "junction radius at least pipe-model sum");
    ASSERT(segs[0].radius <= expectedPipeR * 1.10f,  "junction radius within flare tolerance");
    ASSERT(segs[0].radius > r1, "junction thicker than child A");
    ASSERT(segs[0].radius > r2, "junction thicker than child B");
}

// Terminal tip properly having species.leafDiameter when no children are attached.
TEST(pipe_model_terminal_tip_has_leaf_diameter) {
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
    plant.species.leafDiameter = 0.035f;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.diameter = plant.species.leafDiameter;
    plant.modules.push_back(m);

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 1u, "one segment emitted");
    const float expectedTipR = 0.5f * plant.species.leafDiameter;
    ASSERT(std::fabs(segs[0].radius - expectedTipR) < 1e-5f,
           "terminal twig segment tip radius equals 0.5 * leafDiameter");
}

// Segments reflecting continuous radii across module hierarchy.
TEST(pipe_model_segments_reflect_continuous_radii) {
    static BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 0.5f, 0.0f}, 0.2f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.4f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.edges.push_back({1, 2});
    proto.rootNode = 0;
    proto.terminalNodes = {2};

    Plant plant;
    plant.species = {};
    plant.species.leafDiameter = 0.02f;
    plant.species.pipeExp = 2.5f;

    // Chain: root (mi=0, d=0.30) -> mid (mi=1, d=0.20) -> top (mi=2, d=0.02)
    BranchModuleInstance root; root.prototype = &proto; root.parent = UINT32_MAX; root.diameter = 0.30f;
    BranchModuleInstance mid;  mid.prototype  = &proto; mid.parent  = 0;          mid.parentAttachTerminal = 2; mid.diameter = 0.20f;
    BranchModuleInstance top;  top.prototype  = &proto; top.parent  = 1;          top.parentAttachTerminal = 2; top.diameter = 0.02f;
    plant.modules = {root, mid, top};

    auto segs = emitPlantSegments(plant);
    ASSERT(segs.size() == 6u, "6 segments emitted (2 per module)");

    // Segment radii should decrease monotonically from base to crown tip
    for (size_t i = 1; i < segs.size(); ++i) {
        ASSERT(segs[i - 1].radius >= segs[i].radius - 1e-4f,
               "segment radii monotonically non-increasing along trunk");
    }
    // Crown tip must be leaf radius
    ASSERT(std::fabs(segs.back().radius - 0.01f) < 1e-5f, "top tip has leaf radius");
}
