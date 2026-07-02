#include "test_framework.h"

using namespace broflora;

// Every built-in prototype must satisfy the structural invariants the
// simulation relies on: a valid root, basipetal edge ordering (parent
// index < child index), and terminal nodes that are real leaves.
static void assertWellFormed(const BranchModulePrototype& p, const char* what) {
    ASSERT(!p.nodes.empty(), "prototype has nodes");
    ASSERT(p.rootNode < p.nodes.size(), "rootNode in range");
    ASSERT(!p.terminalNodes.empty(), "has at least one terminal");
    for (const auto& e : p.edges) {
        ASSERT(e.a < p.nodes.size() && e.b < p.nodes.size(), "edge endpoints in range");
        ASSERT(e.a < e.b, "edges basipetally ordered (a < b)");
    }
    for (uint32_t t : p.terminalNodes) {
        ASSERT(t < p.nodes.size(), "terminal node in range");
        bool isLeaf = true;
        for (const auto& e : p.edges) if (e.a == t) { isLeaf = false; break; }
        ASSERT(isLeaf, "terminal node has no outgoing edge");
    }
    (void)what;
}

TEST(prototypes_straight_is_single_segment) {
    auto p = straightModule();
    assertWellFormed(p, "straight");
    ASSERT(p.nodes.size() == 2u, "straight has 2 nodes");
    ASSERT(p.terminalNodes.size() == 1u, "straight has 1 terminal");
}

TEST(prototypes_fork_has_two_terminals) {
    auto p = forkModule();
    assertWellFormed(p, "fork");
    ASSERT(p.terminalNodes.size() == 2u, "fork has 2 terminals");
}

TEST(prototypes_whorl_arm_count_and_clamp) {
    auto p3 = whorlModule(3);
    assertWellFormed(p3, "whorl3");
    ASSERT(p3.terminalNodes.size() == 3u, "whorl(3) has 3 arm terminals");
    // root + fork + 3 arms, each arm a two-node curved chain (mid + tip)
    ASSERT(p3.nodes.size() == 8u, "whorl(3) has 8 nodes");

    auto p1 = whorlModule(1);   // clamps up to 2
    ASSERT(p1.terminalNodes.size() == 2u, "whorl arms clamp to >= 2");
    auto p99 = whorlModule(99); // clamps down to 8
    ASSERT(p99.terminalNodes.size() == 8u, "whorl arms clamp to <= 8");
}

// A whorl's arms must actually spread in 3D (not collapse to a line), so
// crowns fill volumetrically. Check the arm terminals span all three axes.
TEST(prototypes_whorl_arms_spread_in_3d) {
    auto p = whorlModule(4, 0.8f);
    float xlo = 1e9f, xhi = -1e9f, zlo = 1e9f, zhi = -1e9f, ymax = -1e9f;
    for (uint32_t t : p.terminalNodes) {
        const auto& pos = p.nodes[t].position;
        xlo = pos.x < xlo ? pos.x : xlo; xhi = pos.x > xhi ? pos.x : xhi;
        zlo = pos.z < zlo ? pos.z : zlo; zhi = pos.z > zhi ? pos.z : zhi;
        ymax = pos.y > ymax ? pos.y : ymax;
    }
    ASSERT(xhi - xlo > 0.5f, "arms spread in X");
    ASSERT(zhi - zlo > 0.5f, "arms spread in Z");
    ASSERT(ymax > 0.5f, "arms rise above the fork");
}

// End-to-end: a whorl-seeded plant must grow into a wider, fuller crown
// than a fork-seeded one under identical conditions — the whole point of
// shipping a richer prototype.
TEST(prototypes_whorl_grows_fuller_than_fork) {
    auto grow = [](BranchModulePrototype proto) {
        WorldState w;
        w.rngState = 0xC0FFEEULL;
        w.climate.annualTempBase = 15.0f;
        w.climate.annualPrecip   = 1000.0f;
        w.shadow.origin = {-8, 0, -8};
        w.shadow.cellSize = 1.0f;
        w.shadow.width = w.shadow.height = w.shadow.depth = 16;
        w.shadow.qg.assign(16 * 16 * 16, 1.0f);
        const uint32_t pi = addPrototype(w, std::move(proto));
        addVoronoiSite(w, pi, 0.2f, 0.6f);
        addVoronoiSite(w, pi, 0.5f, 0.4f);

        Plant pl;
        pl.species = {};
        pl.species.moduleMatureAge = 0.5f;
        pl.species.orthotropy = 0.3f;  // let arms spread rather than rise straight
        pl.origin = {0, 0, 0};
        pl.effectiveRootVigorMax = pl.species.rootVigorMax;
        BranchModuleInstance root;
        root.prototype = prototypeAt(w, pi);
        root.parent = UINT32_MAX;
        root.vigor = pl.species.minVigor * 2.0f;
        root.light = 1.0f;
        pl.modules.push_back(root);
        addPlant(w, std::move(pl));

        for (int i = 0; i < 300; ++i) step(w, 0.02f);

        float foliage = 0.0f;
        for (const auto& f : emitWorldFoliage(w)) foliage += f.mass;
        return foliage;
    };

    // The whorl's extra terminals bear more leaf-bearing twigs, so it
    // carries more foliage than a fork under identical conditions — the
    // structural "fuller crown" guarantee. (Raw module count is not a
    // reliable proxy: whorl modules are larger, so fewer pack into the
    // same vigor budget.)
    const float forkFoliage  = grow(forkModule());
    const float whorlFoliage = grow(whorlModule(4, 0.7f));
    ASSERT(whorlFoliage > forkFoliage, "whorl crown carries more foliage than fork");
}
