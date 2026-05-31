#pragma once

// Mesh-emit boundary — the only place the simulation core meets the
// geometry side. broflora emits straight into `bromesh::MeshData` so
// output drops into the rest of the bromesh pipeline (UV/colors/tangents
// downstream, leaf scatter, foliage anchors) without copying.

#include <bromesh/mesh_data.h>
#include <bromesh/procedural/space_colonization.h>

#include "broflora/plant.h"
#include "broflora/world.h"

#include <vector>

namespace broflora {

using MeshData = bromesh::MeshData;

// Emit a faceted cylinder-strip mesh for a single plant — one tapered
// cylinder per branch segment within each module, sided by `sides`.
// Per-edge radius is interpolated by node depth from the prototype's
// root, anchored at the module's pipe-model `diameter` (stem) and the
// species' `leafDiameter` (tip). Segment endpoints come from each
// module's grown `nodePositions` cache, so branches that haven't
// reached their target length emit shorter cylinders. Returns an
// empty mesh when the plant has no modules.
//
// Known limitations (intentional; revisit when the rendering side of
// `bro` is in place):
//   - No vertex sharing across module boundaries — parent's tip ring
//     and child's root ring are independent vertex sets even when
//     coincident, and the two rings use different radii (leaf tip vs.
//     child stem) so a small geometric step is visible at junctions.
//   - No end caps on cylinders — branch tips are open ended on the
//     assumption that foliage geometry will cover them.
//   - Normals are radial-only; the cone half-angle from tapering is
//     ignored. The lighting error is small at typical taper ratios.
//   - The Euler roll φ on `BranchModuleInstance::orientation.phi` is
//     applied as a phase offset to the ring vertices of every cylinder
//     in the module. The cylinder body is rotationally symmetric so the
//     surface looks identical for any φ, but the per-vertex tangent
//     frame (and therefore any UV/texture seam, or leaf attachment
//     direction a downstream renderer derives from these vertices) is a
//     deterministic function of φ. Branch placement itself still stays
//     in (θ, ψ) — see orientation.h.
MeshData emitPlantMesh(const Plant& plant, uint32_t sides = 6);

// Same, but appends every plant in the world into a single mesh.
MeshData emitWorldMesh(const WorldState& world, uint32_t sides = 6);

// Per-segment foliage state, in lockstep order with `emitPlantSegments`.
// One sample per emitted segment carries the simulation state a foliage
// scatter (or a per-leaf shader) wants to read: a default density
// multiplier plus the raw inputs the multiplier was derived from, so
// callers can either trust `mass` or roll their own policy from the
// scalars below.
//
// All scalars are deliberately in [0, 1] (or [0, ~2] for `age01` —
// modules can over-mature without bound) so they can be wired straight
// into vertex attributes or scatter density without further normalisation.
struct FoliageSample {
    // Default density multiplier for the segment: 0 means "no foliage
    // here," 1 means "full." Foliage is distributed through the whole
    // crown — not just terminal tips — so plants read as full rather than
    // bare twigs with leaf-balls on the ends. Computed as
    //   `leafGrade(diameter) · maturity · vigor01 · light01 · (1 - senescence01)`
    // where `leafGrade` is 1 on twigs at leaf thickness and falls to 0 on
    // branches thicker than ~6× the leaf diameter (leaves grow on shoots,
    // not the trunk), and `maturity` ramps 0→1 as module.age reaches
    // species.moduleMatureAge. The same leaf-area term drives the canopy
    // shadow the simulation casts, so emitted foliage and simulated shade
    // agree. Callers wanting a different policy can ignore `mass` and
    // combine the raw scalars below themselves.
    float mass = 0.0f;

    // module.age / species.moduleMatureAge, clamped to [0, 2]. Stays
    // below 1 while the module is still maturing; above 1 once mature.
    // The upper clamp at 2 prevents very old modules from dominating
    // shader attributes that read this directly.
    float age01 = 0.0f;

    // module.vigor / species.maxVigor, clamped to [0, 1]. Drops as the
    // plant senesces (root vigor cap shrinks) and as canopy shading
    // attenuates Q_eff.
    float vigor01 = 0.0f;

    // module.light (Q_eff) — already in [0, 1] from light.cpp's
    // shade-tolerance lerp, no further clamp needed.
    float light01 = 0.0f;

    // module.lightExposure — the RAW illumination Q·Q_G before the shade-
    // tolerance lerp, clamped to [0, 1]. Unlike `light01` (which a shade-
    // tolerant species floors near 1.0), this carries the true shadow
    // gradient: ~1 in full sun, →0 deep in a closed canopy. Use this — not
    // `light01` — to carve foliage density by actual shade, so an interior
    // twig in deep shadow goes bare even for a shade-tolerant species.
    float lightExposure01 = 1.0f;

    // Plant-level senescence ramp:
    //   0 while plant.age <= species.maxAge,
    //   linearly to 1 over the next 20% of maxAge,
    //   clamped at 1 thereafter.
    // Use this to drive autumn-tint shaders or to thin foliage past
    // peak, on top of (or instead of) `mass`.
    float senescence01 = 0.0f;

