#pragma once

// Crown clumps: where a plant's leaf mass sits, as distinct clumps hung at
// its twig ends with sky between them, rather than a uniform spray along
// every twig. The clumps are bromesh::LeafClump, so
// bromesh::placeLeavesInClumps fills them on the CPU and bro.scene's scatter
// node (clump mode) on the GPU, leaf for leaf the same.
//
// A plant's clumps are picked greedily from the tips of its leafy twigs
// (the twigs `foliageDensity` gives leaves to), outermost first, each kept
// only if it stands clear of the clumps already taken; so the crown's
// silhouette fills first and the interior takes what fits. A clump's size
// follows its plant's crown, its leaf density the twig's light and health.

#include <bromesh/procedural/leaf_clumps.h>

#include "broflora/mesh_emit.h"

#include <cstdint>
#include <vector>

namespace broflora {

struct ClumpOptions {
    /// Clump radius; 0 sizes clumps from each plant's crown (radiusOfCrown
    /// times its crown radius).
    float radius = 0.0f;
    float radiusOfCrown = 0.26f;
    /// +/- fraction of the radius per clump.
    float radiusJitter = 0.3f;
    /// Least distance between two clumps' centres, in their mean radius:
    /// below 2 they overlap, above it the sky shows between them.
    float spacing = 1.4f;
    /// Only twigs at most this thick, at least this deep, carry clumps (as
    /// bromesh::LeafPlacementOptions).
    float maxRadius = 0.05f;
    int minDepth = 1;
    /// The leaf density (`foliageDensity`) a twig needs to carry a clump.
    float minDensity = 0.08f;
    /// Optional per-segment leaf density in place of `foliageDensity`,
    /// indexed like the segments `segment` indexes (plant or world); a
    /// segment past its end has none.
    std::vector<float> densityWeight;
    uint64_t seed = 0;
};

/// A plant's clumps; `segment` indexes `emitPlantSegments(plant)`.
std::vector<bromesh::LeafClump> emitPlantClumps(const Plant& plant, const ClumpOptions& opts = {});

/// Every plant's clumps, concatenated in plant order; `segment` indexes
/// `emitWorldSegments(world)`.
std::vector<bromesh::LeafClump> emitWorldClumps(const WorldState& world, const ClumpOptions& opts = {});

/// The plants `plants` names (world indices, in that order) as though the
/// world held only them: their clumps concatenated, `segment` indexing their
/// segments concatenated in that order (as `densityWeight` does too).
std::vector<bromesh::LeafClump> emitPlantsClumps(const WorldState& world, const std::vector<uint32_t>& plants,
                                                 const ClumpOptions& opts = {});

/// Vegetation records (mesh_emit.h "Wind sway") for leaves placed in
/// `clumps` by bromesh::placeLeavesInClumps: one per leaf, the anchor at the
/// leaf, the axis its tip, the weight its twig tip's. `sway` is aligned with
/// the segments the clumps' `segment` indexes.
std::vector<float> clumpLeafVegetation(const bromesh::LeafPlacements& pl,
                                       const std::vector<bromesh::LeafClump>& clumps,
                                       const std::vector<SegmentSway>& sway);

/// The 16-float record bro.scene's scatter node takes per clump: the
/// clump's twig sway, its weight the twig tip's at both ends.
void appendClumpVegetation(std::vector<float>& out, const SegmentSway& s);

} // namespace broflora
