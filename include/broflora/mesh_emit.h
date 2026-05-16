#pragma once

// Mesh-emit boundary — the only place the simulation core meets the
// geometry side. broflora emits straight into `bromesh::MeshData` so
// output drops into the rest of the bromesh pipeline (UV/colors/tangents
// downstream, leaf scatter, foliage anchors) without copying.

#include <bromesh/mesh_data.h>

#include "broflora/plant.h"
#include "broflora/world.h"

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

} // namespace broflora
