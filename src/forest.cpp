#include "broflora/forest.h"

#include "broflora/sway.h"

#include <bromath/vec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace broflora {

using bromath::Vec3;

std::vector<TubeSegment> tubeSegments(const std::vector<bromesh::BranchSegment>& segs, float minRadius) {
    TubeShape shape;
    shape.minRadius = minRadius;
    return tubeSegments(segs, shape);
}

void appendTubeVegetation(std::vector<float>& out, const TubeSegment& t, const std::vector<SegmentSway>& sway,
                          const std::vector<bromesh::BranchSegment>& segs) {
    const size_t i = static_cast<size_t>(t.segment);
    if (i >= sway.size() || i >= segs.size()) return;
    appendSegmentVegetation(out, sway[i], t.along0, t.along1, bromath::vlen(segs[i].to - segs[i].from));
}

std::vector<TubeSegment> tubeSegments(const std::vector<bromesh::BranchSegment>& segs, const TubeShape& shape) {
    const float minRadius = shape.minRadius;
    // Each segment's continuation: its thickest child.
    std::vector<int> mainChild(segs.size(), -1);
    for (size_t i = 0; i < segs.size(); ++i) {
        const int p = segs[i].parent;
        if (p < 0 || static_cast<size_t>(p) >= segs.size()) continue;
        if (mainChild[p] < 0 || segs[i].radius > segs[mainChild[p]].radius) mainChild[p] = static_cast<int>(i);
    }
    const auto dirOf = [&](size_t i) {
        const Vec3 d = segs[i].to - segs[i].from;
        const float l = bromath::vlen(d);
        return l > 1e-6f ? d * (1.0f / l) : Vec3{0.0f, 1.0f, 0.0f};
    };
    const auto bisect = [](const Vec3& a, const Vec3& b) {
        const Vec3 s = a + b;
        const float l = bromath::vlen(s);
        return l > 0.2f ? s * (1.0f / l) : b;
    };

    std::vector<TubeSegment> out;
    out.reserve(segs.size());
    for (size_t i = 0; i < segs.size(); ++i) {
        const auto& s = segs[i];
        if (bromath::vlen(s.to - s.from) < 1e-6f) continue;
        if (s.radius < minRadius) continue;
        TubeSegment t;
        t.from = s.from;
        t.to = s.to;
        t.rTo = s.radius;
        t.rFrom = s.radius;
        const int p = s.parent;
        const bool hasParent = p >= 0 && static_cast<size_t>(p) < segs.size();
        const bool continues = hasParent && mainChild[p] == static_cast<int>(i);
        if (hasParent && segs[p].radius > 0.0f)
            t.rFrom = continues ? segs[p].radius : std::min(segs[p].radius, 1.5f * s.radius);
        const Vec3 di = dirOf(i);
        t.tangentFrom = continues ? bisect(dirOf(static_cast<size_t>(p)), di) : di;
        t.tangentTo = mainChild[i] >= 0 ? bisect(di, dirOf(static_cast<size_t>(mainChild[i]))) : di;
        t.segment = static_cast<int32_t>(i);

        // The pieces of the segment, at these distances from its from-end
        // with these radii (the first and last ends are t's own).
        const float L = bromath::vlen(s.to - s.from), r = s.radius;
        float cut[4], rad[4];
        int cuts = 0;
        if (!hasParent && shape.rootFlare > 0.0f && r > 0.0f) {
            // The root flare: r (1 + F e^(-h / r)), sampled where it bends.
            for (float h : {0.35f, 1.0f, 2.2f}) {
                if (h * r > L - 0.25f * r) break;
                cut[cuts] = h * r;
                rad[cuts++] = r * (1.0f + shape.rootFlare * std::exp(-h));
            }
            t.rFrom = r * (1.0f + shape.rootFlare);
        } else if (hasParent && !continues && shape.branchCollar > 0.0f && L > 4.0f * r) {
            // The collar: a short swell at the fork, then the branch.
            if (segs[p].radius > 0.0f) t.rFrom = std::min(segs[p].radius, (1.0f + shape.branchCollar) * r);
            cut[0] = 2.0f * r;
            rad[cuts++] = r * (1.0f + 0.25f * shape.branchCollar);
        }
        if (cuts == 0) {
            out.push_back(t);
            continue;
        }
        float at = 0.0f, rAt = t.rFrom;
        for (int k = 0; k <= cuts; ++k) {
            TubeSegment piece = t;
            const float end = k < cuts ? cut[k] : L;
            piece.from = s.from + di * at;
            piece.to = k < cuts ? s.from + di * end : s.to;
            piece.rFrom = rAt;
            piece.rTo = k < cuts ? rad[k] : t.rTo;
            piece.tangentFrom = k == 0 ? t.tangentFrom : di;
            piece.tangentTo = k < cuts ? di : t.tangentTo;
            piece.along0 = at / L;
            piece.along1 = k < cuts ? end / L : 1.0f;
            out.push_back(piece);
            at = end;
            rAt = piece.rTo;
        }
    }
    return out;
}

