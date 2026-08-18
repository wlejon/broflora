#pragma once

// Internal geometry helpers shared across .cpp files. NOT part of the
// public API — kept out of include/broflora/ on purpose so consumers
// don't accidentally take a dependency on this surface.
//
// `rotateYawPitch` lives in `include/broflora/orientation.h` next to
// Euler3; this header forwards it into the internal namespace and adds
// the per-node tropism-aware offset used by development, spawning, and
// mesh emission.

#include "broflora/orientation.h"  // rotateYawPitch (broflora yaw-then-pitch)
#include "broflora/plant.h"        // Species, BranchModuleInstance
#include "bromath/sphere.h"        // sintersectVolume
#include "bromath/vec.h"

#include <algorithm>
#include <cmath>

namespace broflora::internal {

using broflora::rotateYawPitch;

// Offset of `nodeIdx` from the module's local-frame origin, in world
// orientation, with per-node biomechanical tropism applied.
//
// Biomechanics:
//   - Young shoot tips (small a_b) exhibit upward light-seeking phototropism
//     curling upward toward -tropismDir (+Y):
//       τ_photo(a_b, L) = sp.tropismG2 * (sp.tropismG1 / (a_b + sp.tropismG1)) * spanScale
//   - Older, mature branches accumulate cantilever gravity sag along tropismDir
//     proportional to mature age and reach:
//       τ_sag(a_b, L) = sp.tropismG2 * (a_b / (a_b + sp.tropismG1)) * spanScale
//   - Net per-node tropism offset along tropismDir (g_norm):
//       τ(a_b, L) = g_norm * (τ_sag - τ_photo)
//                 = g_norm * [sp.tropismG2 * ((a_b - sp.tropismG1) / (a_b + sp.tropismG1)) * spanScale]
//   - spanScale is the reach (distance from root node, vlen(rel)), guaranteeing
//     the root node (spanScale = 0) identically maps to zero net drift so modules
//     remain attached at their exact parent terminals.
//   - Combined across module hierarchies, this yields the characteristic botanical
//     S-curve: drooping heavy main boughs with upward-turned apical shoot tips.
inline bromath::Vec3 nodeOffsetFromRoot(const broflora::Species& sp,
                                       const broflora::BranchModuleInstance& m,
                                       uint32_t nodeIdx) {
    if (!m.prototype) return {0.0f, 0.0f, 0.0f};
    const auto& proto = *m.prototype;
    if (nodeIdx >= proto.nodes.size()) return {0.0f, 0.0f, 0.0f};
    const uint32_t rootIdx = proto.rootNode < proto.nodes.size() ? proto.rootNode : 0u;
    if (nodeIdx == rootIdx) return {0.0f, 0.0f, 0.0f};

    bromath::Vec3 local = (nodeIdx < m.nodePositions.size())
        ? m.nodePositions[nodeIdx] : proto.nodes[nodeIdx].position;
    bromath::Vec3 rootLocal = (rootIdx < m.nodePositions.size())
        ? m.nodePositions[rootIdx] : proto.nodes[rootIdx].position;
    bromath::Vec3 rel = local - rootLocal;
    bromath::Vec3 rotated = rotateYawPitch(rel, m.orientation.psi, m.orientation.theta);

    const float dirLen = bromath::vlen(sp.tropismDir);
    if (dirLen <= 1e-6f) return rotated;
    const bromath::Vec3 gNorm = sp.tropismDir * (1.0f / dirLen);

    // Cantilever moment arm: lateral/transverse reach perpendicular to gravity.
    // Vertical trunks (parallel to gravity) experience zero cantilever bending,
    // while spreading lateral branches accumulate realistic sag and phototropic lift.
    const float parallelProj = bromath::vdot(rotated, gNorm);
    const bromath::Vec3 perpRel = rotated - gNorm * parallelProj;
    const float spanScale = bromath::vlen(perpRel);
    if (spanScale <= 1e-6f) return rotated;

    const float ab = std::max(0.0f, m.age - proto.nodes[nodeIdx].ageAtBirth);
    const float denom = ab + sp.tropismG1;
    if (denom <= 1e-6f) return rotated;

    // Biomechanical tropism:
    // τ_sag   = sp.tropismG2 * (ab / denom) * spanScale along gNorm (+tropismDir)
    // τ_photo = sp.tropismG2 * (sp.tropismG1 / denom) * spanScale along -gNorm (-tropismDir)
    // Net displacement along gNorm:
    const float k = sp.tropismG2 * ((ab - sp.tropismG1) / denom) * spanScale;
    return rotated + gNorm * k;
}

} // namespace broflora::internal
