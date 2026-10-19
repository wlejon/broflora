#include "broflora/forest.h"

#include "broflora/forest_records.h"

#include "broflora/sway.h"

#include <bromath/vec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace broflora {

namespace rec = forest_records;

namespace {
void put3(float* d, const bromath::Vec3& v) { d[0] = v.x; d[1] = v.y; d[2] = v.z; }
}  // namespace

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
        // A side branch leaves its parent half along it, so the curve through
        // its ends (bro's forestHermite) sweeps out of the parent's wood
        // rather than leaving it at an elbow, and its first ring lies tilted
        // into the parent, not across it.
        t.tangentFrom = hasParent ? bisect(dirOf(static_cast<size_t>(p)), di) : di;
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
    for (const size_t f : {rec::vegetation::kRoot, rec::vegetation::kCrown}) {
        r[f] -= o.x; r[f + 1] -= o.y; r[f + 2] -= o.z;
    }
}

// How far apart (in bloom sizes, centre to centre) a plant's blooms stand at
// the least: a head or a flower about its own size, so neighbours touch but
// do not pile into each other; a spike, slender, a third of its length, so
// a lavender's spikes stand in a sheaf.
float bloomSpacing(uint32_t form) {
    switch (static_cast<BloomForm>(form)) {
    case BloomForm::Head: return 0.9f;
    case BloomForm::Spike: return 0.35f;
    default: return 0.8f;
    }
}

