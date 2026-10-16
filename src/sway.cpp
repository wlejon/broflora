// Wind sway: each plant as a trunk, limbs and twigs, each a cantilever whose
// frequency and deflection come from its diameter, length and the leaves it
// carries; and the vegetation records that carry them to the renderer
// (include/broflora/sway.h).
#include "broflora/sway.h"

#include "broflora/mesh_emit.h"

#include "bromath/vec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace broflora {

using bromath::Vec3;

namespace {

// A well-mixed hash of an integer into [0, 1).
float hash01(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<float>((x >> 40) * (1.0 / 16777216.0));
}

// The sway of one plant's segments (`segs`, `fol` aligned, parent indices
// local to `segs`), appended to `out`.
void computePlantSway(const Plant& plant,
                      const std::vector<bromesh::BranchSegment>& segs,
                      const std::vector<FoliageSample>& fol,
                      std::vector<SegmentSway>& out) {
    const size_t n = segs.size();
    const size_t base = out.size();
    out.resize(base + n);
    if (n == 0) return;

    SegmentSway common;
    common.root = plant.origin;
    float top = plant.origin.y;
    for (const auto& s : segs) top = std::max({top, s.from.y, s.to.y});
    common.height = std::max(0.05f, top - plant.origin.y);

    std::vector<std::vector<int>> children(n);
    std::vector<float> len(n);
    for (size_t i = 0; i < n; ++i) {
        len[i] = bromath::vlen(segs[i].to - segs[i].from);
        const int p = segs[i].parent;
        // Emit order puts every parent before its children; anything else
        // is treated as a root.
        if (p >= 0 && static_cast<size_t>(p) < i) children[p].push_back(static_cast<int>(i));
    }
    auto parentOf = [&](size_t i) -> int {
        const int p = segs[i].parent;
        return (p >= 0 && static_cast<size_t>(p) < i) ? p : -1;
    };
    auto mass = [&](size_t i) { return i < fol.size() ? std::max(fol[i].mass, 0.0f) : 0.0f; };

    // The trunk: the leader chain from the thickest root segment, taking the
    // thickest child at each fork while it stays dominant (>= 80% of its
    // parent; a fork into equal limbs ends the trunk) and >= 35% of the base.
    std::vector<char> trunk(n, 0);
    int cur = -1;
    for (size_t i = 0; i < n; ++i) {
        if (parentOf(i) < 0 && (cur < 0 || segs[i].radius > segs[cur].radius)) cur = static_cast<int>(i);
    }
    const int trunkBase = cur;
    const float baseR = cur >= 0 ? segs[cur].radius : 0.0f;
    while (cur >= 0) {
        trunk[cur] = 1;
        int next = -1;
        for (int c : children[cur]) {
            if (next < 0 || segs[c].radius > segs[next].radius) next = c;
        }
        if (next < 0) break;
        const float r = segs[next].radius;
        if (r < 0.35f * baseR || r < 0.8f * segs[cur].radius) break;
        cur = next;
    }

    // Longest way from each segment's tip to a twig tip, off the trunk.
    std::vector<float> rest(n, 0.0f);
    for (size_t k = n; k-- > 0; ) {
        for (int c : children[k]) {
            if (!trunk[c]) rest[k] = std::max(rest[k], len[c] + rest[c]);
        }
    }
    // The crown: where the foliage mass sits, and how far it reaches.
    bromath::Vec3 c{0.0f, 0.0f, 0.0f};
    float wsum = 0.0f;
    for (int pass = 0; pass < 2 && wsum <= 0.0f; ++pass) {
        for (size_t i = 0; i < n; ++i) {
            if (trunk[i]) continue;
            const float w = (pass == 0 ? mass(i) : 1.0f) * len[i];
            if (!(w > 0.0f)) continue;
            c = c + (segs[i].from + segs[i].to) * (0.5f * w);
            wsum += w;
        }
    }
    float crownR = 0.0f;
    if (wsum > 0.0f) {
        c = c * (1.0f / wsum);
        const bool byMass = [&] {
            for (size_t i = 0; i < n; ++i) if (!trunk[i] && mass(i) > 0.0f) return true;
            return false;
        }();
        for (size_t i = 0; i < n; ++i) {
            if (trunk[i] || (byMass && !(mass(i) > 0.0f))) continue;
            crownR = std::max(crownR, bromath::vlen(segs[i].to - c));
        }
    } else {
        c = plant.origin + bromath::Vec3{0.0f, 0.5f * common.height, 0.0f};
    }
    common.crown = c;
    common.crownRadius = crownR;

    // The wind meets the crown's face, kSwayCrownFill of a disc of its
    // radius, shared among the segments by their foliage (the simulation's
    // density times length); each segment weighs its wood and the leaves
    // behind its share of that face.
    float foliage = 0.0f;
    for (size_t i = 0; i < n; ++i) foliage += mass(i) * len[i];
    const float faceArea = kSwayCrownFill * 3.14159265f * crownR * crownR;
    auto area = [&](size_t i) { return foliage > 0.0f ? faceArea * mass(i) * len[i] / foliage : 0.0f; };
    auto pieceMass = [&](size_t i) {
        const float r = std::max(segs[i].radius, 0.0f);
        return kSwayWoodDensity * 3.14159265f * r * r * len[i] + kSwayLeafMass * area(i);
    };

    // The trunk as one cantilever: its base diameter, the plant's height,
    // everything on the plant placed by its height.
    CantileverLoad trunkLoad;
    for (size_t i = 0; i < n; ++i) {
        const float y = 0.5f * (segs[i].from.y + segs[i].to.y) - plant.origin.y;
        const float phi = swayModeShape(y / common.height);
        trunkLoad.mass += pieceMass(i) * phi * phi;
        trunkLoad.area += area(i) * phi;
    }
    const float trunkD = 2.0f * (trunkBase >= 0 ? segs[trunkBase].radius : 0.0f);
    common.trunk = swayCantilever(trunkD, common.height, trunkLoad, kSwayTrunkGain);

    // Each limb's load: every segment of it placed by how far along the
    // limb it sits (the way from where the limb leaves the trunk). Emit
    // order puts parents first, so one pass finds each segment's limb.
    std::vector<int> limbOf(n, -1);
    std::vector<float> along(n, 0.0f);
    std::vector<CantileverLoad> limbLoad(n);
    for (size_t i = 0; i < n; ++i) {
        if (trunk[i]) continue;
        const int p = parentOf(i);
        if (p < 0 || trunk[p]) {
            limbOf[i] = static_cast<int>(i);
        } else {
            limbOf[i] = limbOf[p];
            along[i] = along[p] + len[p];
        }
        const int l = limbOf[i];
        const float span = std::max(len[l] + rest[l], 1e-6f);
        const float phi = swayModeShape((along[i] + 0.5f * len[i]) / span);
        limbLoad[l].mass += pieceMass(i) * phi * phi;
        limbLoad[l].area += area(i) * phi;
    }

    // Branch weight, continuous along every path: 0 on the trunk and where a
    // limb leaves it, then each segment covers its share of what is left to
    // the farthest tip beyond it, reaching 1 at every tip. Limbs (the
    // subtrees leaving the trunk, or rooted beside it) each draw a phase and
    // are a cantilever of their own, with their twigs another: a limb's
    // segments share both, so a limb bends as one piece.
    const uint64_t plantSeed = static_cast<uint64_t>(std::llround(plant.origin.x * 1013.0f)) * 0x9E3779B97F4A7C15ULL
                             ^ static_cast<uint64_t>(std::llround(plant.origin.z * 7919.0f));
    for (size_t i = 0; i < n; ++i) {
        SegmentSway& s = out[base + i];
        s = common;
        if (trunk[i]) continue;
        const int p = parentOf(i);
        const bool limbStart = (p < 0) || trunk[p];
        s.weightFrom = limbStart ? 0.0f : out[base + p].weightTo;
        const float span = len[i] + rest[i];
        s.weightTo = span > 1e-6f ? s.weightFrom + (1.0f - s.weightFrom) * len[i] / span : 1.0f;
        if (!limbStart) {
            s.phase = out[base + p].phase;
            s.limb = out[base + p].limb;
            s.twig = out[base + p].twig;
            continue;
        }
        s.phase = hash01(plantSeed + i * 0x2545F4914F6CDD1DULL);
        const float limbLen = len[i] + rest[i];
        s.limb = swayCantilever(2.0f * segs[i].radius, limbLen, limbLoad[i], kSwayLimbGain);

        // The limb's twigs: the leafy sprays at its tips. The simulation
        // stops resolving shoots at its thinnest segment, so a spray is at
        // least kSwaySprayLength long (or one and a half of the limb's tip
        // segments) and at most kSwaySprayDiameter thick at its base, with
        // kSwayLeafBreadth of leaf face a metre.
        float tipLen = 0.0f, tipR = segs[i].radius;
        int tips = 0;
        std::vector<int> stack{static_cast<int>(i)};
        while (!stack.empty()) {
            const int k = stack.back();
            stack.pop_back();
            if (segs[k].radius > 0.0f) tipR = std::min(tipR, segs[k].radius);
            bool leafEnd = true;
            for (int ch : children[k]) {
                if (!trunk[ch]) { stack.push_back(ch); leafEnd = false; }
            }
            if (leafEnd && len[k] > 1e-6f) {
                tipLen += len[k];
                ++tips;
            }
        }
        if (tips > 0) {
            // A uniform shoot: its wood and leaves spread along it (33/140 of
            // their mass in the mode, 3/8 of their drag). A bare limb's
            // sprays carry no leaves, so they ring but do not deflect.
            const float twigLen = std::min(std::max(1.5f * tipLen / static_cast<float>(tips), kSwaySprayLength), limbLen);
            const float d = std::min(2.0f * tipR, kSwaySprayDiameter);
            const float face = limbLoad[i].area > 0.0f ? kSwayLeafBreadth * twigLen : 0.0f;
            CantileverLoad tl;
            tl.mass = swayBeamMass(d, twigLen) + (33.0f / 140.0f) * kSwayLeafMass * face;
            tl.area = 0.375f * face;
            s.twig = swayCantilever(d, twigLen, tl, kSwayTwigGain);
        }
    }
}

// A positive number on a log scale over `range`, `bits` wide: code 0 is
// zero, 1 .. 2^bits - 1 the range end to end.
uint32_t logCode(float v, const float (&range)[2], int bits) {
    if (!(v > 0.0f)) return 0;
    const uint32_t top = (1u << bits) - 1u;
    const float t = (std::log(v) - std::log(range[0])) / (std::log(range[1]) - std::log(range[0]));
    return 1u + static_cast<uint32_t>(std::lround(std::clamp(t, 0.0f, 1.0f) * static_cast<float>(top - 1u)));
}

float logValue(uint32_t code, const float (&range)[2], int bits) {
    if (code == 0) return 0.0f;
    const uint32_t top = (1u << bits) - 1u;
    const float t = static_cast<float>(code - 1u) / static_cast<float>(top - 1u);
    return range[0] * std::pow(range[1] / range[0], t);
}

uint32_t wordOf(float f) { return f > 0.0f ? static_cast<uint32_t>(std::lround(f)) : 0u; }

} // namespace

