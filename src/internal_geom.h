#pragma once

// Internal geometry helpers shared across .cpp files. NOT part of the
// public API — kept out of include/broflora/ on purpose so consumers
// don't accidentally take a dependency on this surface.

#include "broflora/vec_math.h"

#include <algorithm>
#include <cmath>

namespace broflora::internal {

// Closed-form volume of intersection of two spheres. Returns 0 for
// disjoint, the smaller sphere's volume for full containment.
inline float sphereIntersectVolume(Vec3 c1, float r1, Vec3 c2, float r2) {
    if (r1 <= 0.0f || r2 <= 0.0f) return 0.0f;
    const float d = v3_len(v3_sub(c2, c1));
    if (d >= r1 + r2) return 0.0f;
    if (d + std::min(r1, r2) <= std::max(r1, r2)) {
        const float rs = std::min(r1, r2);
        return (4.0f / 3.0f) * 3.14159265358979f * rs * rs * rs;
    }
    const float sum = r1 + r2, diff = r1 - r2;
    const float term1 = (sum - d) * (sum - d);
    const float term2 = d * d + 2.0f * d * sum - 3.0f * diff * diff;
    return 3.14159265358979f * term1 * term2 / (12.0f * std::max(d, 1e-6f));
}

// Rotate `v` by yaw (around +Y) then pitch (around +X). Roll (φ) is
// intentionally ignored — it doesn't change a child's attachment point.
inline Vec3 rotateYawPitch(Vec3 v, float yaw, float pitch) {
    const float cy = std::cos(yaw),   sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const Vec3 r1 = { cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z };
    return { r1.x, cp * r1.y - sp * r1.z, sp * r1.y + cp * r1.z };
}

} // namespace broflora::internal