// `plant`'s blooms into `out.blooms`, its range and look into `sp`: its
// bloom anchors, each sized by its shoot's state, visited in a shuffled
// order (so the survivors are shuffled too) and kept only when no bloom
// kept before stands nearer than the spacing. `segs` are the plant's
// segments, `tubes` its tube pieces in the order the specimen emitted them.
void appendBlooms(ForestEmit& out, ForestSpecimen& sp, const Plant& plant,
                  const std::vector<bromesh::BranchSegment>& segs, const std::vector<TubeSegment>& tubes,
                  uint64_t seed) {
    const Species& s = plant.species;
    sp.bloomStart = static_cast<uint32_t>(out.blooms.size() / rec::bloom::kFloats);
    sp.bloomForm = s.bloomForm < kBloomFormCount ? s.bloomForm : 0u;
    sp.bloomColor = s.bloomColor;
    sp.bloomSize = s.bloomSize > 0.0f ? s.bloomSize : 0.0f;
    if (sp.bloomForm == static_cast<uint32_t>(BloomForm::None) || sp.bloomSize <= 0.0f || tubes.empty()) return;
    const std::vector<BloomAnchor> anchors = emitPlantBloomAnchors(plant);
    if (anchors.empty() || segs.empty()) return;

    // Each segment's piece in the specimen's list: its last (the tip, where
    // the twig's anchor is), else the nearest drawn segment below it.
    std::vector<int32_t> tubeOf(segs.size(), -1);
    for (size_t t = 0; t < tubes.size(); ++t) {
        const int32_t g = tubes[t].segment;
        if (g < 0 || static_cast<size_t>(g) >= segs.size()) continue;
        if (tubeOf[g] < 0 || tubes[t].along1 >= tubes[static_cast<size_t>(tubeOf[g])].along1)
            tubeOf[g] = static_cast<int32_t>(t);
    }
    const auto pieceOf = [&](int32_t g) {
        for (size_t guard = 0; g >= 0 && static_cast<size_t>(g) < segs.size() && guard <= segs.size(); ++guard) {
            if (tubeOf[g] >= 0) return tubeOf[g];
            g = segs[g].parent;
        }
        return int32_t{0};
    };

    // The plant's shell: the centre of its segments' tips and the farthest
    // of them from it, and its height.
    Vec3 shellC{0.0f, 0.0f, 0.0f};
    float shellR = 0.0f, plantH = 0.0f;
    for (const auto& g : segs) {
        shellC = shellC + g.to;
        plantH = std::max(plantH, g.to.y - plant.origin.y);
    }
    shellC = shellC * (1.0f / static_cast<float>(segs.size()));
    for (const auto& g : segs) shellR = std::max(shellR, bromath::vlen(g.to - shellC));

    std::vector<uint32_t> order(anchors.size());
    std::iota(order.begin(), order.end(), 0u);
    uint64_t rng = seed;
    for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[splitmix(rng) % i]);

    // Kept blooms by grid cell, a cell the spacing between the largest.
    const float spacing = bloomSpacing(sp.bloomForm);
    const float cell = std::max(spacing * sp.bloomSize, 1e-3f);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    std::vector<Vec3> keptAt;
    std::vector<float> keptSize;
    const auto key = [](int64_t x, int64_t y, int64_t z) {
        return static_cast<uint64_t>(x * 73856093) ^ static_cast<uint64_t>(y * 19349663) ^
               static_cast<uint64_t>(z * 83492791);
    };
    for (const uint32_t i : order) {
        const BloomAnchor& a = anchors[i];
        if (a.segment < 0) continue;
        // A young shoot's bloom is still opening, a weak or shaded one's
        // smaller, and they shrink as the plant senesces; one deep in the
        // shade, or too small to read, is not borne at all, nor one on a
        // shoot hanging down (an arching stem's tip under the mound: blooms
        // are held up and out, where the light is).
        const float age = std::clamp(a.age01, 0.0f, 1.0f);
        const float size = sp.bloomSize * (0.75f + 0.25f * age) * (0.9f + 0.1f * std::clamp(a.vigor01, 0.0f, 1.0f)) *
                           (0.85f + 0.15f * std::clamp(a.lightExposure01, 0.0f, 1.0f)) *
                           (1.0f - 0.5f * std::clamp(a.senescence01, 0.0f, 1.0f));
        if (a.lightExposure01 < 0.04f || a.normal.y < -0.35f || size < 0.3f * sp.bloomSize) continue;
        Vec3 p = a.position;
        Vec3 facing = a.normal;
        // Held on the plant's outer shell, over its lower part: a stem tip
        // inside the foliage, low by the ground, or on the shell's underside
        // bears none (an arching stem's tip under the mound sat its heads on
        // the ground).
        if (shellR > 0.0f) {
            const Vec3 out = p - shellC;
            const float d = bromath::vlen(out);
            if (d < 0.6f * shellR || out.y < -0.1f * d || p.y - plant.origin.y < 0.4f * plantH) continue;
            // A head stands proud of the leaves round its stem's tip (they
            // reach past it), turned out from the mound as well as up its
            // stem.
            if (sp.bloomForm == static_cast<uint32_t>(BloomForm::Head) && d > 1e-4f) {
                const Vec3 o = out * (1.0f / d);
                Vec3 f = a.normal * 0.5f + o * 0.5f;
                const float fl = bromath::vlen(f);
                f = fl > 1e-4f ? f * (1.0f / fl) : o;
                p = p + f * (0.45f * size);
                facing = f;
            }
        }
        const int64_t cx = static_cast<int64_t>(std::floor(p.x / cell));
        const int64_t cy = static_cast<int64_t>(std::floor(p.y / cell));
        const int64_t cz = static_cast<int64_t>(std::floor(p.z / cell));
        bool crowded = false;
        for (int64_t dx = -1; dx <= 1 && !crowded; ++dx)
            for (int64_t dy = -1; dy <= 1 && !crowded; ++dy)
                for (int64_t dz = -1; dz <= 1 && !crowded; ++dz) {
                    const auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (const uint32_t k : it->second) {
                        const float d = spacing * 0.5f * (size + keptSize[k]);
                        const Vec3 e = keptAt[k] - p;
                        if (bromath::vdot(e, e) < d * d) { crowded = true; break; }
                    }
                }
        if (crowded) continue;
        grid[key(cx, cy, cz)].push_back(static_cast<uint32_t>(keptAt.size()));
        keptAt.push_back(p);
        keptSize.push_back(size);
        const Vec3 r = p - plant.origin;
        float* b = rec::append(out.blooms, rec::bloom::kFloats);
        put3(b + rec::bloom::kPosition, r);
        b[rec::bloom::kSize] = size;
        put3(b + rec::bloom::kDir, facing);
        b[rec::bloom::kSegment] = static_cast<float>(pieceOf(a.segment));
    }
    sp.bloomCount = static_cast<uint32_t>(out.blooms.size() / rec::bloom::kFloats) - sp.bloomStart;
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
        sp.leafWidth = std::clamp(plant.species.leafWidth, 0.0f, 1.0f);
        sp.leafTip = std::clamp(plant.species.leafTip, 0.0f, 1.0f);
        sp.leafTeeth = std::clamp(plant.species.leafTeeth, 0.0f, 40.0f);
        sp.leafToothDepth = std::clamp(plant.species.leafToothDepth, 0.0f, 0.6f);
        sp.leafGloss = std::clamp(plant.species.leafGloss, 0.0f, 1.0f);
        sp.sprayBrush = std::clamp(plant.species.sprayBrush, 0.0f, 1.0f);
        sp.phyllotaxis = std::min(plant.species.phyllotaxis, kPhyllotaxisCount - 1);
        sp.petiole = std::clamp(plant.species.petiole, 0.0f, 0.6f);
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
        // Each segment's clumps, its own and every one beyond it, while
        // they are few (indices into the specimen's clumps): a clump counted
        // on its way up to the root.
        constexpr size_t kSubMax = rec::segment_clumps::kMax;
        std::vector<std::array<uint32_t, kSubMax>> subList(segs.size());
        std::vector<uint32_t> subCount(segs.size(), 0);
        for (size_t ci = clumpAt; ci < clumps.size() && static_cast<size_t>(clumps[ci].segment) < segEnd; ++ci) {
            for (int64_t s = static_cast<int64_t>(clumps[ci].segment) - static_cast<int64_t>(segBase);
                 s >= 0 && static_cast<size_t>(s) < segs.size(); s = segs[static_cast<size_t>(s)].parent) {
                const size_t k = static_cast<size_t>(s);
                if (subCount[k] < kSubMax) subList[k][subCount[k]] = static_cast<uint32_t>(ci - clumpAt);
                ++subCount[k];
                if (segs[k].parent >= s) break;
            }
        }
        std::vector<TubeSegment> tubes = tubeSegments(segs, shape);
        std::stable_sort(tubes.begin(), tubes.end(), [&](const TubeSegment& a, const TubeSegment& b) {
            return keep[static_cast<size_t>(a.segment)] > keep[static_cast<size_t>(b.segment)];
        });
        sp.segStart = static_cast<uint32_t>(out.segments.size() / rec::segment::kFloats);
        sp.segCount = static_cast<uint32_t>(tubes.size());
        for (const TubeSegment& t : tubes) {
            out.keepRadius.push_back(keep[static_cast<size_t>(t.segment)]);
            {
                const size_t k = static_cast<size_t>(t.segment);
                float* sc = rec::append(out.segmentClumps, rec::segment_clumps::kFloats);
                for (size_t j = 0; j < kSubMax; ++j)
                    sc[rec::segment_clumps::kClumps + j] =
                        j < subCount[k] && subCount[k] <= kSubMax ? static_cast<float>(subList[k][j]) : -1.0f;
            }
            const Vec3 f = t.from - o, e = t.to - o;
            float* g = rec::append(out.segments, rec::segment::kFloats);
            put3(g + rec::segment::kFrom, f);
            g[rec::segment::kRFrom] = t.rFrom;
            put3(g + rec::segment::kTo, e);
            g[rec::segment::kRTo] = t.rTo;
            float* tg = rec::append(out.tangents, rec::tangent::kFloats);
            put3(tg + rec::tangent::kFrom, t.tangentFrom);
            put3(tg + rec::tangent::kTo, t.tangentTo);
            const size_t at = out.segmentVegetation.size();
            appendTubeVegetation(out.segmentVegetation, t, sway, segs);
            relativeRecord(&out.segmentVegetation[at], o);
        }

        // Clumps, and their leaves shuffled into an even thinning order.
        sp.clumpStart = static_cast<uint32_t>(clumpAt);
        sp.leafStart = static_cast<uint32_t>(out.leaves.size() / rec::leaf::kValues);
        std::vector<uint32_t> mine;   // (clump, key) pairs
        while (clumpAt < clumps.size() && static_cast<size_t>(clumps[clumpAt].segment) < segEnd) {
            const bromesh::LeafClump& c = clumps[clumpAt];
            const Vec3 p = c.center - o;
            float* cr = rec::append(out.clumps, rec::clump::kFloats);
            put3(cr + rec::clump::kCentre, p);
            cr[rec::clump::kRadius] = c.radius;
            put3(cr + rec::clump::kTwig, c.dir);
            cr[rec::clump::kLeafCount] = static_cast<float>(leafCount[clumpAt]);
            const size_t local = static_cast<size_t>(c.segment) - segBase;
            const size_t at = out.clumpVegetation.size();
            appendClumpVegetation(out.clumpVegetation, sway[local]);
            relativeRecord(&out.clumpVegetation[at], o);
            for (int k = 0; k < leafCount[clumpAt]; ++k) {
                uint32_t l[rec::leaf::kValues];
                l[rec::leaf::kClump] = static_cast<uint32_t>(clumpAt);
                l[rec::leaf::kKey] = leafKey[clumpAt] + static_cast<uint32_t>(k);
                mine.insert(mine.end(), l, l + rec::leaf::kValues);
            }
            ++clumpAt;
        }
        sp.clumpCount = static_cast<uint32_t>(clumpAt) - sp.clumpStart;
        const size_t n = mine.size() / rec::leaf::kValues;
        uint64_t rng = opts.seed ^ (static_cast<uint64_t>(pi + 1) * 0xD1B54A32D192ED03ULL);
        for (size_t i = n; i > 1; --i) {
            const size_t j = static_cast<size_t>(splitmix(rng) % i);
            std::swap_ranges(mine.begin() + rec::leaf::kValues * (i - 1), mine.begin() + rec::leaf::kValues * i,
                             mine.begin() + rec::leaf::kValues * j);
        }
        out.leaves.insert(out.leaves.end(), mine.begin(), mine.end());
        sp.leafCount = static_cast<uint32_t>(n);

        // Its blooms, on the pieces just emitted.
        appendBlooms(out, sp, plant, segs, tubes, opts.seed ^ (static_cast<uint64_t>(pi + 1) * 0x8CB92BA72F3D8DD7ULL));

        out.specimens.push_back(sp);
        segBase = segEnd;
    }
    return out;
}

