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
// orientation, with per-node tropism applied (paper §3.3). Tropism
// τ(a_b) = g1·g2 / (a_b + g1) along normalized species.tropismDir,
// where a_b = max(0, m.age − node.ageAtBirth). Applied per-node — not
// uniformly to the module — so segments born later have larger a_b
// (smaller offset) and the branch curves under gravity instead of
// translating rigidly. The module's own worldPos is the un-tropism-
// shifted attach point handed down from the parent.
inline bromath::Vec3 nodeOffsetFromRoot(const broflora::Species& sp,
                                       const broflora::BranchModuleInstance& m,
                                       uint32_t nodeIdx) {
    if (!m.prototype) return {0.0f, 0.0f, 0.0f};
    const auto& proto = *m.prototype;
    if (nodeIdx >= proto.nodes.size()) return {0.0f, 0.0f, 0.0f};
    const uint32_t rootIdx = proto.rootNode < proto.nodes.size() ? proto.rootNode : 0u;
    bromath::Vec3 local = (nodeIdx < m.nodePositions.size())
        ? m.nodePositions[nodeIdx] : proto.nodes[nodeIdx].position;
    bromath::Vec3 rootLocal = (rootIdx < m.nodePositions.size())
        ? m.nodePositions[rootIdx] : proto.nodes[rootIdx].position;
    bromath::Vec3 rel = local - rootLocal;
    bromath::Vec3 rotated = rotateYawPitch(rel, m.orientation.psi, m.orientation.theta);
    const float nodeAb = proto.nodes[nodeIdx].ageAtBirth;
    const float ab    = std::max(0.0f, m.age - nodeAb);
    const float denom = ab + sp.tropismG1;
    if (denom <= 1e-6f) return rotated;
    const float k = sp.tropismG1 * sp.tropismG2 / denom;
    if (k == 0.0f) return rotated;
    return rotated + bromath::vnorm(sp.tropismDir) * k;
}

} // namespace broflora::internal
