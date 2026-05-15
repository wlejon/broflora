#include "broflora/spawning.h"

#include "broflora/vec_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace broflora {

// Nearest Voronoi site in (D, λ) parameter space. Returns nullptr if the
// world has no prototypes registered. Exposed (non-anonymous) so seeding
// in senescence.cpp can reuse the same juvenile-(D, λ) lookup.
const BranchModulePrototype* pickPrototype(const WorldState& world,
                                           float dPrime, float lambda) {
    const BranchModulePrototype* best = nullptr;
    float bestD2 = std::numeric_limits<float>::infinity();
    for (const auto& site : world.voronoi) {
        if (!site.prototype) continue;
        float dd = site.determinacy - dPrime;
        float dl = site.apicalControl - lambda;
        float d2 = dd * dd + dl * dl;
        if (d2 < bestD2) { bestD2 = d2; best = site.prototype; }
    }
    return best;
}

namespace {

// Same yaw+pitch rotation as development.cpp uses — duplicated here so
// we don't need a header dep for one tiny helper. Roll (φ) is ignored
// for branch placement.
Vec3 rotateYP(Vec3 v, float yaw, float pitch) {
    float cy = std::cos(yaw),   sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);
    Vec3 r1 = { cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z };
    return { r1.x, cp * r1.y - sp * r1.z, sp * r1.y + cp * r1.z };
}

// Closed-form intersection volume of two spheres — same formula as
// light.cpp's helper; isolated here to keep the orientation evaluator
// self-contained.
float sphereIntersect(Vec3 c1, float r1, Vec3 c2, float r2) {
    if (r1 <= 0.0f || r2 <= 0.0f) return 0.0f;
    float d = v3_len(v3_sub(c2, c1));
    if (d >= r1 + r2) return 0.0f;
    if (d + std::min(r1, r2) <= std::max(r1, r2)) {
        float rs = std::min(r1, r2);
        return (4.0f / 3.0f) * 3.14159265358979f * rs * rs * rs;
    }
    float s = r1 + r2, df = r1 - r2;
    return 3.14159265358979f * (s - d) * (s - d) *
           (d * d + 2.0f * d * s - 3.0f * df * df) / (12.0f * std::max(d, 1e-6f));
}

struct NeighbourSphere { Vec3 c; float r; };

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
        Vec3 loc = v3_sub(nd.position, root);
        Vec3 r   = rotateYP(loc, psi, theta);
        rotated.push_back(r);
        sum = v3_add(sum, r);
    }
    Vec3 mean = v3_scale(sum, 1.0f / static_cast<float>(rotated.size()));
    float maxD2 = 0.0f;
    for (const auto& r : rotated) {
        maxD2 = std::max(maxD2, v3_len2(v3_sub(r, mean)));
    }
    h.centre = v3_add(attachWorld, mean);
    h.radius = std::sqrt(maxD2);

    uint32_t term = proto.terminalNodes.empty() ? proto.rootNode : proto.terminalNodes.front();
    if (term < proto.nodes.size()) {
        Vec3 axisLoc = v3_sub(proto.nodes[term].position, root);
        h.axis = v3_normalize(rotateYP(axisLoc, psi, theta));
    }
    return h;
}

float evalDistribution(const BranchModulePrototype& proto,
                       Vec3 attachWorld, float theta, float psi,
                       const std::vector<NeighbourSphere>& neighbours,
                       Vec3 tropismUp, float cosTarget,
                       float w1, float w2) {
    OrientationHypothesis h = predictHypothesis(proto, attachWorld, theta, psi);
    float fc = 0.0f;
    for (const auto& nb : neighbours) {
        fc += sphereIntersect(h.centre, h.radius, nb.c, nb.r);
    }
    float cosU = v3_dot(h.axis, tropismUp);
    float ft   = std::fabs(cosTarget - cosU);
    return w1 * fc + w2 * ft;
}

