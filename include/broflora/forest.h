#pragma once

// Forest emit: everything bro.scene's forest node needs to draw a world's
// plants at any distance, from standing among the trunks to the horizon,
// with every level of detail posed by the same sway (sway.h).
//
// Each plant becomes a SPECIMEN: its bark segments, its crown clumps and its
// leaves, in coordinates relative to where it stands, so one grown plant can
// be drawn many times (an instance is a specimen at a position, a turn about
// the vertical and a scale). The renderer picks per instance and per frame
// how much of a specimen to draw, and the order here is what makes that a
// prefix:
//
//   - segments are sorted thickest first, by the thickest tip at or beyond
//     each (its keep radius), so "every segment that leads to bark at least
//     r thick" is the first n of them and a prefix stays connected;
//   - leaves are in a shuffled order, so the first n of a specimen's leaves
//     are an even thinning of every clump (the renderer widens the ones it
//     keeps, holding the crown's cover while the work falls);
//   - clumps are the crown's masses (clumps.h), the same ones the leaves
//     fill, so a far tier drawn as clumps keeps the near tier's silhouette
//     and colour.
//
// The leaves are exactly the world's clump-mode scatter: leaf k of a
// specimen carries its clump's index (global, as emitWorldClumps numbers
// them) and the index it has in placeLeavesInClumps' order over the world,
// which keys its placement hash. So a forest drawn at full detail draws the
// very leaves emitScatterSegments' scatter node draws.

#include "broflora/clumps.h"
#include "broflora/mesh_emit.h"

#include <bromesh/procedural/leaf_clumps.h>

#include <cstdint>
#include <vector>

namespace broflora {

struct ForestOptions {
    /// Clump picking, as the foliage emits.
    ClumpOptions clumps;
    /// Leaf density and placement within the clumps.
    bromesh::LeafClumpOptions leaves;
    /// Segments thinner than this at their tip are left out of the bark.
    float minRadius = 0.0f;
    /// Seeds the leaves' shuffle.
    uint64_t seed = 0x5eed;
};

/// One plant's share of the arrays below. Counts are of segments, clumps and
/// leaves; starts index them.
struct ForestSpecimen {
    uint32_t plant = 0;            // its index in the world
    bromath::Vec3 origin{};        // where it stands; its records are relative to this
    uint32_t segStart = 0, segCount = 0;
    uint32_t clumpStart = 0, clumpCount = 0;
    uint32_t leafStart = 0, leafCount = 0;
    float height = 0.0f;           // above its origin
    bromath::Vec3 crown{};         // crown centre, relative
    float crownRadius = 0.0f;
};

/// A tube segment: from/to with the radius at each end and the bark's
/// direction there (the bisector at a joint with its continuation), the
/// layout emitBranchTubes writes.
struct TubeSegment {
    bromath::Vec3 from{}, to{};
    float rFrom = 0.0f, rTo = 0.0f;
    bromath::Vec3 tangentFrom{0.0f, 1.0f, 0.0f}, tangentTo{0.0f, 1.0f, 0.0f};
    int32_t segment = -1;          // its index in the segment list it was made from
};

/// The tube walls of `segs`: a continuation starts at its parent's radius
/// (the bark runs on unbroken), a side shoot with a small collar; joints
/// between a segment and its thickest child are mitred on the bisector.
/// Degenerate segments and those thinner than `minRadius` are left out.
std::vector<TubeSegment> tubeSegments(const std::vector<bromesh::BranchSegment>& segs, float minRadius = 0.0f);

struct ForestEmit {
    std::vector<ForestSpecimen> specimens;
    /// Per segment: from.xyz, rFrom, to.xyz, rTo (8 floats).
    std::vector<float> segments;
    /// Per segment: the bark's direction at from, at to (6 floats).
    std::vector<float> tangents;
    /// Per segment: its 16-float vegetation record (appendSegmentVegetation).
    std::vector<float> segmentVegetation;
    /// Per segment: its keep radius, the thickest tip at or beyond it. The
    /// order's key: it never grows down a specimen's list, so "every segment
    /// whose keep radius is at least r" is a connected prefix.
    std::vector<float> keepRadius;
    /// Per clump: centre.xyz, radius, twig dir.xyz, leaf count (8 floats).
    std::vector<float> clumps;
    /// Per clump: its 16-float vegetation record (appendClumpVegetation).
    std::vector<float> clumpVegetation;
    /// Per leaf: its clump (global index) and its placement key (2 values).
    std::vector<uint32_t> leaves;
    /// The leaf placement every clump is filled by.
    bromesh::LeafClumpOptions leafOptions;
};

/// Every plant of the world as a specimen, in plant order. Plants with no
/// segments are left out.
ForestEmit emitForest(const WorldState& world, const ForestOptions& opts = {});

} // namespace broflora
