#include "broflora/spawning.h"

#include "bromath/rng.h"
#include "bromath/scalar.h"
#include "bromath/spatial_hash.h"
#include "bromath/sphere.h"
#include "bromath/vec.h"
#include "internal_geom.h"
#include "internal_select.h"
#include "internal_spatial.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace broflora {

using bromath::Vec3;
using bromath::vdot;
using bromath::vlen2;
using bromath::vnorm;
using bromath::Sphere;
using bromath::sintersectVolume;
using bromath::randSigned;
using internal::rotateYawPitch;
using internal::nodeOffsetFromRoot;

// Defined in this TU; declared in internal_select.h so senescence.cpp
// (and any future caller) can share the same juvenile (D, λ) lookup.
const BranchModulePrototype* internal::pickPrototype(const WorldState& world,
                                                    float dPrime, float lambda) {
    uint32_t bestIdx = UINT32_MAX;
    float bestD2 = std::numeric_limits<float>::infinity();
    for (const auto& site : world.voronoi) {
        if (site.prototypeIndex >= world.prototypes.size()) continue;
        float dd = site.determinacy - dPrime;
        float dl = site.apicalControl - lambda;
        float d2 = dd * dd + dl * dl;
        if (d2 < bestD2) { bestD2 = d2; bestIdx = site.prototypeIndex; }
    }
    return prototypeAt(world, bestIdx);
}

namespace {

// Prediction of a freshly-attached module's bbox + growth axis given a
// candidate (θ, ψ). Uses the prototype's *static* node positions so the
// gradient descent reasons about where the module will grow to, not its
// zero-extent newborn state.
struct OrientationHypothesis {
    Vec3  centre;
    float radius;
    Vec3  axis;   // unit vector, root → first terminal in world space
};

OrientationHypothesis predictHypothesis(const BranchModulePrototype& proto,
                                        Vec3 attachWorld, float theta, float psi) {
    OrientationHypothesis h;
    h.centre = attachWorld;
    h.radius = 0.0f;
    h.axis   = {0.0f, 1.0f, 0.0f};
    if (proto.nodes.empty()) return h;

    const Vec3 root = proto.nodes[proto.rootNode].position;
    std::vector<Vec3> rotated;
    rotated.reserve(proto.nodes.size());
    Vec3 sum = {0.0f, 0.0f, 0.0f};
    for (const auto& nd : proto.nodes) {
        Vec3 loc = nd.position - root;
        Vec3 r   = rotateYawPitch(loc, psi, theta);
        rotated.push_back(r);
        sum += r;
    }
    Vec3 mean = sum * (1.0f / static_cast<float>(rotated.size()));
    float maxD2 = 0.0f;
    for (const auto& r : rotated) {
        maxD2 = std::max(maxD2, vlen2(r - mean));
    }
    h.centre = attachWorld + mean;
    h.radius = std::sqrt(maxD2);

    // Growth axis = the module's *central* direction: the mean of all
    // terminal positions relative to the root. Using a single terminal
    // (e.g. terminalNodes.front()) injects that arm's off-centre offset
    // into the axis, which for a radially-symmetric whorl is horizontal.
    // Rotating an off-centre axis by yaw ψ then couples ψ into
    // cos(axis, up), so the f_tropism objective gains a spurious preferred
    // ψ and the descent drives every module toward the *same* world yaw —
    // shearing the whole canopy off in one direction. Averaging the
    // terminals cancels the radial offsets (the whorl's mean is its
    // vertical trunk axis), restoring a ψ-independent tropism term.
    Vec3 axisLoc = {0.0f, 0.0f, 0.0f};
    if (!proto.terminalNodes.empty()) {
        for (uint32_t t : proto.terminalNodes) {
            if (t < proto.nodes.size()) axisLoc += proto.nodes[t].position - root;
        }
        axisLoc = axisLoc * (1.0f / static_cast<float>(proto.terminalNodes.size()));
    }
    // Degenerate fallback: a module whose terminals' mean coincides with
    // the root has no usable axis — keep the default up vector.
    if (vlen2(axisLoc) > 1e-12f) {
        h.axis = vnorm(rotateYawPitch(axisLoc, psi, theta));
    }
    return h;
}

float evalDistribution(const BranchModulePrototype& proto,
                       const WorldState& world,
                       const bromath::SpatialHash3D& index,
                       Vec3 attachWorld, float theta, float psi,
                       Vec3 growthTarget,
                       float w1, float w2,
                       std::vector<int32_t>& scratch) {
    OrientationHypothesis h = predictHypothesis(proto, attachWorld, theta, psi);
    scratch.clear();
    index.radiusQuery(h.centre, h.radius, scratch);
    float fc = 0.0f;
    for (int32_t nid : scratch) {
        uint32_t np, nmi;
        internal::unpackEntryId(nid, np, nmi);
        const auto& n = world.plants[np].modules[nmi];
        fc += sintersectVolume(Sphere{h.centre, h.radius},
                               Sphere{n.bboxCenter, n.bboxRadius});
    }
    // f_tropism: 0 when the module's growth axis points exactly along the
    // desired growth direction, rising to 2 when antiparallel.
    float ft = 1.0f - vdot(h.axis, growthTarget);
    return w1 * fc + w2 * ft;
}

// Coordinate descent over (θ, ψ): on each iteration try the four
// axis-aligned ±step neighbours, take the first improving move, otherwise
// halve the step. Six iterations with a starting step of 0.4 rad covers
// the full hemisphere down to ~6 mrad — enough resolution given the
// objective is dominated by a few coarse features (overlap with a
// neighbour vs. clear sky).
void settleOrientation(const BranchModulePrototype& proto,
                       const WorldState& world,
                       const bromath::SpatialHash3D& index,
                       Vec3 attachWorld,
                       Vec3 growthTarget, float w1, float w2,
                       float& theta, float& psi,
                       std::vector<int32_t>& scratch) {
    float bestObj = evalDistribution(proto, world, index, attachWorld, theta, psi,
                                     growthTarget, w1, w2, scratch);
    float step = 0.4f;
    for (int iter = 0; iter < 8 && step > 1e-3f; ++iter) {
        bool improved = false;
        const float dts[4] = { step, -step, 0.0f,  0.0f };
        const float dps[4] = { 0.0f,  0.0f, step, -step };
        for (int d = 0; d < 4; ++d) {
            float t = theta + dts[d];
            float p = psi   + dps[d];
            float o = evalDistribution(proto, world, index, attachWorld, t, p,
                                       growthTarget, w1, w2, scratch);
            if (o < bestObj - 1e-6f) {
                bestObj = o;
                theta = t;
                psi   = p;
                improved = true;
                break;
            }
        }
        if (!improved) step *= 0.5f;
    }
}

} // namespace

