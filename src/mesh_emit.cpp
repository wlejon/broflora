#include "broflora/mesh_emit.h"

#include "bromath/scalar.h"
#include "bromath/vec.h"
#include "internal_geom.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace broflora {

using bromath::Vec3;
using bromath::vlen;
using bromath::vnorm;
using internal::rotateYawPitch;

namespace {

// Build an orthonormal frame around `axis` (unit). `axis` becomes +y of
// the frame so we can lay a ring of points around the cylinder body in
// the (x,z)-of-frame plane. Stable for any non-degenerate axis.
void frameAround(Vec3 axis, Vec3& outX, Vec3& outZ) {
    Vec3 up = (std::fabs(axis.y) < 0.9f) ? Vec3{0.0f, 1.0f, 0.0f}
                                         : Vec3{1.0f, 0.0f, 0.0f};
    // x = normalize(up × axis); z = axis × x.
    Vec3 x = {
        up.y * axis.z - up.z * axis.y,
        up.z * axis.x - up.x * axis.z,
        up.x * axis.y - up.y * axis.x,
    };
    x = vnorm(x);
    Vec3 z = {
        axis.y * x.z - axis.z * x.y,
        axis.z * x.x - axis.x * x.z,
        axis.x * x.y - axis.y * x.x,
    };
    outX = x;
    outZ = vnorm(z);
}

void emitCylinder(MeshData& mesh,
                  Vec3 a, Vec3 b, float radiusA, float radiusB,
                  uint32_t sides) {
    if (sides < 3) sides = 3;
    Vec3 axis = b - a;
    float len = vlen(axis);
    if (len < 1e-5f || (radiusA <= 0.0f && radiusB <= 0.0f)) return;
    axis = axis * (1.0f / len);

    Vec3 fx, fz;
    frameAround(axis, fx, fz);

    const uint32_t baseIdx = static_cast<uint32_t>(mesh.positions.size() / 3);

    for (uint32_t i = 0; i < sides; ++i) {
        float t = bromath::TWO_PI * static_cast<float>(i) / static_cast<float>(sides);
        float cx = std::cos(t), cz = std::sin(t);
        // Outward radial direction in world space.
        Vec3 radial = fx * cx + fz * cz;
        Vec3 pa = a + radial * radiusA;
        Vec3 pb = b + radial * radiusB;

        mesh.positions.push_back(pa.x); mesh.positions.push_back(pa.y); mesh.positions.push_back(pa.z);
        mesh.normals.push_back(radial.x); mesh.normals.push_back(radial.y); mesh.normals.push_back(radial.z);
        mesh.positions.push_back(pb.x); mesh.positions.push_back(pb.y); mesh.positions.push_back(pb.z);
        mesh.normals.push_back(radial.x); mesh.normals.push_back(radial.y); mesh.normals.push_back(radial.z);
    }

    for (uint32_t i = 0; i < sides; ++i) {
        uint32_t i0 = baseIdx + 2 * i;
        uint32_t i1 = baseIdx + 2 * i + 1;
        uint32_t i2 = baseIdx + 2 * ((i + 1) % sides);
        uint32_t i3 = baseIdx + 2 * ((i + 1) % sides) + 1;
        mesh.indices.push_back(i0); mesh.indices.push_back(i1); mesh.indices.push_back(i3);
        mesh.indices.push_back(i0); mesh.indices.push_back(i3); mesh.indices.push_back(i2);
    }
}

// World-space position of a module-local prototype-node index, applying
// the module's yaw+pitch rotation and worldPos offset. Mirrors the
// localNodePos walk used by development.cpp.
Vec3 worldNodePos(const BranchModuleInstance& m, uint32_t nodeIdx) {
    if (!m.prototype || nodeIdx >= m.prototype->nodes.size()) return m.worldPos;
    Vec3 local = (nodeIdx < m.nodePositions.size())
        ? m.nodePositions[nodeIdx]
        : m.prototype->nodes[nodeIdx].position;
    return m.worldPos + rotateYawPitch(local, m.orientation.psi, m.orientation.theta);
}

// Depth of every prototype node from `rootNode`, measured in edge hops.
// UINT32_MAX for disconnected nodes (they fall back to the static layout
// in the rest of the pipeline; here we just give them root depth so a
// cylinder still emits with sensible radius). Mirrors the BFS walk that
// development.cpp already does implicitly via the edge ordering, but
// this TU needs it explicitly to interpolate per-edge radius.
std::vector<uint32_t> nodeDepths(const BranchModulePrototype& proto) {
    const size_t n = proto.nodes.size();
    std::vector<uint32_t> depth(n, std::numeric_limits<uint32_t>::max());
    if (n == 0) return depth;
    const uint32_t root = proto.rootNode < n ? proto.rootNode : 0u;
    depth[root] = 0;
    // Edges are topologically ordered (parent index < child index by
    // contract on ModuleEdge), so a single forward pass suffices.
    for (const auto& e : proto.edges) {
        uint32_t a = e.a, b = e.b;
        if (a > b) std::swap(a, b);
        if (a >= n || b >= n) continue;
        if (depth[a] != std::numeric_limits<uint32_t>::max() &&
            depth[b] == std::numeric_limits<uint32_t>::max()) {
            depth[b] = depth[a] + 1;
        }
    }
    for (auto& d : depth) {
        if (d == std::numeric_limits<uint32_t>::max()) d = 0;
    }
    return depth;
}

void emitPlantInto(const Plant& plant, MeshData& mesh, uint32_t sides) {
    for (const auto& m : plant.modules) {
        if (!m.prototype) continue;

        // Stem and tip radii for *this module*. Interpolate per-edge by
        // depth-from-prototype-root so multi-edge modules taper smoothly
        // instead of stepping at internal nodes.
        const float tipR  = 0.5f * plant.species.leafDiameter;
        const float rootR = std::max(tipR, 0.5f * m.diameter);

        const auto depth = nodeDepths(*m.prototype);
        uint32_t maxDepth = 0;
        for (uint32_t d : depth) if (d > maxDepth) maxDepth = d;
        const float invMaxDepth = (maxDepth > 0)
            ? 1.0f / static_cast<float>(maxDepth) : 0.0f;

        auto radiusForNode = [&](uint32_t idx) -> float {
            if (idx >= depth.size() || maxDepth == 0) return rootR;
            const float t = static_cast<float>(depth[idx]) * invMaxDepth;
            return rootR + (tipR - rootR) * t;
        };

        for (const auto& e : m.prototype->edges) {
            Vec3 pa = worldNodePos(m, e.a);
            Vec3 pb = worldNodePos(m, e.b);
            emitCylinder(mesh, pa, pb,
                         radiusForNode(e.a),
                         radiusForNode(e.b),
                         sides);
        }
    }
}

} // namespace

MeshData emitPlantMesh(const Plant& plant, uint32_t sides) {
    MeshData mesh;
    emitPlantInto(plant, mesh, sides);
    return mesh;
}

MeshData emitWorldMesh(const WorldState& world, uint32_t sides) {
    MeshData mesh;
    for (const auto& plant : world.plants) {
        emitPlantInto(plant, mesh, sides);
    }
    return mesh;
}

} // namespace broflora
