#pragma once

// Tube walls: a plant's branch segments as the pieces of bark bro.scene's
// tube node draws (emitBranchTubes), with the radius and the bark's
// direction at each end and the vegetation record each piece sways by.

#include "broflora/sway.h"

#include <bromath/vec.h>
#include <bromesh/procedural/space_colonization.h>

#include <cstdint>
#include <vector>

namespace broflora {

/// A tube segment: from/to with the radius at each end and the bark's
/// direction there (the bisector at a joint with its continuation), the
/// layout emitBranchTubes writes.
struct TubeSegment {
    bromath::Vec3 from{}, to{};
    float rFrom = 0.0f, rTo = 0.0f;
    bromath::Vec3 tangentFrom{0.0f, 1.0f, 0.0f}, tangentTo{0.0f, 1.0f, 0.0f};
    int32_t segment = -1;          // its index in the segment list it was made from
    /// The share of `segment` it covers, from its from-end (a segment split
    /// at a root flare or a branch collar is several pieces).
    float along0 = 0.0f, along1 = 1.0f;
};

/// The wood's shape where it meets the ground and its forks (Species::
/// rootFlare and branchCollar), x the radius there.
struct TubeShape {
    float minRadius = 0.0f;
    float rootFlare = 0.0f;
    float branchCollar = 0.0f;
};

/// The tube walls of `segs`: a continuation starts at its parent's radius
/// (the bark runs on unbroken), a side shoot with a collar; joints between a
/// segment and its thickest child are mitred on the bisector. Degenerate
/// segments and those thinner than `minRadius` are left out.
///
/// With a `rootFlare`, each root segment (no parent) is split into pieces
/// that swell toward the ground, r (1 + flare e^(-h / r)) at height h up it;
/// with a `branchCollar`, a side shoot long enough is split two radii from
/// its base, the first piece swelling from (1 + collar) r at the fork to
/// (1 + collar / 4) r, so the collar is a short swell rather than a taper
/// along the whole segment. Pieces of one segment are adjacent and in order.
std::vector<TubeSegment> tubeSegments(const std::vector<bromesh::BranchSegment>& segs, float minRadius = 0.0f);
std::vector<TubeSegment> tubeSegments(const std::vector<bromesh::BranchSegment>& segs, const TubeShape& shape);

/// A tube piece's vegetation record: its segment's (`sway`, aligned with
/// the segments it was made from) at the piece's own ends.
void appendTubeVegetation(std::vector<float>& out, const TubeSegment& t, const std::vector<SegmentSway>& sway,
                          const std::vector<bromesh::BranchSegment>& segs);

} // namespace broflora