float swayModeShape(float s) {
    s = std::clamp(s, 0.0f, 1.0f);
    return s * s * (3.0f - s) * 0.5f;
}

float swayBeamMass(float d, float L) {
    return (33.0f / 140.0f) * kSwayWoodDensity * 3.14159265f * 0.25f * d * d * std::max(L, 0.0f);
}

Cantilever swayCantilever(float d, float L, const CantileverLoad& load, float gain) {
    Cantilever c;
    const float pi = 3.14159265f;
    d = std::max(d, 1.0e-3f);
    c.length = std::max(L, 1.0e-3f);
    const float d2 = d * d;
    // The beam's tip stiffness, softened by the gain (what a uniform beam
    // on a rigid footing leaves out).
    const float k = 3.0f * kSwayWoodModulus * (pi * d2 * d2 / 64.0f) / (c.length * c.length * c.length) /
                    std::max(gain, 1e-3f);
    const float m = std::max(load.mass, swayBeamMass(d, c.length));
    c.freq = std::clamp(std::sqrt(k / m) / (2.0f * pi), 0.08f, 12.0f);
    const float U = kSwayReferenceWind;
    const float F = 0.5f * kSwayAirDensity * kSwayDrag * std::max(load.area, 0.0f) * U * U;
    c.sway = std::clamp(F / (k * c.length), 0.0f, 0.5f);
    return c;
}