void spawnModules(Plant& plant,
                  WorldState& world,
                  bromath::SpatialHash3D& index,
                  uint64_t& rng) {
    auto& mods = plant.modules;
    if (mods.empty()) return;

    const auto& sp = plant.species;
    const float lambda = plant.flowering ? sp.apicalControlMature : sp.apicalControl;
    const float D      = plant.flowering ? sp.determinacyMature   : sp.determinacy;
    const float vmax   = sp.maxVigor > 0.0f ? sp.maxVigor : 1.0f;

    // Pack (parent index, terminal id) into a 64-bit key so the
    // already-attached check is O(1). Drops the spawn pass from O(N²) to
    // O(N · #terminals) which matters once a plant has more than a few
    // hundred modules.
    auto key = [](uint32_t pidx, uint32_t term) {
        return (static_cast<uint64_t>(pidx) << 32) | term;
    };
    std::unordered_set<uint64_t> occupied;
    occupied.reserve(mods.size() * 2);
    for (const auto& m : mods) {
        if (m.parent != UINT32_MAX) {
            occupied.insert(key(m.parent, m.parentAttachTerminal));
        }
    }

    // We don't need to snapshot neighbours — the per-tick spatial hash
    // built by world.cpp already indexes every module's bbox sphere.
    // We do insert each freshly-settled sibling back into the same hash
    // so subsequent siblings (and subsequent plants' spawn pass within
    // this tick) treat it as an existing neighbour.

    const Vec3  up = vnorm(sp.tropismDir * -1.0f);
    const float orthotropy = std::max(0.0f, std::min(1.0f, sp.orthotropy));
    const float w1        = sp.distributionWeightCollisions;
    const float w2        = sp.distributionWeightTropism;

    std::vector<BranchModuleInstance> spawns;
    const uint32_t modCount = static_cast<uint32_t>(mods.size());

    // Scratch buffer reused by every radiusQuery in settleOrientation.
    std::vector<int32_t> queryScratch;

    for (uint32_t i = 0; i < modCount; ++i) {
        auto& u = mods[i];
        if (!u.prototype) continue;
        if (u.age < sp.moduleMatureAge) continue;

        const auto& terms = u.prototype->terminalNodes;
        if (terms.empty()) continue;

        // Local Q at the parent — `light` is stamped by the light pass
        // and untouched by the basipetal accumulation (which writes to
        // `subtreeLight`). Paper §3.4: q(n_i) = Q(u) / #n.
        const float perTerminal = u.light / static_cast<float>(terms.size());
        if (perTerminal <= sp.minVigor) continue;

        const float dPrime = u.vigor * D / vmax;

        for (uint32_t k = 0; k < terms.size(); ++k) {
            uint32_t termNode = terms[k];
            if (occupied.count(key(i, termNode))) continue;

            const BranchModulePrototype* proto = internal::pickPrototype(world, dPrime, lambda);
            if (!proto) continue;

            // Where this child will attach in world space — parent's
            // worldPos plus the parent's grown, tropism-curved terminal
            // offset. nodeOffsetFromRoot folds in both the orientation
            // and the per-node tropism so the descent reasons about
            // where the terminal actually sits, not its rigid pose.
            Vec3 attachWorld = u.worldPos + nodeOffsetFromRoot(sp, u, termNode);

            // Growth target: continue the direction of the *twig* this
            // child sprouts from — the parent terminal's own incoming edge
            // (fork → tip), not the chord from the module root, which would
            // fold in the module's vertical trunk and pull every arm back
            // toward vertical. The twig direction carries the whorl's true
            // outward spread, so children inherit a distinct, genuinely
            // splayed heading per terminal; lifting it toward up by
            // `orthotropy` then sets how strongly the crown rises vs.
            // spreads. A child off an outward twig keeps growing outward —
            // building a crown instead of a vertical whip.
            uint32_t termParent = u.prototype->rootNode;
            for (const auto& e : u.prototype->edges) {
                uint32_t a = e.a, b = e.b;
                if (a > b) std::swap(a, b);
                if (b == termNode) { termParent = a; break; }
            }
            Vec3 armOff = nodeOffsetFromRoot(sp, u, termNode)
                        - nodeOffsetFromRoot(sp, u, termParent);
            Vec3 armDir = (vlen2(armOff) > 1e-8f) ? vnorm(armOff) : up;
            Vec3 growthTarget = vnorm(armDir + (up - armDir) * orthotropy);

            // Shoots are negatively gravitropic: they never *aim* below the
            // horizon. An outward arm on a steeply-tilted module can point
            // downward, and continuing it would drive the branch into the
            // ground. Clamp the target into the upper hemisphere (relative
            // to the species' up axis); the per-node gravitropic bend still
            // lets mature branches droop slightly, but new growth always
            // heads up-and-out. Falls back to straight up if the arm was
            // pointing straight down.
            const float upDot = vdot(growthTarget, up);
            if (upDot < 0.0f) {
                growthTarget = growthTarget - up * upDot;   // project onto horizon
                growthTarget = (vlen2(growthTarget) > 1e-6f) ? vnorm(growthTarget) : up;
            }

            // Seed (θ, ψ) directly from the target direction so the descent
            // starts aligned (the child prototype's mean axis is ~vertical,
            // so rotateYawPitch({0,1,0}, ψ, θ) = (sinψ sinθ, cosθ, cosψ sinθ)
            // inverts to θ = acos(target.y), ψ = atan2(target.x, target.z)).
            // A small rng jitter keeps re-seeds varied while a fixed seed
            // stays deterministic.
            const float jitter = 0.05f;
            const float gy = std::max(-1.0f, std::min(1.0f, growthTarget.y));
            float theta = std::acos(gy) + jitter * randSigned(rng);
            float psi   = std::atan2(growthTarget.x, growthTarget.z)
                          + jitter * randSigned(rng);
            settleOrientation(*proto, world, index, attachWorld,
                              growthTarget, w1, w2, theta, psi,
                              queryScratch);

            BranchModuleInstance child;
            child.prototype = proto;
            child.parent = i;
            child.parentAttachTerminal = termNode;
            child.age = 0.0f;
            child.vigor = perTerminal;
            child.light = perTerminal;
            // The prototype's first listed terminal grows the main
            // meristem (paper §3.2). Tagging at spawn time keeps the
            // λ-weighted vigor split independent of insertion order.
            child.isMainChild = (k == 0);
            child.orientation.theta = theta;
            child.orientation.psi   = psi;
            // worldPos / bbox will be filled in next development tick.
            spawns.push_back(child);
            occupied.insert(key(i, termNode));

            // Subsequent siblings cannot see this one for collision-aware
            // descent: inserting its sphere into the spatial hash here
            // would create an id pointing at a module that doesn't exist
            // yet (mods isn't appended until the end of this function),
            // and evalDistribution dereferences modules[id] to read bbox
            // geometry. The insertion used to live here but was a no-op
            // in practice (the dereferenced uninitialised bbox carried a
            // zero radius, so intersection volume was always zero), and
            // an out-of-range crash in Debug builds.
        }
    }

    if (!spawns.empty()) {
        mods.insert(mods.end(), spawns.begin(), spawns.end());
    }
}

} // namespace broflora
