// The forest emit's segment clumps (forest_records.h's segment_clumps): a
// segment leading to only a few of its specimen's clumps lists them, each
// hanging beyond it (further along the wood from the root), so a renderer
// can leave out the wood under clumps it drops.

#include "test_framework.h"

#include "broflora/forest.h"
#include "broflora/forest_records.h"
#include "broflora/presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace broflora;
namespace rec = broflora::forest_records;

namespace {

WorldState subtreeWorld() {
    WorldState world;
    world.rngState = 0x5ab7ee;
    world.climate.annualTempBase = 15.0f;
    world.climate.annualPrecip = 1100.0f;
    const float W = 30.0f;
    world.shadow.origin = {-W / 2, -2.0f, -W / 2};
    world.shadow.cellSize = 1.0f;
    world.shadow.width = uint32_t(W);
    world.shadow.height = 40;
    world.shadow.depth = uint32_t(W);
    world.shadow.qg.assign(size_t(world.shadow.width) * world.shadow.height * world.shadow.depth, 1.0f);
    const char* names[] = {"oak", "hydrangea", "fir", "willow"};
    float x = -9.0f;
    for (const char* n : names) {
        if (const auto s = addSpeciesPreset(world, n)) plantSpecies(world, *s, {x, 0.0f, 0.0f});
        x += 6.0f;
    }
    for (int t = 0; t < 600; ++t) step(world, 0.02f);
    return world;
}

}  // namespace

TEST(forest_segment_clumps_a_segment_lists_the_few_clumps_beyond_it) {
    const WorldState w = subtreeWorld();
    const ForestEmit f = emitForest(w);
    const size_t nSeg = f.segments.size() / rec::segment::kFloats;
    namespace sc = rec::segment_clumps;
    namespace v = rec::vegetation;
    ASSERT(f.specimens.size() == 4, "four specimens");
    ASSERT(f.segmentClumps.size() == nSeg * sc::kFloats, "a record a segment");
    if (f.segmentClumps.size() != nSeg * sc::kFloats) return;
    // A segment's listed clumps (indices into its specimen's), in order.
    const auto listed = [&](uint32_t g) {
        std::vector<int> out;
        for (size_t j = 0; j < sc::kMax; ++j) {
            const float c = f.segmentClumps[g * sc::kFloats + sc::kClumps + j];
            if (c >= 0.0f) out.push_back(static_cast<int>(c));
        }
        return out;
    };
    bool packed = true, inSpecimen = true, distinct = true, beyond = true, nestedOk = true, baseAll = true;
    size_t lists = 0, nested = 0;
    for (const ForestSpecimen& sp : f.specimens) {
        for (uint32_t g = sp.segStart; g < sp.segStart + sp.segCount; ++g) {
            const std::vector<int> l = listed(g);
            // -1 only past the last.
            for (size_t j = l.size(); j < sc::kMax; ++j)
                packed = packed && f.segmentClumps[g * sc::kFloats + sc::kClumps + j] < 0.0f;
            if (l.empty()) continue;
            ++lists;
            const float path = f.segmentVegetation[g * v::kFloats + v::kPath];
            // (A piece of a segment split at a flare or a collar starts past
            // its segment's start, where a clump on that segment is counted
            // from: by under the piece's own length.)
            const float* sg = &f.segments[g * rec::segment::kFloats];
            const float dx = sg[rec::segment::kTo] - sg[rec::segment::kFrom];
            const float dy = sg[rec::segment::kTo + 1] - sg[rec::segment::kFrom + 1];
            const float dz = sg[rec::segment::kTo + 2] - sg[rec::segment::kFrom + 2];
            const float piece = std::sqrt(dx * dx + dy * dy + dz * dz);
            for (size_t j = 0; j < l.size(); ++j) {
                if (l[j] >= static_cast<int>(sp.clumpCount)) { inSpecimen = false; continue; }
                if (j > 0) distinct = distinct && l[j] > l[j - 1];
                beyond = beyond && f.clumpVegetation[(sp.clumpStart + l[j]) * v::kFloats + v::kPath] >= path - piece - 1e-4f;
            }
        }
        // The thickest piece (the first: the base) leads to more than a few.
        baseAll = baseAll && (sp.clumpCount <= sc::kMax || listed(sp.segStart).empty());
        // The wood going on from a listed piece (one starting where it
        // ends) leads to some of its clumps. (Two branches can lie on one
        // line for a stretch, ending at one point: the list is within one
        // of theirs.)
        std::map<std::array<float, 3>, std::vector<uint32_t>> ends;
        for (uint32_t g = sp.segStart; g < sp.segStart + sp.segCount; ++g) {
            const float* e = &f.segments[g * rec::segment::kFloats + rec::segment::kTo];
            if (!listed(g).empty()) ends[{e[0], e[1], e[2]}].push_back(g);
        }
        for (uint32_t h = sp.segStart; h < sp.segStart + sp.segCount; ++h) {
            const std::vector<int> q = listed(h);
            const float* hs = &f.segments[h * rec::segment::kFloats];
            // (Not a piece of no length, which starts where it ends.)
            if (q.empty() || (hs[rec::segment::kFrom] == hs[rec::segment::kTo] &&
                              hs[rec::segment::kFrom + 1] == hs[rec::segment::kTo + 1] &&
                              hs[rec::segment::kFrom + 2] == hs[rec::segment::kTo + 2]))
                continue;
            const auto it = ends.find({hs[rec::segment::kFrom], hs[rec::segment::kFrom + 1], hs[rec::segment::kFrom + 2]});
            if (it == ends.end()) continue;
            ++nested;
            bool within = false;
            for (const uint32_t g : it->second) {
                const std::vector<int> r = listed(g);
                bool all = true;
                for (const int c : q) all = all && std::find(r.begin(), r.end(), c) != r.end();
                within = within || all;
            }
            nestedOk = nestedOk && within;
        }
    }
    std::printf("  segment clumps: %zu segments, %zu list their clumps, %zu nested\n", nSeg, lists, nested);
    ASSERT(packed, "a list's -1s come after its clumps");
    ASSERT(inSpecimen, "each listed clump is its specimen's");
    ASSERT(distinct, "a list names each clump once, in order");
    ASSERT(beyond, "each listed clump hangs beyond its segment");
    ASSERT(nestedOk && nested > 0, "the wood going on from a piece leads to some of its clumps");
    ASSERT(baseAll, "a specimen's base leads to more clumps than a list holds");
    ASSERT(lists > nSeg / 10, "many segments lead to only a few clumps");
    ASSERT(emitForest(w).segmentClumps == f.segmentClumps, "deterministic");
}

