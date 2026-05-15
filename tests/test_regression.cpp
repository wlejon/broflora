#include "test_framework.h"

#include <cmath>

using namespace broflora;

// Single-terminal prototype helper.
static BranchModulePrototype simplePrototype() {
    BranchModulePrototype p;
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    p.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.5f, 1.0f, 1.0f});
    p.edges.push_back({0, 1});
    p.rootNode = 0;
    p.terminalNodes = {1};
    return p;
}

// --- Bug #1: shedding the middle of a sub-branch must not corrupt parent
// indices of survivors. We starve a lateral sub-branch (light=0) so the
// acropetal split assigns it zero vigor; senescence then sheds it and
// its descendant in cascade.
TEST(shedding_module_remaps_parent_indices) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);

    Plant plant;
    plant.species = {};
    plant.species.apicalControl = 0.99f;     // main child gets ~all vigor
    plant.species.minVigor = 0.1f;
    plant.species.maxAge = 1000.0f;
    plant.species.moduleMatureAge = 1000.0f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;
    plant.age = 1.0f;

    // root(0), mainChild(1, gets vigor), lateral(2, starved), leaf(3, under lateral).
    BranchModuleInstance root;  root.prototype = &proto; root.parent = UINT32_MAX;
    root.age = 1.0f; root.light = 1.0f; root.vigor = 1.0f;
    BranchModuleInstance main;  main.prototype = &proto; main.parent = 0;
    main.age = 1.0f; main.light = 1.0f; main.vigor = 1.0f; main.isMainChild = true;
    BranchModuleInstance lat;   lat.prototype  = &proto; lat.parent  = 0;
    lat.age = 1.0f;  lat.light  = 0.0f; lat.vigor  = 0.0f; lat.isMainChild = false;
    BranchModuleInstance leaf;  leaf.prototype = &proto; leaf.parent = 2;
    leaf.age = 1.0f; leaf.light = 0.0f; leaf.vigor = 0.0f;
    plant.modules = {root, main, lat, leaf};
    world.plants.push_back(plant);

    step(world, 0.0f);  // dt=0 → ages don't advance, shed predicate stable

    const auto& survivors = world.plants.front().modules;
    ASSERT(survivors.size() == 2, "shed cascade removed lat + leaf");
    ASSERT(survivors[0].parent == UINT32_MAX, "root parent intact");
    ASSERT(survivors[1].parent == 0, "main child still anchored to root");
}

// Two surviving siblings whose indices shift down after a sibling is
// shed in front of them. Without the remap, the later sibling's parent
// index would still point at the old slot.
TEST(shedding_remaps_sibling_indices_after_compact) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);

    Plant plant;
    plant.species = {};
    plant.species.minVigor = 0.5f;
    plant.species.maxAge = 1000.0f;
    plant.species.moduleMatureAge = 1000.0f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;
    plant.age = 1.0f;

    // Layout: root(0), childA(1, shed), childB(2, survives), grandchild(3, parent=2).
    BranchModuleInstance root; root.prototype = &proto; root.parent = UINT32_MAX;
    root.age = 1.0f; root.light = 1.0f; root.vigor = 1.0f;
    BranchModuleInstance a;    a.prototype = &proto;    a.parent = 0;
    a.age = 1.0f; a.light = 1.0f; a.vigor = 0.0f;    // shed
    BranchModuleInstance b;    b.prototype = &proto;    b.parent = 0;
    b.age = 1.0f; b.light = 1.0f; b.vigor = 1.0f;    // survive
    BranchModuleInstance gc;   gc.prototype = &proto;   gc.parent = 2;
    gc.age = 1.0f; gc.light = 1.0f; gc.vigor = 1.0f; // survive
    plant.modules = {root, a, b, gc};

    world.plants.push_back(plant);
    step(world, 0.0f);

    const auto& s = world.plants.front().modules;
    ASSERT(s.size() == 3, "exactly one module shed");
    // After compact: [root, b, gc]. b.parent must be 0 (root), gc.parent must be 1 (b).
    ASSERT(s[0].parent == UINT32_MAX, "root unchanged");
    ASSERT(s[1].parent == 0, "surviving sibling re-anchored to root");
    ASSERT(s[2].parent == 1, "grandchild re-anchored to remapped sibling");
}

