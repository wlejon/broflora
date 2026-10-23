#include "broflora/sdf_mesh.h"
#include "broflora/mesh_emit.h"
#include <brass/codegen/kernel_jit.hpp>
#include <bromesh/isosurface/jit/sdf_node.h>
#include <bromesh/isosurface/jit/jit_mesher.h>
#include <bromath/aabb.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace broflora {

namespace {

bromath::AABB3 computeSegmentsBounds(const std::vector<bromesh::BranchSegment>& segs) {
    bromath::AABB3 bounds = bromath::aempty3();
    for (const auto& seg : segs) {
        if (bromath::vlen(seg.to - seg.from) <= 1e-4f || seg.radius <= 1e-4f) {
            continue;
        }
        bromath::Vec3 rVec{seg.radius, seg.radius, seg.radius};
        bounds = bromath::aexpand(bounds, seg.from - rVec);
        bounds = bromath::aexpand(bounds, seg.from + rVec);
        bounds = bromath::aexpand(bounds, seg.to - rVec);
        bounds = bromath::aexpand(bounds, seg.to + rVec);
    }
    return bounds;
}

bromesh::MeshData emitSegmentsSdfMesh(const std::vector<bromesh::BranchSegment>& segs, const SdfMeshOptions& opts) {
    if (segs.empty()) {
        return bromesh::MeshData{};
    }

    bromath::AABB3 bounds = computeSegmentsBounds(segs);
    if (bromath::aisEmpty(bounds)) {
        return bromesh::MeshData{};
    }

    float expandDist = (opts.smoothK > 0.0f ? opts.smoothK : 0.0f) + (opts.margin > 0.0f ? opts.margin : 0.0f);
    bounds.min.x -= expandDist;
    bounds.min.y -= expandDist;
    bounds.min.z -= expandDist;
    bounds.max.x += expandDist;
    bounds.max.y += expandDist;
    bounds.max.z += expandDist;

    // Normalize capsule coordinates into local space relative to the root
    // segment origin so translated copies of the same plant share the compiled
    // JIT SDF kernel in JitSdfCompiler's cache and avoid float precision loss
    // far from the world origin. Rounding to 1e-5m eliminates 1-ULP float drift
    // from world-space translation ((origin + local) - origin != local).
    auto qvec = [](bromath::Vec3 v) -> bromath::Vec3 {
        return {
            std::round(v.x * 1e5f) * 1e-5f,
            std::round(v.y * 1e5f) * 1e-5f,
            std::round(v.z * 1e5f) * 1e-5f
        };
    };
    const bromath::Vec3 origin = segs.front().from;
    const bromath::AABB3 localBounds{qvec(bounds.min - origin), qvec(bounds.max - origin)};

    bromesh::SdfGraph graph;
    std::vector<int> level;
    level.reserve(segs.size());

    for (const auto& seg : segs) {
        if (bromath::vlen(seg.to - seg.from) <= 1e-4f || seg.radius <= 1e-4f) {
            continue;
        }
        level.push_back(graph.capsule(qvec(seg.from - origin), qvec(seg.to - origin), seg.radius));
    }

    if (level.empty()) {
        return bromesh::MeshData{};
    }

    // Reduce capsule primitives in a balanced binary tree (depth O(log N))
    // instead of a linear chain (depth O(N)) to minimize JIT AST depth and
    // register pressure during SIMD volume evaluation.
    const bool useSmooth = opts.smoothK > 1e-4f;
    while (level.size() > 1) {
        std::vector<int> next;
        next.reserve((level.size() + 1) / 2);
        for (size_t i = 0; i + 1 < level.size(); i += 2) {
            int combined = useSmooth
                ? graph.opSmoothUnion(level[i], level[i + 1], opts.smoothK)
                : graph.opUnion(level[i], level[i + 1]);
            next.push_back(combined);
        }
        if (level.size() & 1) {
            next.push_back(level.back());
        }
        level = std::move(next);
    }

    graph.setRoot(level[0]);

    float vs = opts.voxelSize > 0.005f ? opts.voxelSize : 0.04f;
    int dimX = std::clamp(static_cast<int>(std::ceil((localBounds.max.x - localBounds.min.x) / vs)), 8, 256);
    int dimY = std::clamp(static_cast<int>(std::ceil((localBounds.max.y - localBounds.min.y) / vs)), 8, 256);
    int dimZ = std::clamp(static_cast<int>(std::ceil((localBounds.max.z - localBounds.min.z) / vs)), 8, 256);

    bromesh::MeshData mesh = opts.useSurfaceNets
        ? bromesh::surfaceNetsFromSDF(graph, dimX, dimY, dimZ, localBounds, 0.0f)
        : bromesh::marchingCubesFromSDF(graph, dimX, dimY, dimZ, localBounds, 0.0f, true, true);

    const size_t nv = mesh.vertexCount();
    for (size_t i = 0; i < nv; ++i) {
        mesh.positions[3 * i + 0] += origin.x;
        mesh.positions[3 * i + 1] += origin.y;
        mesh.positions[3 * i + 2] += origin.z;
    }
    return mesh;
}

void appendMeshData(bromesh::MeshData& dst, const bromesh::MeshData& src) {
    if (src.empty()) return;
    const uint32_t vOffset = static_cast<uint32_t>(dst.vertexCount());
    dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
    dst.normals.insert(dst.normals.end(), src.normals.begin(), src.normals.end());
    dst.uvs.insert(dst.uvs.end(), src.uvs.begin(), src.uvs.end());
    dst.colors.insert(dst.colors.end(), src.colors.begin(), src.colors.end());
    dst.tangents.insert(dst.tangents.end(), src.tangents.begin(), src.tangents.end());
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (uint32_t idx : src.indices) {
        dst.indices.push_back(idx + vOffset);
    }
}

} // namespace

