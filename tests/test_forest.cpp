// The forest emit (broflora/forest.h): each plant a specimen relative to
// where it stands, bark thickest first, leaves in a shuffled thinning order
// that is exactly the world's clump-mode scatter.

#include "test_framework.h"

#include "broflora/clumps.h"
#include "broflora/forest.h"
#include "broflora/mesh_emit.h"
#include "broflora/plant.h"
#include "broflora/sway.h"
#include "broflora/world.h"

#include <bromesh/procedural/leaf_clumps.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace broflora;

namespace {

// A stem, a fork on it, and a twig on each arm (test_look.cpp's plant),
// standing at `o`.
Plant forkPlantAt(bromath::Vec3 o) {
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
    plant.origin = o;
    auto module = [&](const BranchModulePrototype* p, uint32_t parent, uint32_t attach,
                      bromath::Vec3 pos, float diameter) {
        BranchModuleInstance m;
        m.prototype = p;
        m.parent = parent;
        m.parentAttachTerminal = attach;
        m.worldPos = pos + o;
        m.diameter = diameter;
        m.age = 3.0f;
        m.vigor = plant.species.maxVigor;
        m.light = 1.0f;
        m.lightExposure = 1.0f;
        return m;
    };
    plant.modules = {
        module(&stem, UINT32_MAX, 0, {0.0f, 0.0f, 0.0f}, 0.40f),
        module(&fork, 0, 2, {0.0f, 1.0f, 0.0f}, 0.20f),
        module(&stem, 1, 1, {0.4f, 2.0f, 0.0f}, 0.02f),
        module(&stem, 1, 2, {-0.4f, 2.0f, 0.0f}, 0.02f),
    };
    return plant;
}

WorldState twoPlants() {
    WorldState w;
    w.plants.push_back(forkPlantAt({2.0f, 0.0f, -1.0f}));
    w.plants.push_back(forkPlantAt({-3.0f, 0.5f, 4.0f}));
    return w;
}

ForestOptions forestOptions() {
    ForestOptions fo;
    fo.clumps.maxRadius = 0.02f;
    fo.leaves.perArea = 300.0f;
    return fo;
}

}  // namespace

TEST(forest_specimens_are_their_plants_relative_to_where_they_stand) {
    const WorldState w = twoPlants();
    const ForestEmit f = emitForest(w, forestOptions());
    ASSERT(f.specimens.size() == 2u, "a specimen per plant");
    ASSERT(f.segments.size() / 8 == f.tangents.size() / 6, "a tangent pair per segment");
    ASSERT(f.segmentVegetation.size() / 16 == f.segments.size() / 8, "a record per segment");
    ASSERT(f.clumpVegetation.size() / 16 == f.clumps.size() / 8, "a record per clump");

    // Both plants are the same shape at different places: their specimens
    // are the same numbers.
    const ForestSpecimen& a = f.specimens[0];
    const ForestSpecimen& b = f.specimens[1];
    ASSERT(a.segCount == b.segCount && a.segCount > 0, "same bark");
    ASSERT(b.segStart == a.segStart + a.segCount, "specimens follow each other");
    // The bark is the plant's segments less its origin (in thickness order;
    // a segment split at a flare or a collar ends in its last piece).
    for (size_t k = 0; k < 2; ++k) {
        const ForestSpecimen& s = f.specimens[k];
        const auto segs = emitPlantSegments(w.plants[k]);
        bool found = s.segCount >= segs.size();
        for (const auto& g : segs) {
            const bromath::Vec3 d = g.to - s.origin;
            bool hit = false;
            for (uint32_t i = 0; i < s.segCount; ++i) {
                const float* r = &f.segments[(s.segStart + i) * 8];
                hit = hit || (std::fabs(d.x - r[4]) + std::fabs(d.y - r[5]) + std::fabs(d.z - r[6]) < 1e-4f &&
                              g.radius == r[7]);
            }
            found = found && hit;
        }
        ASSERT(found, "bark relative to the root");
    }
    ASSERT(std::fabs(f.segmentVegetation[a.segStart * 16 + 0]) < 1e-5f &&
           std::fabs(f.segmentVegetation[b.segStart * 16 + 1]) < 1e-5f, "the records' root is the origin");
    ASSERT(bromath::vlen(b.origin - w.plants[1].origin) < 1e-6f && b.plant == 1u, "its plant and where it stands");
    ASSERT(a.height > 0.0f && a.crownRadius > 0.0f, "height and crown");
}