TEST(forest_clump_groups_hold_a_listed_segments_clumps_together) {
    const WorldState w = subtreeWorld();
    const ForestEmit f = emitForest(w);
    const size_t nSeg = f.segments.size() / rec::segment::kFloats;
    const size_t nClump = f.clumps.size() / rec::clump::kFloats;
    namespace sc = rec::segment_clumps;
    ASSERT(f.clumpGroups.size() == nClump, "a group a clump");
    if (f.clumpGroups.size() != nClump) return;
    bool named = true, small = true, together = true;
    size_t grouped = 0;
    for (const ForestSpecimen& sp : f.specimens) {
        std::map<int, int> size;
        for (uint32_t c = 0; c < sp.clumpCount; ++c) {
            const int g = static_cast<int>(f.clumpGroups[sp.clumpStart + c]);
            // Named by its first clump, which is its own group.
            named = named && g >= 0 && g <= static_cast<int>(c) &&
                    static_cast<int>(f.clumpGroups[sp.clumpStart + static_cast<uint32_t>(g)]) == g;
            ++size[g];
        }
        for (const auto& [g, n] : size) {
            small = small && n <= static_cast<int>(sc::kMax);
            if (n > 1) grouped += static_cast<size_t>(n);
        }
        // A segment's listed clumps are one group, so its wood goes when it does.
        for (uint32_t g = sp.segStart; g < sp.segStart + sp.segCount; ++g) {
            int first = -1;
            for (size_t j = 0; j < sc::kMax; ++j) {
                const float c = f.segmentClumps[g * sc::kFloats + sc::kClumps + j];
                if (c < 0.0f) break;
                const int k = static_cast<int>(f.clumpGroups[sp.clumpStart + static_cast<uint32_t>(c)]);
                if (first < 0) first = k;
                together = together && k == first;
            }
        }
    }
    std::printf("  clump groups: %zu clumps, %zu in groups of more than one (%zu segments)\n", nClump, grouped, nSeg);
    ASSERT(named, "a group is named by its first clump");
    ASSERT(small, "a group holds at most a list's clumps");
    ASSERT(together, "a listed segment's clumps share a group");
    ASSERT(grouped > nClump / 4, "many clumps drop with others");
    ASSERT(emitForest(w).clumpGroups == f.clumpGroups, "deterministic");
}
