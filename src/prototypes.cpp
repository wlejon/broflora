#include "broflora/prototypes.h"

#include "bromath/scalar.h"
#include "bromath/vec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace broflora {

using bromath::Vec3;

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Cheap deterministic hash → [0,1) from a 32-bit key. Used to vary each
// edge's lateral bow direction so subdivided members don't all arch in the
// same plane — that planar uniformity is exactly the mechanical look the
// curvature is meant to break.
float hash01(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x) * (1.0f / 4294967296.0f);
}

// Subdivide every edge of a coarse prototype into `segs` sub-edges and bow
// the interior nodes off the straight chord, turning each rigid single-edge
// internode into a smoothly curved member. This is what stops branches from
// reading as stiff rods kinking at sharp joints: a member now sweeps along
// its length instead of being one straight cylinder.
//
// The chord endpoints are preserved exactly, so every terminal, the root,
// and all child-attach points are unchanged — only the middle of each
// internode is displaced, along an up-biased, per-edge-varied perpendicular:
//   • near-horizontal edges arch upward (a branch's self-supporting sweep);
//   • near-vertical edges (trunks) lean along a hashed azimuth so a stack of
//     them wanders instead of stacking into a dead-straight pole.
// Displacement is bow·L·sin(π·t): zero at both ends, peak at mid. Node
// ageAtBirth is interpolated along the chord so the subdivided edge still
// extends basipetally (base before tip) under the development growth model,
// and the per-node gravitropic bend the simulator adds on top now has
// intermediate nodes to act on — so mature members droop as well as arch.
BranchModulePrototype curveModule(const BranchModulePrototype& coarse,
                                  uint32_t segs, float bow) {
    if (segs < 2) return coarse;  // nothing to subdivide

    BranchModulePrototype out;
    out.name = coarse.name;

    const size_t nc = coarse.nodes.size();
    if (nc == 0) return coarse;
    std::vector<uint32_t> mapNew(nc, UINT32_MAX);  // coarse idx → new idx

    const uint32_t rootIdx = coarse.rootNode < nc ? coarse.rootNode : 0u;
    out.nodes.push_back(coarse.nodes[rootIdx]);
    mapNew[rootIdx] = 0;
    out.rootNode = 0;

    for (size_t ei = 0; ei < coarse.edges.size(); ++ei) {
        uint32_t a = coarse.edges[ei].a, b = coarse.edges[ei].b;
        if (a > b) std::swap(a, b);
        if (a >= nc || b >= nc) continue;

        // Parent side is already emitted (coarse edges are basipetally
        // ordered, a < b), but stay defensive if a stray edge slips through.
        uint32_t prevNew = mapNew[a];
        if (prevNew == UINT32_MAX) {
            out.nodes.push_back(coarse.nodes[a]);
            prevNew = static_cast<uint32_t>(out.nodes.size() - 1);
            mapNew[a] = prevNew;
        }

        const Vec3 A = coarse.nodes[a].position;
        const Vec3 B = coarse.nodes[b].position;
        const Vec3 e = B - A;
        const float L = bromath::vlen(e);

        // Bow direction: perpendicular to the edge, biased toward world up,
        // rolled off that plane by a per-edge angle so sibling arms curve
        // into distinct planes.
        Vec3 bowDir = {0.0f, 0.0f, 0.0f};
        if (L > 1e-5f) {
            const Vec3 dir = e * (1.0f / L);
            const Vec3 up  = {0.0f, 1.0f, 0.0f};
            Vec3 perp = up - dir * bromath::vdot(up, dir);
            float pl = bromath::vlen(perp);
            if (pl < 0.2f) {
                // Edge ~vertical: no meaningful "up" perpendicular — pick a
                // horizontal direction by hashed azimuth instead.
                const float az = hash01(static_cast<uint32_t>(ei) * 2654435761u)
                                 * bromath::TWO_PI;
                perp = {std::cos(az), 0.0f, std::sin(az)};
                perp = perp - dir * bromath::vdot(perp, dir);
                pl = bromath::vlen(perp);
            }
            if (pl > 1e-5f) {
                perp = perp * (1.0f / pl);
                const float roll =
                    (hash01(static_cast<uint32_t>(ei) * 40503u + 7u) - 0.5f) * 1.2f;
                // tangent = dir × perp (unit: dir ⟂ perp, both unit length).
                const Vec3 tan = {
                    dir.y * perp.z - dir.z * perp.y,
                    dir.z * perp.x - dir.x * perp.z,
                    dir.x * perp.y - dir.y * perp.x,
                };
                bowDir = perp * std::cos(roll) + tan * std::sin(roll);
            }
        }

        for (uint32_t k = 1; k <= segs; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(segs);
            uint32_t newIdx;
            if (k == segs) {
                // Endpoint: copy coarse b exactly (position + growth params),
                // so the terminal / attach point never moves.
                out.nodes.push_back(coarse.nodes[b]);
                newIdx = static_cast<uint32_t>(out.nodes.size() - 1);
                mapNew[b] = newIdx;
            } else {
                ModuleNode nd = coarse.nodes[b];  // inherit lengthMax/thickening
                const Vec3 base = A + e * t;
                nd.position = base + bowDir * (bow * L * std::sin(kPi * t));
                nd.ageAtBirth = coarse.nodes[a].ageAtBirth
                    + (coarse.nodes[b].ageAtBirth - coarse.nodes[a].ageAtBirth) * t;
                out.nodes.push_back(nd);
                newIdx = static_cast<uint32_t>(out.nodes.size() - 1);
            }
            out.edges.push_back({prevNew, newIdx});
            prevNew = newIdx;
        }
    }

    out.terminalNodes.reserve(coarse.terminalNodes.size());
    for (uint32_t t : coarse.terminalNodes) {
        if (t < nc && mapNew[t] != UINT32_MAX) out.terminalNodes.push_back(mapNew[t]);
    }
    return out;
}

} // namespace

