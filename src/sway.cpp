// Wind sway: each plant as a trunk, limbs and twigs, each a cantilever whose
// frequency and deflection come from its diameter, length and the leaves it
// carries; and the vegetation records that carry them to the renderer
// (include/broflora/sway.h).
#include "broflora/sway.h"

#include "broflora/forest_records.h"
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
    // thickest child at each fork while it stays the leader: >= 80% of its
    // parent, or kSwayLeaderDominance times the next thickest child (a
    // conifer's leader through a whorl that takes much of its wood). A fork
    // into near-equal limbs ends it (a broadleaf's decurrent crown).
    std::vector<char> trunk(n, 0);
    int cur = -1;
    for (size_t i = 0; i < n; ++i) {
        if (parentOf(i) < 0 && (cur < 0 || segs[i].radius > segs[cur].radius)) cur = static_cast<int>(i);
    }
    const int trunkBase = cur;
    while (cur >= 0) {
        trunk[cur] = 1;
        int next = -1;
        float second = 0.0f;
        for (int c : children[cur]) {
            if (next < 0 || segs[c].radius > segs[next].radius) {
                if (next >= 0) second = std::max(second, segs[next].radius);
                next = c;
            } else {
                second = std::max(second, segs[c].radius);
            }
        }
        if (next < 0) break;
        const float r = segs[next].radius;
        if (!(r > 0.0f) || (r < 0.8f * segs[cur].radius && r < kSwayLeaderDominance * second)) break;
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
    // The face the wind meets at the piece, in the shelter of the crown
    // above, kSwayReferenceWind blowing at kSwayReferenceHeight of the plant.
    const float refShelter = swayShelter(kSwayReferenceHeight * common.height, common.height);
    auto drag = [&](size_t i) {
        return area(i) * swayShelter(0.5f * (segs[i].from.y + segs[i].to.y) - plant.origin.y, common.height) /
               refShelter;
    };
    auto pieceMass = [&](size_t i) {
        const float r = std::max(segs[i].radius, 0.0f);
        return kSwayWoodDensity * 3.14159265f * r * r * len[i] + kSwayLeafMass * area(i);
    };

    // The trunk: its stem bent by the drag on everything it carries, each
    // piece pushing at its own height (swayStem).
    {
        // The stem that bends: the leader, on through its thickest child at
        // every fork to the top (past a broadleaf's fork into equal limbs too:
        // what stands on the fork leans with the wood under it).
        std::vector<SwayStemWood> stemWood;
        std::vector<SwayStemLoad> load;
        for (int k = trunkBase; k >= 0;) {
            stemWood.push_back({segs[k].from.y - plant.origin.y, segs[k].to.y - plant.origin.y, segs[k].radius});
            int next = -1;
            for (int ch : children[k]) {
                if (next < 0 || segs[ch].radius > segs[next].radius) next = ch;
            }
            k = next;
        }
        load.reserve(n);
        const float q = 0.5f * kSwayAirDensity * kSwayDrag * kSwayReferenceWind * kSwayReferenceWind;
        for (size_t i = 0; i < n; ++i) {
            load.push_back({0.5f * (segs[i].from.y + segs[i].to.y) - plant.origin.y, q * drag(i), pieceMass(i)});
        }
        common.trunk = swayStem(stemWood, load, common.height);
        if (!(common.trunk.freq > 0.0f)) {
            // No stem to speak of: the uniform beam of the base diameter.
            CantileverLoad trunkLoad;
            for (size_t i = 0; i < n; ++i) {
                const float phi = swayModeShape(load[i].z / common.height);
                trunkLoad.mass += pieceMass(i) * phi * phi;
                trunkLoad.area += drag(i) * phi;
            }
            const float trunkD = 2.0f * (trunkBase >= 0 ? segs[trunkBase].radius : 0.0f);
            common.trunk = swayCantilever(trunkD, common.height, trunkLoad, kSwayTrunkGain);
        }
    }

    // Each limb's load: every segment of it placed by how far along the
    // limb it sits (the way from where the limb leaves the trunk). Emit
    // order puts parents first, so one pass finds each segment's limb.
    std::vector<int> limbOf(n, -1);
    std::vector<float> along(n, 0.0f);
    std::vector<CantileverLoad> limbLoad(n);
    std::vector<float> limbOpen(n, 0.0f);   // its leaf face out of the crown's shelter
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
        limbLoad[l].weight += pieceMass(i) * phi;
        limbLoad[l].area += drag(i) * phi;
        limbOpen[l] += area(i) * phi;
    }

    // Branch weight, continuous along every path: 0 on the trunk and where a
    // limb leaves it, then each segment covers its share of what is left to
    // the farthest tip beyond it, reaching 1 at every tip. Limbs (the
    // subtrees leaving the trunk, or rooted beside it) each draw a phase and
    // are a cantilever of their own, with their twigs another: a limb's
    // segments share both, so a limb bends as one piece.
    const uint64_t plantSeed = static_cast<uint64_t>(std::llround(plant.origin.x * 1013.0f)) * 0x9E3779B97F4A7C15ULL
                             ^ static_cast<uint64_t>(std::llround(plant.origin.z * 7919.0f));
    common.bark = packBarkWord(plant.species.barkFurrow, plant.species.barkScale, plant.species.barkMoss,
                               plant.species.barkRibs > 0.5f);
    for (size_t i = 0; i < n; ++i) {
        SegmentSway& s = out[base + i];
        s = common;
        {
            const int p = parentOf(i);
            s.pathFrom = p < 0 ? 0.0f : out[base + p].pathFrom + len[p];
        }
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
            for (int ch : children[k]) {
                if (!trunk[ch]) stack.push_back(ch);
            }
            // A tip: nothing of any length beyond it (a shoot's newest
            // module can be a zero-length bud, which is no tip of its own
            // and leaves the shoot under it one; a lavender's every shoot).
            if (rest[k] <= 1e-6f && len[k] > 1e-6f) {
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
            tl.weight = 0.375f * (kSwayWoodDensity * 0.785398f * d * d * twigLen + kSwayLeafMass * face);
            // Its sprays stand in the limb's shelter (the share of its face's
            // drag the crown above leaves it).
            const float shelter = limbOpen[i] > 0.0f ? limbLoad[i].area / limbOpen[i] : 1.0f;
            tl.area = 0.375f * face * shelter;
            s.twig = swayCantilever(d, twigLen, tl, kSwayTwigGain);
            // A limb rings under the sprays it carries: a short stiff limb
            // (a shrub's stem) is held at its fork by them as much as by its
            // wood, so it rings no faster than 0.8 of them (what keeps a
            // boxwood's or a lavender's stems from buzzing at 5-9 Hz). Only
            // the frequency moves; how far it bends is its stiffness's. A
            // thick-tipped limb's sprays, as soft as their fork lets them be,
            // would otherwise fall under it and move as one with it.
            s.limb.freq = std::min(s.limb.freq, 0.8f * s.twig.freq);
        }
    }

    // A climber's wood holds to its host's bark (Plant::support), standing
    // where the host stands: every piece of it is the host's stem at its
    // height, bending with it, with no limb of its own.
    const float* host = plant.supportSway;
    if (plant.species.climb > 0.0f && !plant.support.empty() && host[0] > 0.0f) {
        for (size_t i = 0; i < n; ++i) {
            SegmentSway& s = out[base + i];
            s.height = host[0];
            s.trunk = Cantilever{host[1], host[2], host[3], host[4]};
            s.weightFrom = s.weightTo = 0.0f;
            s.limb = s.twig = Cantilever{};
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

float swayShelter(float z, float h) {
    const float depth = std::clamp(1.0f - z / std::max(h, 1e-3f), 0.0f, 1.0f);
    return std::exp(-2.0f * kSwayCrownShelter * depth);
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
    // At least as stiff as holding its own weight takes (kSwaySagLimit).
    const float k = std::max(3.0f * kSwayWoodModulus * (pi * d2 * d2 / 64.0f) / (c.length * c.length * c.length) /
                                 std::max(gain, 1e-3f),
                             9.81f * std::max(load.weight, 0.0f) / (kSwaySagLimit * c.length));
    const float m = std::max(load.mass, swayBeamMass(d, c.length));
    c.freq = std::clamp(std::sqrt(k / m) / (2.0f * pi), 0.08f, 12.0f);
    const float U = kSwayReferenceWind;
    const float F = 0.5f * kSwayAirDensity * kSwayDrag * std::max(load.area, 0.0f) * U * U;
    // No further than its wood's working strain lets it bend: a tip
    // deflection d bends the base of a cantilever to the strain 3 D d / (2 L^2).
    const float strainCap = 2.0f * kSwayWorkingStrain * c.length / (3.0f * d);
    c.sway = std::clamp(F / (k * c.length), 0.0f, std::min(0.5f, strainCap));
    return c;
}

Cantilever swayStem(const std::vector<SwayStemWood>& wood, const std::vector<SwayStemLoad>& load, float height) {
    Cantilever c;
    const float H = height;
    if (wood.empty() || !(H > 0.0f)) return c;
    const float pi = 3.14159265f;
    float zTop = 0.0f;
    for (const auto& w : wood) zTop = std::max({zTop, w.z0, w.z1});
    zTop = std::min(zTop, H);
    const float d0 = 2.0f * wood.front().radius;
    if (!(d0 > 0.0f) || zTop < 0.05f * H) return c;

    // The stem's diameter at height z: the first piece of the chain (root
    // up) that reaches it; above the chain's top, its last piece's. The
    // leader's last shoots are stiffened by the side shoots and needles
    // round them, so no thinner than kSwayStemTopFloor of the base.
    auto diam = [&](float z) {
        const float floor = kSwayStemTopFloor * d0;
        for (const auto& w : wood) {
            if (std::max(w.z0, w.z1) >= z) return std::max(2.0f * w.radius, floor);
        }
        return std::max(2.0f * wood.back().radius, floor);
    };
    double drag = 0.0;
    for (const auto& l : load) drag += l.force;
    if (!(drag > 0.0)) return c;

    // The bend: a stem grown to its load is strained alike along its length,
    // so its curvature is 2 strain / d, most where it is thinnest; above the
    // leader's wood (a broadleaf past its last fork) it is straight.
    constexpr int N = 96;
    const float dz = H / N;
    std::vector<float> kap(N + 1, 0.0f), th(N + 1, 0.0f), y(N + 1, 0.0f);
    for (int k = 0; k <= N; ++k) kap[k] = k * dz > zTop ? 0.0f : 2.0f * kSwayStemStrain / diam(k * dz);
    for (int k = 1; k <= N; ++k) {
        th[k] = th[k - 1] + 0.5f * (kap[k - 1] + kap[k]) * dz;
        y[k] = y[k - 1] + 0.5f * (th[k - 1] + th[k]) * dz;
    }
    // The root plate's turn: kSwayRootShare of the stem's own bend at the top.
    const float root = kSwayRootShare * y[N] / H;
    for (int k = 0; k <= N; ++k) y[k] += root * k * dz;
    const float top = y[N];
    if (!(top > 0.0f)) return c;
    c.length = H;
    c.sway = std::clamp(top / H, 0.0f, 0.5f);
    // The shape as h^p, matched on its area over the height (a uniform
    // beam's end-loaded bend is p 1.67, a tapered stem's more, the root's
    // turn alone 1).
    double area = 0.0;
    for (int k = 1; k <= N; ++k) area += 0.5 * (y[k - 1] + y[k]) / top / N;
    c.shape = std::clamp(static_cast<float>(1.0 / std::max(area, 1e-3) - 1.0), kSwayShapeRange[0], kSwayShapeRange[1]);

    // The frequency, Rayleigh's over that bend: the energy it stores (the
    // stem's E I kappa^2, its modulus softened by kSwayStemGain, and the
    // root plate's moment through its turn) over the mass it moves.
    const float E = kSwayWoodModulus / kSwayStemGain;
    double energy = 0.0;
    for (int k = 0; k <= N; ++k) {
        const float d = diam(k * dz);
        const double EI = E * pi * std::pow(static_cast<double>(d), 4.0) / 64.0;
        energy += (k == 0 || k == N ? 0.5 : 1.0) * EI * kap[k] * kap[k] * dz;
    }
    energy += E * pi * std::pow(static_cast<double>(d0), 4.0) / 64.0 * kap[0] * root;
    double mass = 0.0;
    for (const auto& l : load) {
        float s = l.z >= H ? (top + (th[N] + root) * (l.z - H)) / top : 0.0f;
        if (l.z < H) {
            const float u = std::max(l.z, 0.0f) / dz;
            const int k = std::min(static_cast<int>(u), N - 1);
            s = (y[k] + (y[k + 1] - y[k]) * (u - k)) / top;
        }
        mass += static_cast<double>(l.mass) * s * s;
    }
    const double stiff = energy / (static_cast<double>(top) * top);
    c.freq = mass > 0.0 ? std::clamp(static_cast<float>(std::sqrt(stiff / mass) / (2.0 * pi)), 0.08f, 12.0f) : 12.0f;
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

// The trunk's shape exponent in 6 bits: code 0 unset (the uniform beam's
// bend), 1 .. 63 linear over kSwayShapeRange.
static uint32_t shapeCode(float p) {
    if (!(p > 0.0f)) return 0;
    const float t = (p - kSwayShapeRange[0]) / (kSwayShapeRange[1] - kSwayShapeRange[0]);
    return 1u + static_cast<uint32_t>(std::lround(std::clamp(t, 0.0f, 1.0f) * 62.0f));
}

float packSwayTrunk(const Cantilever& t) {
    return static_cast<float>((logCode(t.freq, kSwayFreqRange, 9) << 15) | (logCode(t.sway, kSwaySwayRange, 9) << 6) |
                              shapeCode(t.shape));
}

Cantilever unpackSwayTrunk(float word) {
    const uint32_t w = wordOf(word);
    Cantilever c{logValue(w >> 15, kSwayFreqRange, 9), logValue((w >> 6) & 511u, kSwaySwayRange, 9), 0.0f};
    const uint32_t s = w & 63u;
    c.shape = s == 0 ? 0.0f : kSwayShapeRange[0] + (kSwayShapeRange[1] - kSwayShapeRange[0]) * static_cast<float>(s - 1u) / 62.0f;
    return c;
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
    appendSegmentVegetation(out, s, 0.0f, 1.0f, 0.0f);
}

void appendSegmentVegetation(std::vector<float>& out, const SegmentSway& s, float along0, float along1,
                             float length) {
    namespace v = forest_records::vegetation;
    static_assert(v::kFloats == kSegmentVegetationFloats);
    const auto at = [&](float t) { return s.weightFrom + (s.weightTo - s.weightFrom) * t; };
    float* r = forest_records::append(out, v::kFloats);
    r[v::kRoot] = s.root.x; r[v::kRoot + 1] = s.root.y; r[v::kRoot + 2] = s.root.z;
    r[v::kHeight] = s.height;
    r[v::kCrown] = s.crown.x; r[v::kCrown + 1] = s.crown.y; r[v::kCrown + 2] = s.crown.z;
    r[v::kCrownRadius] = s.crownRadius;
    r[v::kWeightFrom] = at(along0);
    r[v::kWeightTo] = at(along1);
    r[v::kPath] = s.pathFrom + along0 * length;
    r[v::kBark] = s.bark;
    r[v::kTwig] = packSwayTwig(s.phase, s.twig);
    r[v::kLimb] = packSwayLimb(s.limb);
    r[v::kTrunk] = packSwayTrunk(s.trunk);
}

float packBarkWord(float furrow, float scale, float moss, bool ribs) {
    const auto lin = [](float v, float top) {
        return 1u + static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * top));
    };
    const uint32_t sc = scale > 0.0f ? logCode(scale, kBarkScaleRange, 8) : 0u;
    return static_cast<float>(lin(furrow, 254.0f) | (sc << 8) | (lin(moss, 126.0f) << 16) | (ribs ? 1u << 23 : 0u));
}

void unpackBarkWord(float word, float& furrow, float& scale, float& moss, bool& ribs) {
    const uint32_t w = wordOf(word);
    const auto lin = [](uint32_t c, float top, float dflt) { return c == 0 ? dflt : static_cast<float>(c - 1u) / top; };
    const Species dflt;
    furrow = lin(w & 255u, 254.0f, dflt.barkFurrow);
    const uint32_t sc = (w >> 8) & 255u;
    scale = sc == 0 ? dflt.barkScale : logValue(sc, kBarkScaleRange, 8);
    moss = lin((w >> 16) & 127u, 126.0f, dflt.barkMoss);
    ribs = ((w >> 23) & 1u) != 0;
}

void unpackBarkWord(float word, float& furrow, float& scale, float& moss) {
    bool ribs;
    unpackBarkWord(word, furrow, scale, moss, ribs);
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
