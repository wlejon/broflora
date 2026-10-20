// Tube walls (broflora/tubes.h): a plant's segments as bark pieces, joined
// end to end, swelling at the root and at a side shoot's fork, each with the
// vegetation record it sways and is shaded by.

#include "test_framework.h"

#include "broflora/mesh_emit.h"
#include "broflora/plant.h"
#include "broflora/sway.h"
#include "broflora/tubes.h"

#include <algorithm>
#include <cmath>
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

}  // namespace

TEST(tube_segments_match_the_branch_segments) {
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

TEST(tube_root_flare_and_branch_collar) {
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

TEST(tube_records_carry_the_path_and_the_bark_word) {
    const bromath::Vec3 origin{-3.0f, 0.5f, 4.0f};
    Plant p = forkPlantAt(origin);
    p.species.barkFurrow = 0.9f;
    p.species.barkScale = 2.0f;
    p.species.barkMoss = 0.1f;
    const auto segs = emitPlantSegments(p);
    const auto sway = emitPlantSway(p);
    const auto tubes = tubeSegments(segs);
    std::vector<float> veg;
    for (const TubeSegment& t : tubes) appendTubeVegetation(veg, t, sway, segs);
    ASSERT(veg.size() == tubes.size() * kSegmentVegetationFloats, "a record a piece");
    namespace v = segment_vegetation;

    float furrow, scale, moss;
    unpackBarkWord(veg[v::kBark], furrow, scale, moss);
    ASSERT(std::fabs(furrow - 0.9f) < 0.005f && std::fabs(scale - 2.0f) < 0.05f && std::fabs(moss - 0.1f) < 0.005f,
           "the species' bark in every record");
    bool ribs = true;
    unpackBarkWord(veg[v::kBark], furrow, scale, moss, ribs);
    ASSERT(!ribs, "plated bark unless the species is ribbed");
    unpackBarkWord(packBarkWord(1.0f, 4.0f, 1.0f, true), furrow, scale, moss, ribs);
    ASSERT(ribs && furrow == 1.0f && std::fabs(scale - 4.0f) < 0.05f && moss == 1.0f, "ribbed, every field at its top");
    unpackBarkWord(0.0f, furrow, scale, moss);
    const Species dflt;
    ASSERT(furrow == dflt.barkFurrow && scale == dflt.barkScale && moss == dflt.barkMoss, "an unset word: the defaults");

    // The path at a piece's from-end is the path to it: the piece that ends
    // there (a module's base can stand off its parent's tip, where none
    // does) has the path less its own length.
    bool path = true;
    size_t checked = 0;
    float most = 0.0f;
    for (size_t i = 0; i < tubes.size(); ++i) {
        const float at = veg[i * kSegmentVegetationFloats + v::kPath];
        most = std::max(most, at);
        if (bromath::vlen(tubes[i].from - origin) < 1e-4f) {
            path = path && at == 0.0f;
            continue;
        }
        for (size_t j = 0; j < tubes.size(); ++j) {
            if (bromath::vlen(tubes[j].to - tubes[i].from) > 1e-5f) continue;
            const float len = bromath::vlen(tubes[j].to - tubes[j].from);
            path = path && std::fabs(veg[j * kSegmentVegetationFloats + v::kPath] + len - at) < 1e-4f;
            ++checked;
            break;
        }
    }
    ASSERT(path && checked * 2 >= tubes.size(), "each piece's path is its parent's plus the parent's length");
    ASSERT(most > 2.0f, "the path reaches the twigs");
}