float packSwayAxis(const Vec3& axis) {
    float x = axis.x, y = axis.y, z = axis.z;
    const float l1 = std::fabs(x) + std::fabs(y) + std::fabs(z);
    if (!(l1 > 1e-12f)) return 0.0f;
    x /= l1; y /= l1; z /= l1;
    if (z < 0.0f) {
        const float ox = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float oy = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = ox; y = oy;
    }
    const auto q = [](float e) { return static_cast<uint32_t>(std::lround((std::clamp(e, -1.0f, 1.0f) * 0.5f + 0.5f) * 4095.0f)); };
    return static_cast<float>(1u + q(x) * 4096u + q(y));
}

Vec3 unpackSwayAxis(float word) {
    const uint32_t w = wordOf(word);
    if (w == 0) return {0.0f, 0.0f, 0.0f};
    const uint32_t u = w - 1u;
    float x = static_cast<float>(u >> 12) / 4095.0f * 2.0f - 1.0f;
    float y = static_cast<float>(u & 4095u) / 4095.0f * 2.0f - 1.0f;
    float z = 1.0f - std::fabs(x) - std::fabs(y);
    if (z < 0.0f) {
        const float ox = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float oy = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = ox; y = oy;
    }
    const float l = std::sqrt(x * x + y * y + z * z);
    return {x / l, y / l, z / l};
}