// Coordinate descent over (θ, ψ): on each iteration try the four
// axis-aligned ±step neighbours, take the first improving move, otherwise
// halve the step. Six iterations with a starting step of 0.4 rad covers
// the full hemisphere down to ~6 mrad — enough resolution given the
// objective is dominated by a few coarse features (overlap with a
// neighbour vs. clear sky).
void settleOrientation(const BranchModulePrototype& proto,
                       Vec3 attachWorld,
                       const std::vector<NeighbourSphere>& neighbours,
                       Vec3 tropismUp, float cosTarget, float w1, float w2,
                       float& theta, float& psi) {
    float bestObj = evalDistribution(proto, attachWorld, theta, psi,
                                     neighbours, tropismUp, cosTarget, w1, w2);
    float step = 0.4f;
    for (int iter = 0; iter < 8 && step > 1e-3f; ++iter) {
        bool improved = false;
        const float dts[4] = { step, -step, 0.0f,  0.0f };
        const float dps[4] = { 0.0f,  0.0f, step, -step };
        for (int d = 0; d < 4; ++d) {
            float t = theta + dts[d];
            float p = psi   + dps[d];
            float o = evalDistribution(proto, attachWorld, t, p,
                                       neighbours, tropismUp, cosTarget, w1, w2);
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

void spawnModules(Plant& plant, WorldState& world, uint64_t& rng) {
    (void)rng;  // rng currently unused; kept in signature for caller symmetry
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

    // Snapshot every existing module's bounding sphere across the whole
    // world as collision targets for the gradient descent. Mutated below
    // as each new sibling settles so subsequent siblings see it.
    std::vector<NeighbourSphere> neighbours;
    for (const auto& pl : world.plants) {
        for (const auto& m : pl.modules) {
            if (m.bboxRadius > 0.0f) {
                neighbours.push_back({m.bboxCenter, m.bboxRadius});
            }
        }
    }

    const Vec3  tropismUp = v3_normalize(v3_scale(sp.tropismDir, -1.0f));
    const float cosTarget = sp.tropismCosTarget;
    const float w1        = sp.distributionWeightCollisions;
    const float w2        = sp.distributionWeightTropism;

    std::vector<BranchModuleInstance> spawns;
    const uint32_t modCount = static_cast<uint32_t>(mods.size());

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

            const BranchModulePrototype* proto = pickPrototype(world, dPrime, lambda);
            if (!proto) continue;

            // Where this child will attach in world space — parent's
            // worldPos plus the parent's grown terminal-node offset.
            Vec3 attachWorld = u.worldPos;
            if (u.prototype && termNode < u.prototype->nodes.size()) {
                Vec3 localTerm = (termNode < u.nodePositions.size())
                    ? u.nodePositions[termNode]
                    : u.prototype->nodes[termNode].position;
                Vec3 rot = rotateYP(localTerm, u.orientation.psi, u.orientation.theta);
                attachWorld = v3_add(u.worldPos, rot);
            }

            // Seed orientation: fanned yaw per terminal slot for sibling
            // separation, mild outward pitch. Settled below by coord
            // descent on f_distribution.
            float theta = 0.15f;
            float psi   = (terms.size() > 0)
                ? (2.0f * 3.14159265358979f) * static_cast<float>(k)
                  / static_cast<float>(terms.size())
                : 0.0f;
            settleOrientation(*proto, attachWorld, neighbours,
                              tropismUp, cosTarget, w1, w2, theta, psi);

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

            // Make this child visible to subsequent siblings' descent so
            // they don't all converge onto the same favoured pose.
            OrientationHypothesis h = predictHypothesis(*proto, attachWorld, theta, psi);
            if (h.radius > 0.0f) {
                neighbours.push_back({h.centre, h.radius});
            }
        }
    }

    if (!spawns.empty()) {
        mods.insert(mods.end(), spawns.begin(), spawns.end());
    }
}

} // namespace broflora
