#include "broflora/clumps.h"

#include "bromath/vec.h"
#include "internal_foliage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace broflora {

using bromath::Vec3;

namespace {

float mix01(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<float>((x >> 40) * (1.0 / 16777216.0));
}

// One plant's clumps, appended to `out`; `segBase` is the index of the
// plant's first segment in whatever list `segment` should index.
void plantClumpsInto(const Plant& plant, const ClumpOptions& opts, size_t segBase,
                     std::vector<bromesh::LeafClump>& out) {
    const auto segs = emitPlantSegments(plant);
    if (segs.empty()) return;
    const auto fol = emitPlantFoliage(plant);
    const auto sway = emitPlantSway(plant);
    if (fol.size() != segs.size() || sway.size() != segs.size()) return;

    const Vec3 crown = sway[0].crown;
    const float crownR = sway[0].crownRadius > 0.0f ? sway[0].crownRadius : 0.4f * sway[0].height;
    // The species scales its clumps (a conifer's sprays are a fraction of a
    // broadleaf's lobes for the same crown).
    const float scale = plant.species.clumpScale > 0.0f ? plant.species.clumpScale : 1.0f;
    const float R0 = scale * (opts.radius > 0.0f ? opts.radius : opts.radiusOfCrown * crownR);
    if (!(R0 > 0.0f)) return;
    const float leafSize = std::clamp(plant.species.leafSize > 0.0f ? plant.species.leafSize : 1.0f, 0.2f, 3.0f);
    const float leafMore = 1.0f / (leafSize * leafSize);
    // Flat clumps (Species::clumpFlat) stack closer up and down: the spacing
    // is measured with heights stretched by 1 / flat.
    const float flat = std::clamp(plant.species.clumpFlat > 0.0f ? plant.species.clumpFlat : 1.0f, 0.2f, 1.0f);
    const float reach = std::max(plant.species.clumpReach, 0.0f);
    const float fill = std::clamp(plant.species.clumpFill, 0.0f, 1.0f);
    const float inner = std::clamp(plant.species.clumpInner, 0.0f, 1.0f);
    // A self-pruned trunk (Species::crownLift) is bare: the short shoots its
    // leader's modules keep along it below the lift carry no foliage (its
    // boughs above may droop below the lift; their ends are far from the
    // axis and keep theirs).
    float liftY = -std::numeric_limits<float>::infinity();
    if (plant.species.crownLift > 0.0f) {
        float top = plant.origin.y;
        for (const auto& s : segs) top = std::max(top, std::max(s.from.y, s.to.y));
        liftY = plant.origin.y + std::min(plant.species.crownLift, 0.9f) * (top - plant.origin.y);
    }
    const float liftAxis = 0.25f * crownR;

    std::vector<int> kids(segs.size(), 0);
    for (const auto& s : segs) {
        if (s.parent >= 0 && static_cast<size_t>(s.parent) < segs.size()) ++kids[s.parent];
    }
    // Children that have grown any length: a shoot whose buds have only just
    // broken (its children still of no length) is still a tip.
    std::vector<int> grownKids(segs.size(), 0);
    for (const auto& s : segs) {
        if (s.parent >= 0 && static_cast<size_t>(s.parent) < segs.size() && bromath::vdist2(s.from, s.to) >= 1e-8f)
            ++grownKids[s.parent];
    }

    const uint64_t seed = opts.seed ^ (static_cast<uint64_t>(std::llround(plant.origin.x * 977.0f)) * 0x9E3779B97F4A7C15ULL)
                        ^ static_cast<uint64_t>(std::llround(plant.origin.z * 6007.0f));

    struct Cand { size_t seg; float priority; float density; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < segs.size(); ++i) {
        const auto& s = segs[i];
        if (s.depth < opts.minDepth) continue;
        if (s.radius > 0.0f && s.radius > opts.maxRadius) continue;
        if (bromath::vdist2(s.from, s.to) < 1e-12f) continue;
        if (s.to.y < liftY && std::hypot(s.to.x - plant.origin.x, s.to.z - plant.origin.z) < liftAxis) continue;
        const size_t gi = segBase + i;
        FoliageSample f = fol[i];
        // A filled crown (Species::clumpFill): new shoots carry their
        // foliage as they grow (a conifer's young leader and top boughs are
        // clothed, not bare), and every shoot tip carries it, wherever it
        // grows: a whorl's short shoots on a leader still lengthening are the
        // spire's foliage, though their module has gone on to branch.
        if (fill > 0.0f) {
            f.age01 = std::max(f.age01, 0.7f * fill);
            if (grownKids[i] == 0) {
                f.isTerminal = true;
                f.twigGrade01 = std::max(f.twigGrade01, internal::leafGrade(plant.species, 2.0f * s.radius));
            }
        }
        float w = opts.densityWeight.empty() ? foliageDensity(f)
                : gi < opts.densityWeight.size() ? opts.densityWeight[gi] : 0.0f;
        // Inner shoots clothed (Species::clumpInner): a shoot that has
        // branched on keeps as much as a tip in half light, so a shrub's
        // mound is foliage to its core and down to the ground, not a shell of
        // tips over bare stems.
        if (inner > 0.0f && opts.densityWeight.empty() && !f.isTerminal) {
            FoliageSample g = f;
            g.isTerminal = true;
            g.lightExposure01 = std::max(g.lightExposure01, 0.5f);
            g.twigGrade01 = std::max(g.twigGrade01, internal::leafGrade(plant.species, 2.0f * s.radius));
            w = std::max(w, inner * foliageDensity(g));
        }
        if (!(w >= opts.minDensity)) continue;
        const float outer = bromath::vlen(s.to - crown) / crownR;
        const float priority = outer + (kids[i] == 0 ? 0.3f : 0.0f) + 0.25f * mix01(seed + i);
        cands.push_back({i, priority, w});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.priority > b.priority || (a.priority == b.priority && a.seg < b.seg);
    });

