#include "broflora/development.h"

#include "broflora/vec_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace broflora {

namespace {

// Rotate `v` by yaw (around +Y) then pitch (around +X). Roll (φ) is
// ignored for branch placement — it doesn't change a child's attachment
// point. Paper-equivalent for branch positions; the roll only matters
// later when we expose nodes for meshing.
Vec3 rotateYawPitch(Vec3 v, float yaw, float pitch) {
    float cy = std::cos(yaw),   sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);
    // Yaw first (around Y).
    Vec3 r1 = { cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z };
    // Then pitch (around X).
    Vec3 r2 = { r1.x, cp * r1.y - sp * r1.z, sp * r1.y + cp * r1.z };
    return r2;
}

// Local-space position of `prototype.nodes[idx]` after applying the
// module's orientation.
Vec3 localNodePos(const BranchModuleInstance& m, uint32_t nodeIdx) {
    if (!m.prototype || nodeIdx >= m.prototype->nodes.size()) return {};
    Vec3 p = m.prototype->nodes[nodeIdx].position;
    return rotateYawPitch(p, m.orientation.psi, m.orientation.theta);
}

// Max distance from the prototype's root node to any other node — used
// as the module's nominal radius at full size.
float prototypeRadius(const BranchModulePrototype& proto) {
    if (proto.nodes.empty()) return 0.0f;
    Vec3 r = proto.nodes[proto.rootNode].position;
    float maxD2 = 0.0f;
    for (const auto& n : proto.nodes) {
        Vec3 d = v3_sub(n.position, r);
        maxD2 = std::max(maxD2, v3_len2(d));
    }
    return std::sqrt(maxD2);
}

} // namespace

void developModules(Plant& plant, float dt) {
    const auto& sp = plant.species;
    const float vRange = sp.maxVigor - sp.minVigor;

    auto& mods = plant.modules;
    if (mods.empty()) return;

    // --- Age advancement (paper eq. for Υ(u)).
    if (vRange > 0.0f) {
        for (auto& m : mods) {
            float t = (m.vigor - sp.minVigor) / vRange;
            float upsilon = smoothstep01(t) * sp.growthScale;
            m.age += upsilon * dt;
        }
    }
    plant.age += dt;

    // --- World position pass (parents are guaranteed earlier in `mods`).
    for (size_t i = 0; i < mods.size(); ++i) {
        auto& m = mods[i];
        if (m.parent == UINT32_MAX) {
            m.worldPos = plant.origin;
        } else {
            const auto& parent = mods[m.parent];
            // Attach at parent's terminal node, transformed by parent's
            // orientation, then translated to parent's world pos.
            Vec3 localTerm = {};
            if (parent.prototype && m.parentAttachTerminal < parent.prototype->nodes.size()) {
                localTerm = localNodePos(parent, m.parentAttachTerminal);
            }
            m.worldPos = v3_add(parent.worldPos, localTerm);
        }

        // --- Tropism offset τ(a_b) = g1 · ĝ · g2 / (a_b + g1).
        // Applied to module's world position; effectively bends the
        // child away from the prototype-rigid attachment.
        const float ab = std::max(0.0f, m.age);
        const float denom = ab + sp.tropismG1;
        if (denom > 1e-6f) {
            float k = sp.tropismG1 * sp.tropismG2 / denom;
            Vec3 g = v3_normalize(sp.tropismDir);
            m.worldPos = v3_add(m.worldPos, v3_scale(g, k));
        }

        // --- Bbox. Radius scales with vigor-driven growth; capped at
        // the prototype's natural radius.
        const float natural = m.prototype ? prototypeRadius(*m.prototype) : 0.0f;
        float growth = (vRange > 0.0f)
            ? smoothstep01((m.vigor - sp.minVigor) / vRange)
            : 1.0f;
        // Modules are never bigger than their prototype; ramp from 0 to
        // natural as age approaches moduleMatureAge.
        float ageFrac = sp.moduleMatureAge > 0.0f
            ? std::min(1.0f, m.age / sp.moduleMatureAge)
            : 1.0f;
        m.bboxRadius = natural * std::max(growth, ageFrac);
        // Centre the sphere at the midpoint between root and farthest node.
        Vec3 mid = m.prototype
            ? localNodePos(m, m.prototype->terminalNodes.empty()
                ? m.prototype->rootNode
                : m.prototype->terminalNodes.front())
            : Vec3{};
        m.bboxCenter = v3_add(m.worldPos, v3_scale(mid, 0.5f));
    }

    // --- Pipe-model diameters: reverse topo order, terminal → root.
    // Each module's diameter is (Σ child^pipeExp)^(1/pipeExp); leaves
    // get the species-defined tip diameter.
    const float pe = sp.pipeExp > 0.0f ? sp.pipeExp : 2.5f;
    const float invPe = 1.0f / pe;

    // Pass 1: zero out, mark which modules have any children.
    std::vector<uint8_t> hasChild(mods.size(), 0);
    for (size_t i = 0; i < mods.size(); ++i) {
        if (mods[i].parent != UINT32_MAX) hasChild[mods[i].parent] = 1;
        mods[i].diameter = 0.0f;
    }
    // Pass 2: accumulate child^pe into parent's diameter slot.
    for (size_t i = mods.size(); i-- > 0; ) {
        auto& m = mods[i];
        if (!hasChild[i]) {
            // Terminal — base case.
            m.diameter = sp.leafDiameter;
        } else {
            // Convert the accumulated sum back to the pe'th root.
            m.diameter = std::pow(m.diameter, invPe);
        }
        if (m.parent != UINT32_MAX) {
            mods[m.parent].diameter += std::pow(m.diameter, pe);
        }
    }
}

} // namespace broflora