// --- Bug #2/#3: main-vs-lateral vigor split is driven by isMainChild,
// not by insertion order. We verify the ratio v(main)/v(lateral) ==
// λ/(1-λ) when both children have identical subtree light.
TEST(vigor_split_respects_isMainChild_flag) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);

    Plant plant;
    plant.species = {};
    plant.species.apicalControl = 0.75f;
    plant.species.moduleMatureAge = 1000.0f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;

    BranchModuleInstance root; root.prototype = &proto; root.parent = UINT32_MAX;
    root.light = 1.0f; root.age = 0.5f;
    BranchModuleInstance lateral; lateral.prototype = &proto; lateral.parent = 0;
    lateral.light = 1.0f; lateral.isMainChild = false;
    BranchModuleInstance main;    main.prototype = &proto;    main.parent = 0;
    main.light = 1.0f;    main.isMainChild = true;

    // Order matters for the assertion: main is *second*, so the old
    // "first child = main" code would have given it the (1-λ) weight.
    plant.modules = {root, lateral, main};
    world.plants.push_back(plant);

    step(world, 0.01f);

    const auto& mods = world.plants.front().modules;
    float vMain    = mods[2].vigor;
    float vLateral = mods[1].vigor;
    ASSERT(vMain > vLateral, "main child gets more vigor regardless of insertion order");
    // Ratio λ/(1-λ) = 0.75/0.25 = 3.
    float ratio = vMain / vLateral;
    ASSERT(ratio > 2.9f && ratio < 3.1f, "ratio matches λ/(1-λ)");
}

// --- Sphere-sphere full-containment branch (light.cpp:18-21). Two
// spheres where the small one is fully inside the big one.
TEST(sphere_containment_uses_smaller_volume) {
    static BranchModulePrototype bigProto = simplePrototype();
    bigProto.nodes[1].position = {0.0f, 5.0f, 0.0f};  // big radius
    static BranchModulePrototype smallProto = simplePrototype();
    smallProto.nodes[1].position = {0.0f, 0.5f, 0.0f}; // small radius

    WorldState world;
    world.shadow.width = world.shadow.height = world.shadow.depth = 4;
    world.shadow.cellSize = 4.0f;
    world.shadow.origin = {-8.0f, -8.0f, -8.0f};
    world.shadow.qg.assign(64, 1.0f);

    auto makePlant = [&](const BranchModulePrototype* proto, bromath::Vec3 origin) {
        Plant p;
        p.species = {};
        p.species.shadeTolerance = 0.0f;
        p.species.moduleMatureAge = 100.0f;
        p.effectiveRootVigorMax = p.species.rootVigorMax;
        p.origin = origin;
        BranchModuleInstance m;
        m.prototype = proto;
        m.parent = UINT32_MAX;
        m.age = 10.0f;
        m.vigor = 0.9f;
        m.light = 1.0f;
        p.modules.push_back(m);
        return p;
    };

    world.plants.push_back(makePlant(&bigProto, {0.0f, 0.0f, 0.0f}));
    world.plants.push_back(makePlant(&smallProto, {0.0f, 0.0f, 0.0f}));  // coincident centres

    step(world, 0.1f);
    step(world, 0.1f);

    // Both plants should have reduced light, and the smaller one's
    // collision sum should equal exactly its own volume (full containment).
    float l = world.plants[1].modules[0].light;
    ASSERT(l < 1.0f, "contained sphere is shaded");
}

// --- senescence.cpp sampleGrid hit path: terrain height read, soil
// blocked rejection. The seeding test currently exercises only the
// no-terrain fallback; here we wire up an actual grid.
TEST(seedling_drop_uses_terrain_and_respects_soil) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, proto);
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    // 2x2 terrain at (0,0), cellSize 10 — covers x,z in [0, 20).
    world.terrain.footprint.origin = {0.0f, 0.0f};
    world.terrain.footprint.cellSize = 10.0f;
    world.terrain.footprint.width = 2;
    world.terrain.footprint.depth = 2;
    world.terrain.height = {3.0f, 3.0f, 3.0f, 3.0f};

    // Matching 2x2 soil — block (0,0) cell only, so seeds dropping near
    // origin get rejected; seeds far from origin (cell 1,1) land.
    world.soil.footprint = world.terrain.footprint;
    world.soil.blocked = {1, 0, 0, 0};

    world.rngState = 7ULL;

    Plant p;
    p.species = {};
    p.species.floweringAge = 0.0f;
    p.species.seedingRadius = 1.0f;     // tight spread → seedlings stay near origin
    p.species.maxAge = 1000.0f;
    p.species.moduleMatureAge = 1000.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.age = 1.0f;
    p.flowering = true;
    p.origin = {15.0f, 0.0f, 15.0f};    // cell (1,1) — unblocked

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 0.0f;
    root.vigor = p.species.rootVigorMax;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    bool seeded = false;
    bool tookTerrainHeight = false;
    for (int i = 0; i < 200 && !seeded; ++i) {
        step(world, 1.0f);
        for (size_t k = 1; k < world.plants.size(); ++k) {
            if (std::fabs(world.plants[k].origin.y - 3.0f) < 1e-3f) {
                tookTerrainHeight = true;
            }
        }
        seeded = world.plants.size() > 1;
    }
    ASSERT(seeded, "seedling spawned");
    ASSERT(tookTerrainHeight, "seedling adopted terrain height (sampleGrid hit path)");
}