    // Accepted clumps on a grid of cells the size of the largest reach.
    const float rMax = R0 * (1.0f + std::max(0.0f, opts.radiusJitter));
    // A crown filled to its core (clumpInner) packs its clumps closer: they
    // overlap into one mass instead of balls with stems between.
    const float spacing = opts.spacing * (1.0f - 0.4f * inner);
    const float cell = std::max(1e-4f, spacing * rMax);
    // Grid and spacing in the stretched space (heights over `flat`).
    const auto stretch = [&](const Vec3& p) { return Vec3{p.x, p.y / flat, p.z}; };
    auto key = [&](const Vec3& q) {
        const int64_t x = static_cast<int64_t>(std::floor(q.x / cell));
        const int64_t y = static_cast<int64_t>(std::floor(q.y / cell));
        const int64_t z = static_cast<int64_t>(std::floor(q.z / cell));
        return (x * 73856093) ^ (y * 19349663) ^ (z * 83492791);
    };
    std::unordered_map<int64_t, std::vector<size_t>> grid;
    const size_t first = out.size();

    for (const Cand& c : cands) {
        const auto& s = segs[c.seg];
        Vec3 dir = s.to - s.from;
        dir = dir * (1.0f / bromath::vlen(dir));
        float r = R0 * (1.0f + opts.radiusJitter * (2.0f * mix01(seed ^ (c.seg * 0x2545F4914F6CDD1DULL)) - 1.0f));
        if (reach > 0.0f) {
            // No wider than its bough reaches from the axis.
            const float ax = std::hypot(s.to.x - plant.origin.x, s.to.z - plant.origin.z);
            r = std::min(r, reach * ax + 0.25f * R0);
        }
        const Vec3 center = s.to + dir * (0.35f * r);
        const Vec3 sc = stretch(center);

        bool clear = true;
        const int64_t cx = static_cast<int64_t>(std::floor(sc.x / cell));
        const int64_t cy = static_cast<int64_t>(std::floor(sc.y / cell));
        const int64_t cz = static_cast<int64_t>(std::floor(sc.z / cell));
        for (int64_t dz = -1; dz <= 1 && clear; ++dz)
            for (int64_t dy = -1; dy <= 1 && clear; ++dy)
                for (int64_t dx = -1; dx <= 1 && clear; ++dx) {
                    const int64_t k = ((cx + dx) * 73856093) ^ ((cy + dy) * 19349663) ^ ((cz + dz) * 83492791);
                    auto it = grid.find(k);
                    if (it == grid.end()) continue;
                    for (size_t j : it->second) {
                        const auto& o = out[j];
                        const float need = spacing * 0.5f * (r + o.radius);
                        if (bromath::vdist2(sc, stretch(o.center)) < need * need) { clear = false; break; }
                    }
                }
        if (!clear) continue;

        bromesh::LeafClump lc;
        lc.center = center;
        lc.radius = r;
        lc.dir = dir;
        // Smaller leaves (Species::leafSize), more of them: as much cover.
        lc.density = std::clamp(0.3f + 0.9f * c.density, 0.0f, 1.0f) * leafMore;
        lc.segment = static_cast<int32_t>(segBase + c.seg);
        grid[key(sc)].push_back(out.size());
        out.push_back(lc);
    }
    // Clumps in segment order: neighbouring clumps (and so their leaves)
    // stay near each other in memory.
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
              [](const bromesh::LeafClump& a, const bromesh::LeafClump& b) { return a.segment < b.segment; });
}

}  // namespace

