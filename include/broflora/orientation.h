#pragma once

// Broflora-specific orientation primitives.
//
// `Euler3` mirrors the paper's notation: roll φ, pitch θ, yaw ψ (radians).
// We keep this struct here rather than reaching for a generic Vec3 of
// angles because the field names line up with the paper's prose.
//
// `rotateYawPitch` applies the broflora yaw-then-pitch convention used
// throughout module placement (yaw around +Y, pitch around +X). Roll (φ)
// is intentionally omitted — it doesn't change a child's attachment
// point, only its local twist, which we don't model here.

#include "bromath/vec.h"

#include <cmath>

namespace broflora {

// Euler angles (paper notation): roll φ, pitch θ, yaw ψ. Radians.
struct Euler3 {
    float phi   = 0.0f;
    float theta = 0.0f;
    float psi   = 0.0f;
};

// Rotate `v` by yaw (around +Y) then pitch (around +X). Roll (φ) is
// intentionally ignored — see header note above.
inline bromath::Vec3 rotateYawPitch(bromath::Vec3 v, float yaw, float pitch) {
    const float cy = std::cos(yaw),   sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const bromath::Vec3 r1 = { cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z };
    return { r1.x, cp * r1.y - sp * r1.z, sp * r1.y + cp * r1.z };
}

} // namespace broflora
