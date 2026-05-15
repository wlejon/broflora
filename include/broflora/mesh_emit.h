#pragma once

// Optional mesh-emit boundary. Broflora's core simulation never depends
// on bromesh; this header is the only place the two libraries meet.
//
// When the sibling ../bromesh checkout is present at configure time
// CMake defines BROFLORA_HAS_BROMESH=1 and we alias `broflora::MeshData`
// to `bromesh::MeshData` so emitted plants drop straight into the rest
// of the bromesh pipeline. Without bromesh we expose a minimal stand-in
// carrying the same three streams; `emitPlantMesh` and `emitWorldMesh`
// return an empty mesh in that case so callers can compile and link
// against the same surface either way.

#if defined(BROFLORA_HAS_BROMESH)
    #include <bromesh/mesh_data.h>
#else
    #include <cstdint>
    #include <vector>
#endif

#include "broflora/plant.h"
#include "broflora/world.h"

namespace broflora {

#if defined(BROFLORA_HAS_BROMESH)
using MeshData = bromesh::MeshData;
#else
// Minimal stand-in matching the field layout of bromesh::MeshData. Same
// stride conventions: positions xyz×3, normals xyz×3, indices uint32_t.
struct MeshData {
    std::vector<float>    positions;
    std::vector<float>    normals;
    std::vector<uint32_t> indices;

    size_t vertexCount()   const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size()   / 3; }
    bool   empty()         const { return positions.empty(); }

    void clear() {
        positions.clear();
        normals.clear();
        indices.clear();
    }
};
#endif

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
//     not applied — branch placement and the mesh stay in (θ, ψ).
MeshData emitPlantMesh(const Plant& plant, uint32_t sides = 6);

// Same, but appends every plant in the world into a single mesh.
MeshData emitWorldMesh(const WorldState& world, uint32_t sides = 6);

} // namespace broflora