bromesh::MeshData emitPlantSdfMesh(const Plant& plant, const SdfMeshOptions& opts) {
    auto segs = emitPlantSegments(plant);
    return emitSegmentsSdfMesh(segs, opts);
}

bromesh::MeshData emitWorldSdfMesh(const WorldState& world, const SdfMeshOptions& opts) {
    const size_t nPlants = world.plants.size();
    if (nPlants == 0) {
        return bromesh::MeshData{};
    }
    if (nPlants == 1) {
        return emitPlantSdfMesh(world.plants[0], opts);
    }

    const float expandDist = (opts.smoothK > 0.0f ? opts.smoothK : 0.0f) + (opts.margin > 0.0f ? opts.margin : 0.0f);
    std::vector<std::vector<bromesh::BranchSegment>> plantSegs(nPlants);
    std::vector<bromath::AABB3> plantBounds(nPlants, bromath::aempty3());

    brass::codegen::parallel_for(0, static_cast<int64_t>(nPlants), [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            const size_t p = static_cast<size_t>(i);
            plantSegs[p] = emitPlantSegments(world.plants[p]);
            bromath::AABB3 b = computeSegmentsBounds(plantSegs[p]);
            if (!bromath::aisEmpty(b)) {
                b.min.x -= expandDist;
                b.min.y -= expandDist;
                b.min.z -= expandDist;
                b.max.x += expandDist;
                b.max.y += expandDist;
                b.max.z += expandDist;
            }
            plantBounds[p] = b;
        }
    });

    // Partition plants into connected components of overlapping expanded AABBs.
    // Overlapping plants stay in a single component so smooth union blends them;
    // disjoint plants mesh in their own tight local voxel grids without evaluating
    // distant plants' capsules or empty space between trees.
    std::vector<uint8_t> visited(nPlants, 0);
    std::vector<std::vector<size_t>> components;
    for (size_t i = 0; i < nPlants; ++i) {
        if (visited[i] || bromath::aisEmpty(plantBounds[i])) continue;
        visited[i] = 1;
        std::vector<size_t> comp = {i};
        for (size_t q = 0; q < comp.size(); ++q) {
            const size_t u = comp[q];
            for (size_t v = 0; v < nPlants; ++v) {
                if (!visited[v] && !bromath::aisEmpty(plantBounds[v]) &&
                    bromath::aintersects(plantBounds[u], plantBounds[v])) {
                    visited[v] = 1;
                    comp.push_back(v);
                }
            }
        }
        components.push_back(std::move(comp));
    }

    bromesh::MeshData out;
    for (const auto& comp : components) {
        if (comp.size() == 1) {
            appendMeshData(out, emitSegmentsSdfMesh(plantSegs[comp[0]], opts));
        } else {
            size_t totalSegs = 0;
            for (size_t p : comp) totalSegs += plantSegs[p].size();
            std::vector<bromesh::BranchSegment> combined;
            combined.reserve(totalSegs);
            for (size_t p : comp) {
                combined.insert(combined.end(), plantSegs[p].begin(), plantSegs[p].end());
            }
            appendMeshData(out, emitSegmentsSdfMesh(combined, opts));
        }
    }
    return out;
}

} // namespace broflora
