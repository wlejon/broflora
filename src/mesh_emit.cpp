#include "broflora/mesh_emit.h"

#include "broflora/vec_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace broflora {

namespace {

// Yaw+pitch rotation, same convention used by development.cpp /
// spawning.cpp. Kept duplicated rather than exporting from one of the
// other TUs because the rotation is implementation detail of the
// branch-placement contract.
Vec3 rotateYP(Vec3 v, float yaw, float pitch) {
    float cy = std::cos(yaw),   sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);
    Vec3 r1 = { cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z };
    return { r1.x, cp * r1.y - sp * r1.z, sp * r1.y + cp * r1.z };
}

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
    x = v3_normalize(x);
    Vec3 z = {
        axis.y * x.z - axis.z * x.y,
        axis.z * x.x - axis.x * x.z,
        axis.x * x.y - axis.y * x.x,
    };
    outX = x;
    outZ = v3_normalize(z);
}

void emitCylinder(MeshData& mesh,
                  Vec3 a, Vec3 b, float radiusA, float radiusB,
                  uint32_t sides) {
    if (sides < 3) sides = 3;
    Vec3 axis = v3_sub(b, a);
    float len = v3_len(axis);
    if (len < 1e-5f || (radiusA <= 0.0f && radiusB <= 0.0f)) return;
    axis = v3_scale(axis, 1.0f / len);

    Vec3 fx, fz;
    frameAround(axis, fx, fz);

    const uint32_t baseIdx = static_cast<uint32_t>(mesh.positions.size() / 3);
    const float twoPi = 2.0f * 3.14159265358979f;

    for (uint32_t i = 0; i < sides; ++i) {
        float t = twoPi * static_cast<float>(i) / static_cast<float>(sides);
        float cx = std::cos(t), cz = std::sin(t);
        // Outward radial direction in world space.
        Vec3 radial = v3_add(v3_scale(fx, cx), v3_scale(fz, cz));
        Vec3 pa = v3_add(a, v3_scale(radial, radiusA));
        Vec3 pb = v3_add(b, v3_scale(radial, radiusB));

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
    return v3_add(m.worldPos, rotateYP(local, m.orientation.psi, m.orientation.theta));
}

void emitPlantInto(const Plant& plant, MeshData& mesh, uint32_t sides) {
    for (const auto& m : plant.modules) {
        if (!m.prototype) continue;
        // Tip radius — same constant we use as the pipe-model base case.
        const float tipR  = 0.5f * plant.species.leafDiameter;
        const float rootR = std::max(tipR, 0.5f * m.diameter);

        for (const auto& e : m.prototype->edges) {
            Vec3 pa = worldNodePos(m, e.a);
            Vec3 pb = worldNodePos(m, e.b);
            // Within a module, segment closer to the prototype root keeps
            // the module's stem diameter; segment ending at a terminal
            // tapers to the leaf radius. Quick heuristic that avoids
            // emitting per-edge pipe-model state.
            bool aIsTerm = false, bIsTerm = false;
            for (uint32_t t : m.prototype->terminalNodes) {
                if (t == e.a) aIsTerm = true;
                if (t == e.b) bIsTerm = true;
            }
            float ra = aIsTerm ? tipR : rootR;
            float rb = bIsTerm ? tipR : rootR;
            emitCylinder(mesh, pa, pb, ra, rb, sides);
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
