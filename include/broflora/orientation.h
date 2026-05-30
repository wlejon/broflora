#pragma once

// Broflora-specific orientation primitives.
//
// `Euler3` mirrors the paper's notation: roll φ, pitch θ, yaw ψ (radians).
// We keep this struct here rather than reaching for a generic Vec3 of
// angles because the field names line up with the paper's prose.
//
// `rotateYawPitch` applies pitch (θ around +X) first, then yaw (ψ around
// +Y). The order matters: an upright growth axis is first pitched off
// vertical by θ, then that tilt is swung around the vertical axis by ψ to
// choose its compass azimuth. Applying yaw first would leave it a no-op on
// a vertical axis (yawing +Y returns +Y), collapsing every branch's tilt
// onto a single world direction. Roll (φ) is intentionally omitted — it
// doesn't change a child's attachment point, only its local twist, which
// we don't model here.

#include "bromath/vec.h"

#include <cmath>

namespace broflora {

// Euler angles (paper notation): roll φ, pitch θ, yaw ψ. Radians.
struct Euler3 {
    float phi   = 0.0f;
    float theta = 0.0f;
    float psi   = 0.0f;
};

// Rotate `v` by pitch (around +X) then yaw (around +Y). Roll (φ) is
// intentionally ignored — see header note above. For an upright vector
// this gives rotateYawPitch({0,1,0}, ψ, θ) = (sinψ·sinθ, cosθ, cosψ·sinθ),
// i.e. ψ is the azimuth of the tilt and θ its magnitude off vertical.
inline bromath::Vec3 rotateYawPitch(bromath::Vec3 v, float yaw, float pitch) {
    const float cy = std::cos(yaw),   sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    // Pitch around +X first.
    const bromath::Vec3 p = { v.x, cp * v.y - sp * v.z, sp * v.y + cp * v.z };
    // Then yaw around +Y.
    return { cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z };
}

} // namespace broflora
