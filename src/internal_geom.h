#pragma once

// Internal geometry helpers shared across .cpp files. NOT part of the
// public API — kept out of include/broflora/ on purpose so consumers
// don't accidentally take a dependency on this surface.
//
// Most of what lived here moved into bromath; `rotateYawPitch` is the
// only broflora-specific transform left (yaw-then-pitch, intentional
// roll omission), and it now lives next to the paper-specific Euler3
// notation in `include/broflora/orientation.h`.

#include "broflora/orientation.h"  // rotateYawPitch (broflora yaw-then-pitch)
#include "bromath/sphere.h"        // sintersectVolume
#include "bromath/vec.h"

namespace broflora::internal {

using broflora::rotateYawPitch;

} // namespace broflora::internal