TEST(forest_bark_is_thickest_first_and_a_prefix_stays_connected) {
    const WorldState w = twoPlants();
    const ForestEmit f = emitForest(w, forestOptions());
    for (const ForestSpecimen& s : f.specimens) {
        bool sorted = true, covers = true;
        for (uint32_t i = 0; i < s.segCount; ++i) {
            if (i) sorted = sorted && f.keepRadius[s.segStart + i] <= f.keepRadius[s.segStart + i - 1];
            // A root flare's or collar's piece swells past its segment's
            // pipe radius by up to that much.
            const Species sp = w.plants[s.plant].species;
            const float swell = 1.0f + std::max(sp.rootFlare, sp.branchCollar);
            covers = covers && f.keepRadius[s.segStart + i] * swell >= f.segments[(s.segStart + i) * 8 + 7];
        }
        ASSERT(f.keepRadius.size() == f.segments.size() / 8, "a keep radius per segment");
        ASSERT(sorted, "the keep radius never grows down the list");
        ASSERT(covers, "a segment's keep radius is at least its own");
        // Every segment's base touches a segment before it (or the root).
        bool connected = true;
        for (uint32_t i = 0; i < s.segCount; ++i) {
            const float* r = &f.segments[(s.segStart + i) * 8];
            if (std::hypot(r[0], r[2]) < 1e-4f && std::fabs(r[1]) < 1e-4f) continue;
            float nearest = 1e30f, nearestLater = 1e30f;
            for (uint32_t j = 0; j < s.segCount; ++j) {
                if (j == i) continue;
                const float* q = &f.segments[(s.segStart + j) * 8];
                const float d = std::fabs(q[4] - r[0]) + std::fabs(q[5] - r[1]) + std::fabs(q[6] - r[2]);
                (j < i ? nearest : nearestLater) = std::min(j < i ? nearest : nearestLater, d);
            }
            if (nearest > nearestLater + 1e-5f)
                std::printf("segment %u: nearest tip before it %g, after %g\n", i, nearest, nearestLater);
            connected = connected && nearest <= nearestLater + 1e-5f;
        }
        ASSERT(connected, "a segment's parent comes before it");
    }
}

TEST(forest_leaves_are_the_world_scatter_in_a_thinning_order) {
    const WorldState w = twoPlants();
    const ForestOptions fo = forestOptions();
    const ForestEmit f = emitForest(w, fo);
    const auto clumps = emitWorldClumps(w, fo.clumps);
    ASSERT(f.clumps.size() / 8 == clumps.size(), "every clump of the world");
    const auto pl = bromesh::placeLeavesInClumps(clumps, fo.leaves);
    ASSERT(f.leaves.size() / 2 == pl.count() && pl.count() > 100u, "every leaf of the world");

    // Each leaf is the world's leaf `key` of clump `clump`.
    std::set<uint32_t> keys;
    bool match = true;
    for (size_t i = 0; i < f.leaves.size() / 2; ++i) {
        const uint32_t clump = f.leaves[2 * i], key = f.leaves[2 * i + 1];
        keys.insert(key);
        match = match && key < pl.count() && static_cast<uint32_t>(pl.segment[key]) == clump;
    }
    ASSERT(match, "a leaf's clump is its key's clump in placeLeavesInClumps");
    ASSERT(keys.size() == pl.count(), "each leaf once");

    // A specimen's leaves are its own clumps', shuffled: the first fifth
    // touches most of its clumps.
    for (const ForestSpecimen& s : f.specimens) {
        bool own = true;
        for (uint32_t i = 0; i < s.leafCount; ++i) {
            const uint32_t c = f.leaves[2 * (s.leafStart + i)];
            own = own && c >= s.clumpStart && c < s.clumpStart + s.clumpCount;
        }
        ASSERT(own, "a specimen holds its own clumps' leaves");
        std::set<uint32_t> seen;
        for (uint32_t i = 0; i < s.leafCount / 5; ++i) seen.insert(f.leaves[2 * (s.leafStart + i)]);
        ASSERT(seen.size() * 4 >= s.clumpCount * 3, "a prefix thins every clump");
        bool inOrder = true;
        for (uint32_t i = 1; i < s.leafCount; ++i)
            inOrder = inOrder && f.leaves[2 * (s.leafStart + i) + 1] > f.leaves[2 * (s.leafStart + i - 1) + 1];
        ASSERT(!inOrder, "the leaves are shuffled");
    }
    // Clump records carry their leaf counts.
    uint32_t total = 0;
    for (size_t c = 0; c < f.clumps.size() / 8; ++c) total += static_cast<uint32_t>(f.clumps[c * 8 + 7]);
    ASSERT(total == f.leaves.size() / 2, "clump leaf counts add up");
}

TEST(forest_tube_segments_match_the_branch_tubes) {
    const Plant p = forkPlantAt({0.0f, 0.0f, 0.0f});
    const auto segs = emitPlantSegments(p);
    const auto tubes = tubeSegments(segs);
    ASSERT(tubes.size() == segs.size(), "a tube per segment");
    bool ok = true;
    for (const TubeSegment& t : tubes) {
        const auto& s = segs[static_cast<size_t>(t.segment)];
        ok = ok && t.rTo == s.radius && t.rFrom >= t.rTo - 1e-6f &&
             std::fabs(bromath::vlen(t.tangentFrom) - 1.0f) < 1e-4f && std::fabs(bromath::vlen(t.tangentTo) - 1.0f) < 1e-4f;
    }
    ASSERT(ok, "radii from the pipe model, unit joint directions");
    ASSERT(tubeSegments(segs, 1.0f).empty(), "minRadius drops thin bark");
}