namespace {

uint64_t splitmix(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// A record moved to be relative to `o`: its root (floats 0-2) and its crown
// or tip (4-6) are points.
void relativeRecord(float* r, const Vec3& o) {
    r[0] -= o.x; r[1] -= o.y; r[2] -= o.z;
    r[4] -= o.x; r[5] -= o.y; r[6] -= o.z;
}

}  // namespace

ForestEmit emitForest(const WorldState& world, const ForestOptions& opts) {
    ForestEmit out;
    out.leafOptions = opts.leaves;

    // The plants emitted, in order.
    std::vector<uint32_t> chosen;
    if (opts.plants.empty()) {
        for (size_t i = 0; i < world.plants.size(); ++i) chosen.push_back(static_cast<uint32_t>(i));
    } else {
        for (const uint32_t i : opts.plants)
            if (i < world.plants.size()) chosen.push_back(i);
    }
    if (opts.leafForm >= 0)
        std::erase_if(chosen, [&](uint32_t i) {
            return world.plants[i].species.leafForm != static_cast<uint32_t>(opts.leafForm);
        });

    // Their clumps, numbered as emitWorldClumps and the scatter node number
    // them over these plants; their leaves keyed as placeLeavesInClumps keys
    // them.
    const std::vector<bromesh::LeafClump> clumps = emitPlantsClumps(world, chosen, opts.clumps);
    std::vector<uint32_t> leafKey(clumps.size() + 1, 0);
    std::vector<int> leafCount(clumps.size(), 0);
    for (size_t c = 0; c < clumps.size(); ++c) {
        leafCount[c] = std::max(0, bromesh::leafClumpCount(clumps[c], static_cast<uint32_t>(c), opts.leaves));
        leafKey[c + 1] = leafKey[c] + static_cast<uint32_t>(leafCount[c]);
    }

    size_t segBase = 0;   // the plant's first segment in the chosen plants' list
    size_t clumpAt = 0;   // clumps are in plant order
    for (const uint32_t pi : chosen) {
        const Plant& plant = world.plants[pi];
        const auto segs = emitPlantSegments(plant);
        const auto sway = emitPlantSway(plant);
        const size_t segEnd = segBase + segs.size();
        if (segs.empty() || sway.size() != segs.size()) {
            while (clumpAt < clumps.size() && static_cast<size_t>(clumps[clumpAt].segment) < segEnd) ++clumpAt;
            segBase = segEnd;
            continue;
        }

        ForestSpecimen sp;
        sp.plant = static_cast<uint32_t>(pi);
        sp.origin = plant.origin;
        sp.height = sway[0].height;
        sp.crown = sway[0].crown - plant.origin;
        sp.crownRadius = sway[0].crownRadius;
        sp.leafForm = plant.species.leafForm;
        sp.leafColor = plant.species.leafColor;
        sp.barkColor = plant.species.barkColor;
        sp.leafSize = plant.species.leafSize > 0.0f ? plant.species.leafSize : 1.0f;
        sp.clumpFlat = std::clamp(plant.species.clumpFlat > 0.0f ? plant.species.clumpFlat : 1.0f, 0.2f, 1.0f);
        const Vec3 o = plant.origin;

        // Bark, thickest first by each segment's keep radius: the thickest
        // tip anywhere at or beyond it. A module tapers to leaf thickness at
        // its terminal and the next one starts thick, so a segment's own
        // radius is no guide; the keep radius never grows from a segment to
        // its children, so the first n are always connected to the root.
        std::vector<float> keep(segs.size());
        for (size_t i = 0; i < segs.size(); ++i) keep[i] = segs[i].radius;
        for (bool changed = true; changed;) {
            changed = false;
            for (size_t i = segs.size(); i-- > 0;) {
                const int p = segs[i].parent;
                if (p >= 0 && static_cast<size_t>(p) < segs.size() && keep[p] < keep[i]) {
                    keep[p] = keep[i];
                    changed = true;
                }
            }
        }
        TubeShape shape;
        shape.minRadius = opts.minRadius;
        shape.rootFlare = plant.species.rootFlare;
        shape.branchCollar = plant.species.branchCollar;
        std::vector<TubeSegment> tubes = tubeSegments(segs, shape);
        std::stable_sort(tubes.begin(), tubes.end(), [&](const TubeSegment& a, const TubeSegment& b) {
            return keep[static_cast<size_t>(a.segment)] > keep[static_cast<size_t>(b.segment)];
        });
        sp.segStart = static_cast<uint32_t>(out.segments.size() / 8);
        sp.segCount = static_cast<uint32_t>(tubes.size());
        for (const TubeSegment& t : tubes) {
            out.keepRadius.push_back(keep[static_cast<size_t>(t.segment)]);
            const Vec3 f = t.from - o, e = t.to - o;
            out.segments.insert(out.segments.end(), {f.x, f.y, f.z, t.rFrom, e.x, e.y, e.z, t.rTo});
            out.tangents.insert(out.tangents.end(), {t.tangentFrom.x, t.tangentFrom.y, t.tangentFrom.z,
                                                     t.tangentTo.x, t.tangentTo.y, t.tangentTo.z});
            const size_t at = out.segmentVegetation.size();
            appendTubeVegetation(out.segmentVegetation, t, sway, segs);
            relativeRecord(&out.segmentVegetation[at], o);
        }

        // Clumps, and their leaves shuffled into an even thinning order.
        sp.clumpStart = static_cast<uint32_t>(clumpAt);
        sp.leafStart = static_cast<uint32_t>(out.leaves.size() / 2);
        std::vector<uint32_t> mine;   // (clump, key) pairs
        while (clumpAt < clumps.size() && static_cast<size_t>(clumps[clumpAt].segment) < segEnd) {
            const bromesh::LeafClump& c = clumps[clumpAt];
            const Vec3 p = c.center - o;
            out.clumps.insert(out.clumps.end(), {p.x, p.y, p.z, c.radius, c.dir.x, c.dir.y, c.dir.z,
                                                 static_cast<float>(leafCount[clumpAt])});
            const size_t local = static_cast<size_t>(c.segment) - segBase;
            const size_t at = out.clumpVegetation.size();
            appendClumpVegetation(out.clumpVegetation, sway[local]);
            relativeRecord(&out.clumpVegetation[at], o);
            for (int k = 0; k < leafCount[clumpAt]; ++k) {
                mine.push_back(static_cast<uint32_t>(clumpAt));
                mine.push_back(leafKey[clumpAt] + static_cast<uint32_t>(k));
            }
            ++clumpAt;
        }
        sp.clumpCount = static_cast<uint32_t>(clumpAt) - sp.clumpStart;
        const size_t n = mine.size() / 2;
        uint64_t rng = opts.seed ^ (static_cast<uint64_t>(pi + 1) * 0xD1B54A32D192ED03ULL);
        for (size_t i = n; i > 1; --i) {
            const size_t j = static_cast<size_t>(splitmix(rng) % i);
            std::swap(mine[2 * (i - 1)], mine[2 * j]);
            std::swap(mine[2 * (i - 1) + 1], mine[2 * j + 1]);
        }
        out.leaves.insert(out.leaves.end(), mine.begin(), mine.end());
        sp.leafCount = static_cast<uint32_t>(n);

        out.specimens.push_back(sp);
        segBase = segEnd;
    }
    return out;
}

} // namespace broflora
