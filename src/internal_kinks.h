#pragma once

// The emitted plant's node positions, shared by the segment emit
// (mesh_emit.cpp) and the foliage and bloom emits (foliage_emit.cpp), so
// every emit stands on the same kinked skeleton. Internal, like
// internal_geom.h.

#include "broflora/plant.h"
#include "bromath/vec.h"
#include "internal_geom.h"

#include <cstdint>
#include <vector>

namespace broflora::internal {

// World-space position of a module-local prototype-node index, including
// the module's yaw+pitch rotation, the per-node tropism curvature, and
// the module's worldPos. Routes through the same internal helper that
// development.cpp uses to build bboxes and that spawning.cpp uses to
// pick attach points, so the mesh matches the simulator's geometry
// exactly.
inline bromath::Vec3 worldNodePos(const Species& sp, const BranchModuleInstance& m, uint32_t nodeIdx) {
    if (!m.prototype || nodeIdx >= m.prototype->nodes.size()) return m.worldPos;
    return m.worldPos + nodeOffsetFromRoot(sp, m, nodeIdx);
}

// The kinks (Species::kink): per module, per prototype node, how far the
// emitted node sits from the grown one (mesh_emit.cpp).
std::vector<std::vector<bromath::Vec3>> kinkOffsets(const Plant& plant);

}  // namespace broflora::internal