TEST(forest_tube_root_flare_and_branch_collar) {
    const Plant p = forkPlantAt({0.0f, 0.0f, 0.0f});
    const auto segs = emitPlantSegments(p);
    TubeShape shape;
    shape.rootFlare = 0.6f;
    shape.branchCollar = 0.5f;
    const auto tubes = tubeSegments(segs, shape);
    ASSERT(tubes.size() > segs.size(), "the flare and a collar split segments");

    // Each segment's pieces are adjacent, in order, end to end and cover it.
    bool joined = true, covered = true;
    for (size_t i = 0; i < tubes.size();) {
        size_t j = i;
        while (j + 1 < tubes.size() && tubes[j + 1].segment == tubes[i].segment) ++j;
        covered = covered && tubes[i].along0 == 0.0f && tubes[j].along1 == 1.0f;
        for (size_t k = i; k < j; ++k)
            joined = joined && bromath::vlen(tubes[k].to - tubes[k + 1].from) < 1e-5f &&
                     std::fabs(tubes[k].rTo - tubes[k + 1].rFrom) < 1e-6f && tubes[k].along1 == tubes[k + 1].along0;
        i = j + 1;
    }
    ASSERT(joined && covered, "a segment's pieces run on unbroken over the whole of it");

    // The root swells toward the ground and narrows up it.
    const TubeSegment& root = tubes.front();
    const float r = segs[static_cast<size_t>(root.segment)].radius;
    ASSERT(segs[static_cast<size_t>(root.segment)].parent < 0, "the root first");
    ASSERT(std::fabs(root.rFrom - 1.6f * r) < 1e-5f && root.rTo < root.rFrom && root.rTo > r, "the flare at the ground");

    // A side shoot's collar: no wider than its parent, a short swell.
    bool collar = false;
    for (size_t k = 0; k + 1 < tubes.size(); ++k) {
        const TubeSegment& a = tubes[k];
        if (a.segment != tubes[k + 1].segment || segs[static_cast<size_t>(a.segment)].parent < 0) continue;
        const auto& s = segs[static_cast<size_t>(a.segment)];
        collar = a.rFrom <= segs[static_cast<size_t>(s.parent)].radius + 1e-6f && a.rFrom > a.rTo &&
                 a.rTo > s.radius && bromath::vlen(a.to - a.from) < 2.01f * s.radius;
    }
    ASSERT(collar, "a side shoot swells over two radii from its fork");
}

TEST(forest_bark_records_carry_the_path_and_the_bark_word) {
    WorldState w = twoPlants();
    w.plants[1].species.barkFurrow = 0.9f;
    w.plants[1].species.barkScale = 2.0f;
    w.plants[1].species.barkMoss = 0.1f;
    const ForestEmit f = emitForest(w, forestOptions());
    const ForestSpecimen& b = f.specimens[1];
    float furrow, scale, moss;
    unpackBarkWord(f.segmentVegetation[b.segStart * 16 + 11], furrow, scale, moss);
    ASSERT(std::fabs(furrow - 0.9f) < 0.005f && std::fabs(scale - 2.0f) < 0.05f && std::fabs(moss - 0.1f) < 0.005f,
           "the species' bark in every record");
    unpackBarkWord(0.0f, furrow, scale, moss);
    const Species dflt;
    ASSERT(furrow == dflt.barkFurrow && scale == dflt.barkScale && moss == dflt.barkMoss, "an unset word: the defaults");

    // The path at a piece's from-end is the path to it: the piece that ends
    // there (a module's base can stand off its parent's tip, where none
    // does) has the path less its own length.
    bool path = true;
    uint32_t checked = 0;
    float most = 0.0f;
    for (uint32_t i = 0; i < b.segCount; ++i) {
        const float* s = &f.segments[(b.segStart + i) * 8];
        const float at = f.segmentVegetation[(b.segStart + i) * 16 + 10];
        most = std::max(most, at);
        if (std::hypot(s[0], s[2]) < 1e-4f && std::fabs(s[1]) < 1e-4f) {
            path = path && at == 0.0f;
            continue;
        }
        for (uint32_t j = 0; j < b.segCount; ++j) {
            const float* q = &f.segments[(b.segStart + j) * 8];
            if (std::fabs(q[4] - s[0]) + std::fabs(q[5] - s[1]) + std::fabs(q[6] - s[2]) > 1e-5f) continue;
            const float len = std::hypot(q[4] - q[0], q[5] - q[1], q[6] - q[2]);
            path = path && std::fabs(f.segmentVegetation[(b.segStart + j) * 16 + 10] + len - at) < 1e-4f;
            ++checked;
            break;
        }
    }
    ASSERT(path && checked * 2 >= b.segCount, "each piece's path is its parent's plus the parent's length");
    ASSERT(most > 2.0f, "the path reaches the twigs");
}