std::vector<bromesh::LeafClump> emitPlantClumps(const Plant& plant, const ClumpOptions& opts) {
    std::vector<bromesh::LeafClump> out;
    plantClumpsInto(plant, opts, 0, out);
    return out;
}

std::vector<bromesh::LeafClump> emitWorldClumps(const WorldState& world, const ClumpOptions& opts) {
    std::vector<bromesh::LeafClump> out;
    size_t base = 0;
    for (const auto& plant : world.plants) {
        plantClumpsInto(plant, opts, base, out);
        base += emitPlantSegments(plant).size();
    }
    return out;
}

std::vector<bromesh::LeafClump> emitPlantsClumps(const WorldState& world, const std::vector<uint32_t>& plants,
                                                 const ClumpOptions& opts) {
    std::vector<bromesh::LeafClump> out;
    size_t base = 0;
    for (const uint32_t pi : plants) {
        if (pi >= world.plants.size()) continue;
        plantClumpsInto(world.plants[pi], opts, base, out);
        base += emitPlantSegments(world.plants[pi]).size();
    }
    return out;
}

std::vector<float> clumpLeafVegetation(const bromesh::LeafPlacements& pl,
                                       const std::vector<bromesh::LeafClump>& clumps,
                                       const std::vector<SegmentSway>& sway) {
    const size_t count = pl.count();
    std::vector<float> out(count * kVegetationFloats, 0.0f);
    if (pl.segment.size() != count) return out;
    for (size_t i = 0; i < count; ++i) {
        const int32_t ci = pl.segment[i];
        if (ci < 0 || static_cast<size_t>(ci) >= clumps.size()) continue;
        const int32_t si = clumps[static_cast<size_t>(ci)].segment;
        if (si < 0 || static_cast<size_t>(si) >= sway.size()) continue;
        const float* M = &pl.transforms[i * 16];
        const Vec3 P{M[3], M[7], M[11]};
        Vec3 F{M[2], M[6], M[10]};
        const float fl = bromath::vlen(F);
        F = fl > 1e-8f ? F * (1.0f / fl) : Vec3{0.0f, 1.0f, 0.0f};
        const SegmentSway& s = sway[static_cast<size_t>(si)];
        writeVegetation(&out[i * kVegetationFloats], s, P, s.weightTo, F, true);
    }
    return out;
}

void appendClumpVegetation(std::vector<float>& out, const SegmentSway& s) {
    SegmentSway tip = s;
    tip.weightFrom = s.weightTo;
    appendSegmentVegetation(out, tip);
}

} // namespace broflora