    // True iff the owning module has no child modules in the plant —
    // the topology gate that distinguishes leaf-bearing twigs from
    // structural branches. The default `mass` formula uses this as a
    // hard gate.
    bool isTerminal = false;
};

// Emit the plant's branch skeleton as a flat list of `bromesh::BranchSegment`
// — the input shape expected by `bromesh::placeLeavesOnBranches` and
// `bromesh::scatterLeaves`. One segment per prototype edge per module,
// using the same grown world-space endpoints and per-node depth-from-
// prototype-root interpolation that `emitPlantMesh` uses, so trunk and
// foliage line up exactly.
//
// Per-segment fields:
//   - `from`, `to`        — world-space endpoints of the prototype edge
//                           (post-tropism, post-rotation), identical to
//                           the cylinder endpoints `emitPlantMesh` uses.
//   - `radius`            — radius at the segment's tip end (`to`). Tip
//                           rather than base or midpoint because the
//                           consumer's `maxRadius` filter is most
//                           intuitive as "leaves only on twigs whose tip
//                           is thin enough" — keeping leaves off the
//                           thicker trunk falls out naturally.
//   - `depth`             — per-segment hop count from the plant's root
//                           segment (root segments at depth 0). Matches
//                           `bromesh::spaceColonize`'s convention so the
//                           `minDepth` filter in `placeLeavesOnBranches`
//                           behaves the same against either source.
//   - `parent`            — index into the returned vector of the
//                           segment whose `to` equals this segment's
//                           `from`, or -1 for root segments. Root
//                           segments come in two shapes: (i) all edges
//                           of the plant's root module that start at the
//                           module's root node, and (ii) the first edge
//                           of each child module gets the parent module's
//                           segment terminating at the attach terminal.
//                           Topology is consistent with bromesh's
//                           `terminalOnly` child-count filter.
//
// Returns an empty vector when the plant has no modules.
std::vector<bromesh::BranchSegment> emitPlantSegments(const Plant& plant);

// Same, but appends every plant in the world into a single segment list.
// Segment `parent` indices are absolute into the returned vector — i.e.
// continue counting across plant boundaries, so a single
// `placeLeavesOnBranches` call can be made over the whole world.
std::vector<bromesh::BranchSegment> emitWorldSegments(const WorldState& world);

// Bloom / fruit anchor — a world-space candidate position for placing
// a flower (`bromesh::flower`) or a fruit cluster on a plant's terminal
// twigs. Emitted only for plants that have entered the flowering regime
// (`plant.flowering == true`); pre-flowering plants produce no anchors.
//
// One anchor per terminal node of each terminal module. A terminal
// module is one with no child modules in the plant's module-tree (same
// definition `FoliageSample::isTerminal` uses); a terminal node is one
// of the prototype's `terminalNodes` — the tip points where blooms
// naturally appear in real plants.
//
// Anchors carry the same life-state scalars FoliageSample exposes so
// callers can drive bloom→fruit transitions, color variation, and per-
// flower size attenuation from the simulation state without re-deriving
// any of it. The plant-level `senescence01` is in particular what a
// "promote blooms to fruit past peak" policy keys off.
struct BloomAnchor {
    // World-space anchor position — `worldNodePos` of the terminal node
    // (post-tropism, post-rotation). Matches the cylinder endpoints
    // emitPlantMesh draws so flowers and twig tips align exactly.
    bromath::Vec3 position{};

    // Unit outward direction at the anchor — the normalised vector from
    // the terminal node's incoming neighbour toward the terminal node
    // itself. Points "along the twig, outward toward the tip," so a
    // consumer rotating a flower mesh's local +Y onto this normal gets
    // the bloom facing the same way the twig is growing. Falls back to
    // +Y when the incoming edge is degenerate (e.g. zero-length).
    bromath::Vec3 normal = {0.0f, 1.0f, 0.0f};

    // Same as FoliageSample fields, see mesh_emit.h above.
    float age01           = 0.0f;
    float vigor01         = 0.0f;
    float senescence01    = 0.0f;
    // Raw illumination Q·Q_G at the anchor (see FoliageSample.lightExposure01)
    // — lets a consumer keep blooms on the lit canopy surface and skip ones
    // buried in deep interior shade.
    float lightExposure01 = 1.0f;
};

// Per-segment foliage state for the same segment order `emitPlantSegments`
// produces. Length and index alignment with `emitPlantSegments(plant)` are
// invariants: the i-th sample describes the i-th segment. Returns an empty
// vector when the plant has no modules.
std::vector<FoliageSample> emitPlantFoliage(const Plant& plant);

// World-level analogue, aligned with `emitWorldSegments(world)`. Walks
// plants in the same order, so concatenating per-plant samples in plant
// order produces identical output.
std::vector<FoliageSample> emitWorldFoliage(const WorldState& world);

// Emit bloom / fruit anchor candidates for a single plant. Returns an
// empty vector when the plant has not yet flowered or has no terminal
// modules with terminal nodes. Order is module-topological with
// per-module terminal nodes in declaration order.
std::vector<BloomAnchor> emitPlantBloomAnchors(const Plant& plant);

// Same across the whole world, concatenated in plant order. The
// returned anchors are intended to be fed to `bromesh::packAnchors`
// (with the world's branch capsule field as `avoid` and any foliage
// keep-out spheres) to thin the candidates down to a non-overlapping
// subset before instancing flower meshes at each survivor.
std::vector<BloomAnchor> emitWorldBloomAnchors(const WorldState& world);

} // namespace broflora