float packSwayTwig(float phase, const Cantilever& t) {
    const uint32_t ph = static_cast<uint32_t>(std::clamp(phase, 0.0f, 0.999999f) * 64.0f);
    return static_cast<float>((ph << 18) | (logCode(t.freq, kSwayFreqRange, 6) << 12) |
                              (logCode(t.sway, kSwaySwayRange, 6) << 6) | logCode(t.length, kSwayLengthRange, 6));
}

Cantilever unpackSwayTwig(float word, float* phase) {
    const uint32_t w = wordOf(word);
    if (phase) *phase = static_cast<float>(w >> 18) / 64.0f;
    return {logValue((w >> 12) & 63u, kSwayFreqRange, 6), logValue((w >> 6) & 63u, kSwaySwayRange, 6),
            logValue(w & 63u, kSwayLengthRange, 6)};
}

float packSwayLimb(const Cantilever& l) {
    return static_cast<float>((logCode(l.freq, kSwayFreqRange, 8) << 16) | (logCode(l.sway, kSwaySwayRange, 8) << 8) |
                              logCode(l.length, kSwayLengthRange, 8));
}

Cantilever unpackSwayLimb(float word) {
    const uint32_t w = wordOf(word);
    return {logValue(w >> 16, kSwayFreqRange, 8), logValue((w >> 8) & 255u, kSwaySwayRange, 8),
            logValue(w & 255u, kSwayLengthRange, 8)};
}

float packSwayTrunk(const Cantilever& t) {
    return static_cast<float>((logCode(t.freq, kSwayFreqRange, 12) << 12) | logCode(t.sway, kSwaySwayRange, 12));
}

Cantilever unpackSwayTrunk(float word) {
    const uint32_t w = wordOf(word);
    return {logValue(w >> 12, kSwayFreqRange, 12), logValue(w & 4095u, kSwaySwayRange, 12), 0.0f};
}

void writeVegetation(float* o, const SegmentSway& s, const Vec3& anchor,
                     float weight, const Vec3& axis, bool leaf) {
    o[0] = s.root.x;   o[1] = s.root.y;   o[2] = s.root.z;   o[3] = s.height;
    o[4] = s.crown.x;  o[5] = s.crown.y;  o[6] = s.crown.z;  o[7] = leaf ? s.crownRadius : 0.0f;
    o[8] = anchor.x;   o[9] = anchor.y;   o[10] = anchor.z;  o[11] = std::clamp(weight, 0.0f, 1.0f);
    o[12] = leaf ? packSwayAxis(axis) : 0.0f;
    o[13] = packSwayTwig(s.phase, s.twig);
    o[14] = packSwayLimb(s.limb);
    o[15] = packSwayTrunk(s.trunk);
}

void writeSegmentVegetation(float* o, const SegmentSway& s, const Vec3& from, const Vec3& to) {
    writeVegetation(o, s, from, s.weightFrom, Vec3{0.0f, 0.0f, 0.0f}, false);
    o[4] = to.x; o[5] = to.y; o[6] = to.z;
    o[7] = -(1.0f + std::clamp(s.weightTo, 0.0f, 1.0f));
}

std::vector<SegmentSway> emitPlantSway(const Plant& plant) {
    std::vector<SegmentSway> out;
    computePlantSway(plant, emitPlantSegments(plant), emitPlantFoliage(plant), out);
    return out;
}

std::vector<SegmentSway> emitWorldSway(const WorldState& world) {
    std::vector<SegmentSway> out;
    for (const auto& plant : world.plants) {
        computePlantSway(plant, emitPlantSegments(plant), emitPlantFoliage(plant), out);
    }
    return out;
}

std::vector<float> leafVegetation(const bromesh::LeafPlacements& pl,
                                  const std::vector<bromesh::BranchSegment>& segs,
                                  const std::vector<SegmentSway>& sway) {
    const size_t count = pl.count();
    std::vector<float> out(count * kVegetationFloats, 0.0f);
    if (pl.segment.size() != count) return out;
    for (size_t i = 0; i < count; ++i) {
        const float* M = &pl.transforms[i * 16];
        const Vec3 P{M[3], M[7], M[11]};
        Vec3 F{M[2], M[6], M[10]};
        const float fl = bromath::vlen(F);
        F = fl > 1e-8f ? F * (1.0f / fl) : Vec3{0.0f, 1.0f, 0.0f};
        const int32_t si = pl.segment[i];
        if (si < 0 || static_cast<size_t>(si) >= segs.size() || static_cast<size_t>(si) >= sway.size()) continue;
        const auto& seg = segs[static_cast<size_t>(si)];
        const SegmentSway& s = sway[static_cast<size_t>(si)];
        const Vec3 d = seg.to - seg.from;
        const float dd = bromath::vdot(d, d);
        const float t = dd > 1e-12f ? std::clamp(bromath::vdot(P - seg.from, d) / dd, 0.0f, 1.0f) : 1.0f;
        writeVegetation(&out[i * kVegetationFloats], s, P, s.weightFrom + (s.weightTo - s.weightFrom) * t, F, true);
    }
    return out;
}