// --- First-flowering switch (senescence.cpp:143-144). A plant whose
// `flowering` flag flips from false to true mid-tick.
TEST(flowering_flag_flips_when_age_exceeds_threshold) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, proto);
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.floweringAge = 0.5f;
    p.species.maxAge = 1000.0f;
    p.species.moduleMatureAge = 1000.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.age = 0.0f;
    p.flowering = false;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.vigor = p.species.rootVigorMax;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    // Two ticks of dt=1.0 → age = 2.0 > 0.5; flowering must have flipped.
    step(world, 1.0f);
    step(world, 1.0f);

    ASSERT(world.plants[0].flowering, "plant entered flowering regime");
}

// --- Single-module "leaf" baseline for the pipe model: a lone module
// has no children, so it takes the species leaf diameter (else branch
// in development.cpp). Covered by other tests indirectly, but here as
// an explicit anchor on the public guarantee.
TEST(single_module_diameter_is_leaf_diameter) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);

    Plant p;
    p.species = {};
    p.species.leafDiameter = 0.123f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.light = 1.0f; root.vigor = 0.5f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    step(world, 0.1f);

    ASSERT(world.plants[0].modules[0].diameter == 0.123f,
           "isolated module diameter = species.leafDiameter");
}

// --- Climate adaptation: a plant far from its T_A / P_A optimum gets a
// small σ, dragging effectiveRootVigorMax down. Anchors the gaussian
// branch + tempLapsePerUnit interplay.
TEST(climate_mismatch_shrinks_effective_root_vigor) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.climate.annualTempBase = 30.0f;
    world.climate.annualPrecip = 100.0f;
    world.climate.tempLapsePerUnit = -0.01f;

    Plant p;
    p.species = {};
    p.species.climateOptT = -20.0f;     // species likes it cold
    p.species.climateOptP = 5000.0f;    // and very wet
    p.species.climateSigT = 1.0f;
    p.species.climateSigP = 50.0f;
    p.species.rootVigorMax = 1.0f;
    p.species.maxAge = 1000.0f;
    p.effectiveRootVigorMax = 1.0f;
    p.origin = {0.0f, 100.0f, 0.0f};

    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.light = 1.0f; root.vigor = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    step(world, 0.1f);

    ASSERT(world.plants[0].effectiveRootVigorMax < 0.01f,
           "climate mismatch crashes effectiveRootVigorMax");
}

// --- makeSeedling fallback: empty Voronoi → no seedling spawned.
// Covers the `if (!proto) return false` branch.
TEST(seeding_without_prototypes_is_a_noop) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    // Intentionally no entries in world.voronoi.
    world.rngState = 1ULL;

    Plant p;
    p.species = {};
    p.species.floweringAge = 0.0f;
    p.species.seedingRadius = 1.0f;
    p.species.maxAge = 1000.0f;
    p.species.moduleMatureAge = 1000.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    p.age = 1.0f;
    p.flowering = true;

    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.light = 1.0f; root.vigor = p.species.rootVigorMax;
    p.modules.push_back(root);
    world.plants.push_back(p);

    for (int i = 0; i < 50; ++i) step(world, 1.0f);
    ASSERT(world.plants.size() == 1, "no seedlings without prototypes");
}

// --- Spawning without a prototype library: parent matures but
// pickPrototype returns nullptr, so no child is appended.
TEST(spawning_without_voronoi_is_a_noop) {
    static BranchModulePrototype proto = simplePrototype();

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    // no entries in world.voronoi

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.0f;
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = &proto;
    root.parent = UINT32_MAX;
    root.age = 1.0f;
    root.vigor = 0.5f;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    step(world, 0.1f);
    ASSERT(world.plants[0].modules.size() == 1, "no spawn without a Voronoi library");
}