BranchModulePrototype straightModule(const char* name) {
    BranchModulePrototype p;
    p.name = name;
    // {position, ageAtBirth, lengthMax, thickening}
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    p.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.2f, 1.0f, 1.0f});
    p.edges.push_back({0, 1});
    p.rootNode = 0;
    p.terminalNodes = {1};
    return p;
}

BranchModulePrototype forkModule(const char* name) {
    BranchModulePrototype p;
    p.name = name;
    p.nodes.push_back({{ 0.0f, 0.0f, 0.0f}, 0.0f,  1.0f, 1.0f});  // root
    p.nodes.push_back({{ 0.35f, 1.0f, 0.0f}, 0.25f, 1.0f, 1.0f}); // term A
    p.nodes.push_back({{-0.35f, 1.0f, 0.0f}, 0.25f, 1.0f, 1.0f}); // term B
    p.edges.push_back({0, 1});
    p.edges.push_back({0, 2});
    p.rootNode = 0;
    p.terminalNodes = {1, 2};
    // Subdivide + bow so the two arms sweep instead of running dead-straight
    // to their tips.
    return curveModule(p, 4, 0.18f);
}

BranchModulePrototype whorlModule(uint32_t arms, float spread, const char* name) {
    if (arms < 2) arms = 2;
    if (arms > 8) arms = 8;
    if (spread < 0.0f) spread = 0.0f;
    if (spread > 1.0f) spread = 1.0f;

    BranchModulePrototype p;
    p.name = name;

    // Node 0: root. Node 1: top of a short trunk the arms fan from. Keeping
    // a real trunk segment (rather than fanning straight off the root) gives
    // the pipe-model a thicker bole and reads as a stem, not a bare burst.
    const float trunkLen = 0.5f;
    const float rise     = 0.7f;          // how much arms climb above the fork
    const float radius   = 0.25f + spread * 0.6f;  // outward reach of arms

    p.nodes.push_back({{0.0f, 0.0f,      0.0f}, 0.0f, 1.0f, 1.0f});  // 0 root
    p.nodes.push_back({{0.0f, trunkLen,  0.0f}, 0.1f, 1.0f, 1.0f});  // 1 fork
    p.edges.push_back({0, 1});
    p.rootNode = 0;

    // Each arm is a single straight chord from the fork to an up-and-out tip;
    // curveModule below subdivides and bows every edge (trunk included), so
    // the arms arch and the trunk wanders rather than every member being a
    // rigid rod. The tip positions are preserved through curveModule, so the
    // crown silhouette and where child modules attach are unchanged.
    for (uint32_t i = 0; i < arms; ++i) {
        const float a = bromath::TWO_PI * static_cast<float>(i)
                        / static_cast<float>(arms);
        const float x = std::cos(a) * radius;
        const float z = std::sin(a) * radius;
        const uint32_t tipIdx = static_cast<uint32_t>(p.nodes.size());
        p.nodes.push_back({{x, trunkLen + rise, z}, 0.25f, 1.0f, 1.0f});
        p.edges.push_back({1, tipIdx});
        p.terminalNodes.push_back(tipIdx);
    }
    return curveModule(p, 5, 0.22f);
}

} // namespace broflora
