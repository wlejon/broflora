#pragma once

// The flat records of a forest emit (forest.h), field by field: the one
// definition emitForest writes them by, its JS binding packs the specimens
// by (world.emitForest: `specimens`, `specimenLooks`, `specimenLeaves`,
// `specimenBlooms`, `instances`), and bro.scene's forest node reads them
// by. Each field is its first float's offset in its record (a vec3 takes
// three from there); kFloats is the record's stride. A count or an index
// is held as a float (exact below 2^24), except the leaves', which stay
// integers (a world's placement keys pass 2^24).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace broflora {

struct ForestEmit;

namespace forest_records {

/// `specimens`: one per specimen. Its runs of segments, clumps and leaves
/// (ForestSpecimen's starts and counts), its height and crown (relative to
/// where it stands), its plant's index in the world and where that stands,
/// and its clumps' height over their width (Species::clumpFlat).
namespace specimen {
inline constexpr size_t kSegStart = 0, kSegCount = 1;
inline constexpr size_t kClumpStart = 2, kClumpCount = 3;
inline constexpr size_t kLeafStart = 4, kLeafCount = 5;
inline constexpr size_t kHeight = 6;
inline constexpr size_t kCrownRadius = 7;
inline constexpr size_t kCrown = 8;        // xyz
inline constexpr size_t kPlant = 11;
inline constexpr size_t kOrigin = 12;      // xyz
inline constexpr size_t kClumpFlat = 15;
inline constexpr size_t kFloats = 16;
}  // namespace specimen

/// `specimenLooks`: one per specimen, its species' look (colours 0,0,0
/// where the renderer's own are meant).
namespace look {
inline constexpr size_t kLeafColor = 0;    // rgb
inline constexpr size_t kLeafForm = 3;     // Species::leafForm
inline constexpr size_t kBarkColor = 4;    // rgb
inline constexpr size_t kLeafSize = 7;     // Species::leafSize
inline constexpr size_t kFloats = 8;
}  // namespace look

/// `specimenLeaves`: one per specimen, its broad leaf and its sprays
/// (Species::leafWidth, leafTip, leafTeeth, leafToothDepth, leafGloss,
/// sprayBrush, petiole, phyllotaxis).
namespace leaf_look {
inline constexpr size_t kWidth = 0;
inline constexpr size_t kTip = 1;
inline constexpr size_t kTeeth = 2;
inline constexpr size_t kToothDepth = 3;
inline constexpr size_t kGloss = 4;
inline constexpr size_t kSprayBrush = 5;
inline constexpr size_t kPetiole = 6;
inline constexpr size_t kPhyllotaxis = 7;
inline constexpr size_t kFloats = 8;
}  // namespace leaf_look

/// `specimenBlooms`: one per specimen, its run of `blooms` and what they are
/// (Species::bloomForm: 0 none, 1 flower, 2 head, 3 spike; bloomSize,
/// bloomColor); the last float is 0.
namespace specimen_bloom {
inline constexpr size_t kStart = 0, kCount = 1;
inline constexpr size_t kForm = 2;
inline constexpr size_t kSize = 3;
inline constexpr size_t kColor = 4;        // rgb
inline constexpr size_t kFloats = 8;
}  // namespace specimen_bloom

/// `instances`: a specimen drawn at a position, a scale and a turn about the
/// vertical (radians); the last two floats are 0.
namespace instance {
inline constexpr size_t kPosition = 0;     // xyz
inline constexpr size_t kScale = 3;
inline constexpr size_t kYaw = 4;
inline constexpr size_t kSpecimen = 5;
inline constexpr size_t kFloats = 8;
}  // namespace instance

/// ForestEmit::segments: a tube piece's ends and the radius at each.
namespace segment {
inline constexpr size_t kFrom = 0;         // xyz
inline constexpr size_t kRFrom = 3;
inline constexpr size_t kTo = 4;           // xyz
inline constexpr size_t kRTo = 7;
inline constexpr size_t kFloats = 8;
}  // namespace segment

/// ForestEmit::segmentClumps: the clumps a segment leads to, its own and
/// every one beyond it, when they are at most kMax: each an index into its
/// specimen's clumps, -1 past the last. All -1: none, or more than kMax (a
/// renderer dropping clumps keeps that wood whatever they do). Listed, not a
/// run: a subtree's clumps are not always neighbours in clump order.
namespace segment_clumps {
inline constexpr size_t kClumps = 0;       // kMax of them
inline constexpr size_t kMax = 4;
inline constexpr size_t kFloats = 4;
}  // namespace segment_clumps

/// ForestEmit::tangents: the bark's direction at a segment's ends.
namespace tangent {
inline constexpr size_t kFrom = 0;         // xyz
inline constexpr size_t kTo = 3;           // xyz
inline constexpr size_t kFloats = 6;
}  // namespace tangent

/// ForestEmit::segmentVegetation and clumpVegetation: the 16-float record
/// appendSegmentVegetation writes (sway.h: kSegmentVegetationFloats). Its
/// root, its height above it, its crown and crown radius, the branch weight
/// at its ends, the path from the root to its from-end, the plant's bark
/// word (packBarkWord), then a zero the renderer puts each leaf's axis in
/// and the twig, limb and trunk words (packSwayTwig, ...).
namespace vegetation {
inline constexpr size_t kRoot = 0;         // xyz
inline constexpr size_t kHeight = 3;
inline constexpr size_t kCrown = 4;        // xyz
inline constexpr size_t kCrownRadius = 7;
inline constexpr size_t kWeightFrom = 8;
inline constexpr size_t kWeightTo = 9;
inline constexpr size_t kPath = 10;
inline constexpr size_t kBark = 11;
inline constexpr size_t kAxis = 12;
inline constexpr size_t kTwig = 13;
inline constexpr size_t kLimb = 14;
inline constexpr size_t kTrunk = 15;
inline constexpr size_t kFloats = 16;
}  // namespace vegetation

/// ForestEmit::clumps: a crown mass, its twig's outward direction and the
/// leaves it holds.
namespace clump {
inline constexpr size_t kCentre = 0;       // xyz
inline constexpr size_t kRadius = 3;
inline constexpr size_t kTwig = 4;         // xyz
inline constexpr size_t kLeafCount = 7;
inline constexpr size_t kFloats = 8;
}  // namespace clump

/// ForestEmit::leaves (uint32): a leaf's clump (global index) and its
/// placement key.
namespace leaf {
inline constexpr size_t kClump = 0;
inline constexpr size_t kKey = 1;
inline constexpr size_t kValues = 2;
}  // namespace leaf

/// ForestEmit::blooms: where a bloom sits (its twig's tip), its size, its
/// twig's outward direction, and its segment (an index into its specimen's
/// segments).
namespace bloom {
inline constexpr size_t kPosition = 0;     // xyz
inline constexpr size_t kSize = 3;
inline constexpr size_t kDir = 4;          // xyz
inline constexpr size_t kSegment = 7;
inline constexpr size_t kFloats = 8;
}  // namespace bloom

/// A new record of `floats` at the end of `v`, zeroed.
inline float* append(std::vector<float>& v, size_t floats) {
    v.resize(v.size() + floats, 0.0f);
    return v.data() + v.size() - floats;
}

/// The per-specimen arrays world.emitForest hands bro.scene, each as laid
/// out above: `specimens`, `looks`, `leafLooks`, `blooms` (specimenBlooms)
/// and `instances` (one per specimen, where its plant grew, unturned at
/// scale 1).
struct Packed {
    std::vector<float> specimens, looks, leafLooks, blooms, instances;
};
Packed pack(const ForestEmit& f);

}  // namespace forest_records
}  // namespace broflora
