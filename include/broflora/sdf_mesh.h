#pragma once
#include <bromesh/mesh_data.h>
#include "broflora/plant.h"
#include "broflora/world.h"

namespace broflora {

struct SdfMeshOptions {
    float voxelSize = 0.04f;      // grid voxel size in meters
    float smoothK = 0.03f;        // smooth union blending radius
    bool useSurfaceNets = true;   // true for Surface Nets, false for Marching Cubes
    float margin = 0.08f;         // boundary safety margin around bbox
};

/// Meshes a plant into a watertight manifold organic mesh using bromesh::SdfGraph
/// capsules with smooth polynomial union, evaluated and meshed via JIT.
bromesh::MeshData emitPlantSdfMesh(const Plant& plant, const SdfMeshOptions& opts = {});

/// Meshes all plants in the world into a watertight organic mesh.
bromesh::MeshData emitWorldSdfMesh(const WorldState& world, const SdfMeshOptions& opts = {});

} // namespace broflora
