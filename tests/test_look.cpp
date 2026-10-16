// What reaches the screen: kinked bark, crown clumps and the segment
// records that let an instanced segment bend with its child.

#include "test_framework.h"

#include "broflora/clumps.h"
#include "broflora/mesh_emit.h"
#include "broflora/plant.h"

#include <bromesh/procedural/leaf_clumps.h>

#include <cmath>
#include <vector>

using namespace broflora;

namespace {

// A stem, a fork on it, and a twig on each arm of the fork (the sway test's
// plant), grown up: mature, lit, twigs at leaf thickness.
Plant forkPlant() {
    static BranchModulePrototype stem;
    static BranchModulePrototype fork;
    if (stem.nodes.empty()) {
        stem.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
        stem.nodes.push_back({{0.0f, 0.5f, 0.05f}, 0.0f, 1.0f, 1.0f});
        stem.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
        stem.edges.push_back({0, 1});
        stem.edges.push_back({1, 2});
        stem.rootNode = 0;
        stem.terminalNodes = {2};
        fork.nodes.push_back({{ 0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
        fork.nodes.push_back({{ 0.4f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
        fork.nodes.push_back({{-0.4f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
        fork.edges.push_back({0, 1});
        fork.edges.push_back({0, 2});
        fork.rootNode = 0;
        fork.terminalNodes = {1, 2};
    }
    Plant plant;
    plant.species.leafDiameter = 0.02f;
    plant.origin = {2.0f, 0.0f, -1.0f};
    auto module = [&](const BranchModulePrototype* p, uint32_t parent, uint32_t attach,
                      bromath::Vec3 pos, float diameter) {
        BranchModuleInstance m;
        m.prototype = p;
        m.parent = parent;
        m.parentAttachTerminal = attach;
        m.worldPos = pos;
        m.diameter = diameter;
        m.age = 3.0f;
        m.vigor = plant.species.maxVigor;
        m.light = 1.0f;
        m.lightExposure = 1.0f;
        return m;
    };
    plant.modules = {
        module(&stem, UINT32_MAX, 0, {2.0f, 0.0f, -1.0f}, 0.40f),
        module(&fork, 0, 2, {2.0f, 1.0f, -1.0f}, 0.20f),
        module(&stem, 1, 1, {2.4f, 2.0f, -1.0f}, 0.02f),
        module(&stem, 1, 2, {1.6f, 2.0f, -1.0f}, 0.02f),
    };
    return plant;
}

}  // namespace

TEST(look_kinks_bend_branches_and_keep_joints) {
    Plant straight = forkPlant();
    straight.species.kink = 0.0f;
    Plant kinked = forkPlant();
    ASSERT(kinked.species.kink > 0.0f, "kinks are on by default");

    const auto a = emitPlantSegments(straight);
    const auto b = emitPlantSegments(kinked);
    ASSERT(a.size() == b.size() && !a.empty(), "kinks change no topology");

    float moved = 0.0f;
    bool joined = true;
    for (size_t i = 0; i < b.size(); ++i) {
        moved = std::max(moved, bromath::vlen(b[i].to - a[i].to));
        const int p = b[i].parent;
        if (p < 0) continue;
        // Kinked joints are exactly as closed as the grown skeleton's.
        const float gapA = bromath::vlen(a[i].from - a[static_cast<size_t>(p)].to);
        const float gapB = bromath::vlen(b[i].from - b[static_cast<size_t>(p)].to);
        if (std::fabs(gapA - gapB) > 1e-5f) {
            std::printf("segment %zu: gap %g grown, %g kinked\n", i, gapA, gapB);
            joined = false;
        }
    }
    ASSERT(joined, "every segment still starts where its parent ends");
    ASSERT(moved > 0.005f, "the kinks move the nodes");
    ASSERT(bromath::vlen(b[0].from - kinked.origin) < 1e-6f, "the root stays at the plant's origin");

    // Deterministic, and each step a fraction of its edge.
    const auto c = emitPlantSegments(kinked);
    bool same = true;
    for (size_t i = 0; i < b.size(); ++i) same = same && bromath::vlen(c[i].to - b[i].to) == 0.0f;
    ASSERT(same, "the same plant kinks the same way");
}

TEST(look_clumps_sit_on_leafy_twigs_apart) {
    Plant plant = forkPlant();
    const auto segs = emitPlantSegments(plant);
    const auto sway = emitPlantSway(plant);
    ClumpOptions opts;
    opts.maxRadius = 0.02f;
    const auto clumps = emitPlantClumps(plant, opts);
    ASSERT(!clumps.empty(), "a lit, mature plant carries clumps");

    for (size_t i = 0; i < clumps.size(); ++i) {
        const auto& c = clumps[i];
        ASSERT(c.segment >= 0 && static_cast<size_t>(c.segment) < segs.size(), "a clump names its twig");
        ASSERT(segs[static_cast<size_t>(c.segment)].radius <= opts.maxRadius + 1e-6f, "only twigs carry clumps");
        ASSERT(c.radius > 0.0f && c.density > 0.0f && c.density <= 1.0f, "a clump has a size and a density");
        // Hung just past its twig's tip, along it.
        const auto& s = segs[static_cast<size_t>(c.segment)];
        ASSERT(bromath::vlen(c.center - s.to) <= c.radius, "a clump hangs at its twig's end");
        for (size_t j = 0; j < i; ++j) {
            const float need = opts.spacing * 0.5f * (c.radius + clumps[j].radius);
            ASSERT(bromath::vlen(c.center - clumps[j].center) >= need - 1e-4f, "clumps keep their spacing");
        }
    }

    // A clump's radius follows the crown unless it is given.
    ClumpOptions fixed = opts;
    fixed.radius = 0.123f;
    fixed.radiusJitter = 0.0f;
    for (const auto& c : emitPlantClumps(plant, fixed)) ASSERT(std::fabs(c.radius - 0.123f) < 1e-6f, "fixed radius");

    // A zero density weight leaves the plant bare.
    ClumpOptions bare = opts;
    bare.densityWeight.assign(segs.size(), 0.0f);
    ASSERT(emitPlantClumps(plant, bare).empty(), "densityWeight replaces the default density");

    // The leaves in them sway with their twig's tip.
    bromesh::LeafClumpOptions lo;
    lo.perArea = 200.0f;
    const auto pl = bromesh::placeLeavesInClumps(clumps, lo);
    ASSERT(pl.count() > 0u, "leaves in the clumps");
    const auto veg = clumpLeafVegetation(pl, clumps, sway);
    ASSERT(veg.size() == pl.count() * kVegetationFloats, "a record per leaf");
    bool ok = true;
    for (size_t i = 0; i < pl.count(); ++i) {
        const float* r = &veg[i * kVegetationFloats];
        const auto& s = sway[static_cast<size_t>(clumps[static_cast<size_t>(pl.segment[i])].segment)];
        const float axis = std::sqrt(r[12] * r[12] + r[13] * r[13] + r[14] * r[14]);
        ok = ok && r[8] == pl.transforms[i * 16 + 3] && r[11] == s.weightTo && r[15] == s.phase &&
             std::fabs(axis - 1.0f) < 1e-3f && r[7] > 0.0f;
    }
    ASSERT(ok, "anchor at the leaf, its twig tip's weight, its limb's phase, a unit stem axis");
}

TEST(look_segment_records_carry_the_tip) {
    Plant plant = forkPlant();
    const auto segs = emitPlantSegments(plant);
    const auto sway = emitPlantSway(plant);
    float rec[kVegetationFloats];
    for (size_t i = 0; i < segs.size(); ++i) {
        writeSegmentVegetation(rec, sway[i], segs[i].from, segs[i].to);
        ASSERT(rec[4] == segs[i].to.x && rec[5] == segs[i].to.y && rec[6] == segs[i].to.z, "tip in the crown floats");
        ASSERT(std::fabs(rec[7] + 1.0f + sway[i].weightTo) < 1e-6f, "-(1 + weightTo) marks the form");
        ASSERT(rec[8] == segs[i].from.x && rec[11] == sway[i].weightFrom, "base and its weight");
        ASSERT(rec[12] == 0.0f && rec[13] == 0.0f && rec[14] == 0.0f, "wood: no flutter axis");
        // The child's base weight is this tip's: the joint bends as one.
        for (size_t k = 0; k < segs.size(); ++k) {
            if (segs[k].parent == static_cast<int>(i) && sway[i].weightTo > 0.0f) {
                ASSERT(std::fabs(sway[k].weightFrom - sway[i].weightTo) < 1e-5f, "continuous at the joint");
            }
        }
    }
}