std::vector<float> vegetationPerVertex(const std::vector<float>& perItem, size_t verts) {
    const size_t items = perItem.size() / kVegetationFloats;
    std::vector<float> out(items * verts * kVegetationFloats);
    for (size_t i = 0; i < items; ++i) {
        for (size_t v = 0; v < verts; ++v) {
            std::copy_n(&perItem[i * kVegetationFloats], kVegetationFloats,
                        &out[(i * verts + v) * kVegetationFloats]);
        }
    }
    return out;
}

void appendSegmentVegetation(std::vector<float>& out, const SegmentSway& s) {
    const float v[kSegmentVegetationFloats] = {
        s.root.x, s.root.y, s.root.z, s.height,
        s.crown.x, s.crown.y, s.crown.z, s.crownRadius,
        s.weightFrom, s.weightTo, 0.0f, 0.0f,
        0.0f, packSwayTwig(s.phase, s.twig), packSwayLimb(s.limb), packSwayTrunk(s.trunk),
    };
    out.insert(out.end(), v, v + kSegmentVegetationFloats);
}

// Bark-mesh vegetation, laid out like bromesh::meshBranches' chains (the
// walk bakeWindDeformation makes): ring 0 at the chain's first `from`, ring
// r + 1 at its r-th segment's `to`, then the start / end caps.
void bakeVegetation(MeshData& mesh,
                    const std::vector<bromesh::BranchSegment>& segs,
                    const std::vector<SegmentSway>& sway,
                    int sides) {
    const size_t vc = mesh.vertexCount();
    if (vc == 0 || segs.empty() || sway.size() != segs.size()) return;
    mesh.vegetation.assign(vc * kVegetationFloats, 0.0f);
    const Vec3 zero{0.0f, 0.0f, 0.0f};

    std::vector<std::vector<int>> children(segs.size());
    for (size_t i = 0; i < segs.size(); ++i) {
        const int p = segs[i].parent;
        if (p >= 0 && static_cast<size_t>(p) < segs.size()) children[p].push_back(static_cast<int>(i));
    }
    auto write = [&](size_t v, size_t seg, const Vec3& at, float w) {
        if (v < vc) writeVegetation(&mesh.vegetation[v * kVegetationFloats], sway[seg], at, w, zero, false);
    };

    size_t off = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        const int p = segs[i].parent;
        if (p >= 0 && children[p].size() == 1) continue;   // not a chain start
        std::vector<int> chain;
        int cur = static_cast<int>(i);
        while (true) {
            if (bromath::vdist2(segs[cur].from, segs[cur].to) > 1e-12f) chain.push_back(cur);
            if (children[cur].size() != 1) break;
            cur = children[cur][0];
        }
        if (chain.empty()) continue;
        const bool capStart = p < 0;
        const bool capEnd = children[cur].empty();

        for (int k = 0; k < sides; ++k) write(off + k, chain[0], segs[chain[0]].from, sway[chain[0]].weightFrom);
        for (size_t r = 0; r < chain.size(); ++r) {
            const size_t ring = off + (r + 1) * static_cast<size_t>(sides);
            for (int k = 0; k < sides; ++k) write(ring + k, chain[r], segs[chain[r]].to, sway[chain[r]].weightTo);
        }
        size_t next = off + (chain.size() + 1) * static_cast<size_t>(sides);
        if (capStart) { write(next, chain[0], segs[chain[0]].from, sway[chain[0]].weightFrom); ++next; }
        if (capEnd) { write(next, chain.back(), segs[chain.back()].to, sway[chain.back()].weightTo); ++next; }
        off = next;
    }
    // Anything past the chains (none, for meshBranches output) stays rooted.
    for (size_t v = off; v < vc; ++v) {
        const Vec3 at{mesh.positions[v * 3], mesh.positions[v * 3 + 1], mesh.positions[v * 3 + 2]};
        write(v, 0, at, 0.0f);
    }
}

} // namespace broflora
