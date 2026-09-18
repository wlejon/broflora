#include "broflora/sdf_mesh.h"
#include "broflora/mesh_emit.h"
#include <bromesh/isosurface/jit/sdf_node.h>
#include <bromesh/isosurface/jit/jit_mesher.h>
#include <bromath/aabb.h>
#include <algorithm>
#include <cmath>

namespace broflora {

namespace {

bromesh::MeshData emitSegmentsSdfMesh(const std::vector<bromesh::BranchSegment>& segs, const SdfMeshOptions& opts) {
    if (segs.empty()) {
        return bromesh::MeshData{};
    }

    bromesh::SdfGraph graph;
    bromath::AABB3 bounds = bromath::aempty3();
    int root = -1;

    for (const auto& seg : segs) {
        if (bromath::vlen(seg.to - seg.from) <= 1e-4f || seg.radius <= 1e-4f) {
            continue;
        }

        bromath::Vec3 rVec{seg.radius, seg.radius, seg.radius};
        bounds = bromath::aexpand(bounds, seg.from - rVec);
        bounds = bromath::aexpand(bounds, seg.from + rVec);
        bounds = bromath::aexpand(bounds, seg.to - rVec);
        bounds = bromath::aexpand(bounds, seg.to + rVec);

        int cap = graph.capsule(seg.from, seg.to, seg.radius);
        root = (root < 0) ? cap : (opts.smoothK > 1e-4f ? graph.opSmoothUnion(root, cap, opts.smoothK) : graph.opUnion(root, cap));
    }

    if (root < 0 || bromath::aisEmpty(bounds)) {
        return bromesh::MeshData{};
    }

    graph.setRoot(root);

    float expandDist = (opts.smoothK > 0.0f ? opts.smoothK : 0.0f) + (opts.margin > 0.0f ? opts.margin : 0.0f);
    bounds.min.x -= expandDist;
    bounds.min.y -= expandDist;
    bounds.min.z -= expandDist;
    bounds.max.x += expandDist;
    bounds.max.y += expandDist;
    bounds.max.z += expandDist;

    float vs = opts.voxelSize > 0.005f ? opts.voxelSize : 0.04f;
    int dimX = std::clamp(static_cast<int>(std::ceil((bounds.max.x - bounds.min.x) / vs)), 8, 256);
    int dimY = std::clamp(static_cast<int>(std::ceil((bounds.max.y - bounds.min.y) / vs)), 8, 256);
    int dimZ = std::clamp(static_cast<int>(std::ceil((bounds.max.z - bounds.min.z) / vs)), 8, 256);

    if (opts.useSurfaceNets) {
        return bromesh::surfaceNetsFromSDF(graph, dimX, dimY, dimZ, bounds, 0.0f);
    } else {
        return bromesh::marchingCubesFromSDF(graph, dimX, dimY, dimZ, bounds, 0.0f, true, true);
    }
}

} // namespace

bromesh::MeshData emitPlantSdfMesh(const Plant& plant, const SdfMeshOptions& opts) {
    auto segs = emitPlantSegments(plant);
    return emitSegmentsSdfMesh(segs, opts);
}

bromesh::MeshData emitWorldSdfMesh(const WorldState& world, const SdfMeshOptions& opts) {
    auto segs = emitWorldSegments(world);
    return emitSegmentsSdfMesh(segs, opts);
}

} // namespace broflora