forest_records::Packed forest_records::pack(const ForestEmit& f) {
    Packed p;
    for (const ForestSpecimen& s : f.specimens) {
        float* r = append(p.specimens, specimen::kFloats);
        r[specimen::kSegStart] = static_cast<float>(s.segStart);
        r[specimen::kSegCount] = static_cast<float>(s.segCount);
        r[specimen::kClumpStart] = static_cast<float>(s.clumpStart);
        r[specimen::kClumpCount] = static_cast<float>(s.clumpCount);
        r[specimen::kLeafStart] = static_cast<float>(s.leafStart);
        r[specimen::kLeafCount] = static_cast<float>(s.leafCount);
        r[specimen::kHeight] = s.height;
        r[specimen::kCrownRadius] = s.crownRadius;
        put3(r + specimen::kCrown, s.crown);
        r[specimen::kPlant] = static_cast<float>(s.plant);
        put3(r + specimen::kOrigin, s.origin);
        r[specimen::kClumpFlat] = s.clumpFlat;

        float* l = append(p.looks, look::kFloats);
        put3(l + look::kLeafColor, s.leafColor);
        l[look::kLeafForm] = static_cast<float>(s.leafForm);
        put3(l + look::kBarkColor, s.barkColor);
        l[look::kLeafSize] = s.leafSize;

        float* ll = append(p.leafLooks, leaf_look::kFloats);
        ll[leaf_look::kWidth] = s.leafWidth;
        ll[leaf_look::kTip] = s.leafTip;
        ll[leaf_look::kTeeth] = s.leafTeeth;
        ll[leaf_look::kToothDepth] = s.leafToothDepth;
        ll[leaf_look::kGloss] = s.leafGloss;
        ll[leaf_look::kSprayBrush] = s.sprayBrush;
        ll[leaf_look::kPetiole] = s.petiole;
        ll[leaf_look::kPhyllotaxis] = static_cast<float>(s.phyllotaxis);

        float* b = append(p.blooms, specimen_bloom::kFloats);
        b[specimen_bloom::kStart] = static_cast<float>(s.bloomStart);
        b[specimen_bloom::kCount] = static_cast<float>(s.bloomCount);
        b[specimen_bloom::kForm] = static_cast<float>(s.bloomForm);
        b[specimen_bloom::kSize] = s.bloomSize;
        put3(b + specimen_bloom::kColor, s.bloomColor);

        // Drawn where the plant grew.
        float* in = append(p.instances, instance::kFloats);
        put3(in + instance::kPosition, s.origin);
        in[instance::kScale] = 1.0f;
        in[instance::kSpecimen] = static_cast<float>(p.instances.size() / instance::kFloats - 1);
    }
    return p;
}

} // namespace broflora
