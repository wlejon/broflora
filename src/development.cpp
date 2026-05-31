#include "broflora/development.h"

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
using bromath::vlen2;
using bromath::vnorm;
using bromath::smoothstep01;
using internal::rotateYawPitch;
using internal::nodeOffsetFromRoot;

// Recompute the per-node positions in module-local frame for the current
// module age (paper §3.3, branch length growth):
//
//   a_b      = max(0, a_u - a_n)
//   l_b      = min(l_max, β · a_b)            // also clipped to the
//   |proto|  // prototype's edge length so growth never overshoots layout
//
// Walks the prototype tree from the root in topological order — the
// `parent index < child index` invariant on prototype edges guarantees
// the parent has been positioned by the time we reach a child.
void refreshModuleNodePositions(BranchModuleInstance& m) {
    if (!m.prototype) { m.nodePositions.clear(); return; }
    const auto& proto = *m.prototype;
    const size_t n = proto.nodes.size();
    m.nodePositions.assign(n, Vec3{});
    if (n == 0) return;

    std::vector<uint32_t> parent(n, UINT32_MAX);
    for (const auto& e : proto.edges) {
        uint32_t pa = e.a, ch = e.b;
        if (pa > ch) std::swap(pa, ch);
        if (ch < n) parent[ch] = pa;
    }

    const uint32_t rootIdx = proto.rootNode < n ? proto.rootNode : 0u;
    m.nodePositions[rootIdx] = proto.nodes[rootIdx].position;

    for (uint32_t i = 0; i < n; ++i) {
        if (i == rootIdx) continue;
        uint32_t p = parent[i];
        if (p == UINT32_MAX) {
            // Disconnected — fall back to static layout.
            m.nodePositions[i] = proto.nodes[i].position;
            continue;
        }
        const auto& nodeI = proto.nodes[i];
        Vec3 protoDir = proto.nodes[i].position - proto.nodes[p].position;
        float protoLen = vlen(protoDir);
        Vec3 dir = (protoLen > 1e-6f) ? protoDir * (1.0f / protoLen) : Vec3{};

        float ab = std::max(0.0f, m.age - nodeI.ageAtBirth);
        float l  = std::min(nodeI.lengthMax, nodeI.thickening * ab);
        l = std::min(l, protoLen);  // never overshoot the static layout

        m.nodePositions[i] = m.nodePositions[p] + dir * l;
    }
}

namespace {

// Bounding sphere over the module's current (grown, rotated, tropism-
// curved) node positions translated by `worldPos`. Falls back to a zero
// sphere when the node cache is empty.
void computeBbox(const Species& sp, BranchModuleInstance& m) {
    if (!m.prototype || m.nodePositions.empty()) {
        m.bboxCenter = m.worldPos;
        m.bboxRadius = 0.0f;
        m.axisTip    = m.worldPos;
        return;
    }
    Vec3 sum = {0.0f, 0.0f, 0.0f};
    size_t count = 0;
    for (size_t i = 0; i < m.nodePositions.size(); ++i) {
        sum += nodeOffsetFromRoot(sp, m, static_cast<uint32_t>(i));
        ++count;
    }
    Vec3 centre = sum * (1.0f / static_cast<float>(count));
    float maxD2 = 0.0f;
    for (size_t i = 0; i < m.nodePositions.size(); ++i) {
        Vec3 r = nodeOffsetFromRoot(sp, m, static_cast<uint32_t>(i));
        maxD2 = std::max(maxD2, vlen2(r - centre));
    }
    m.bboxCenter = m.worldPos + centre;
    m.bboxRadius = std::sqrt(maxD2);

    // Main-axis capsule tip: mean of the terminal nodes' world offsets — the
    // module's central growth direction (the same axis the spawn tropism term
    // uses), so the capsule runs root → crown along the dominant branch.
    Vec3 tipSum = {0.0f, 0.0f, 0.0f};
    size_t tipCount = 0;
    for (uint32_t tnode : m.prototype->terminalNodes) {
        if (tnode < m.nodePositions.size()) {
            tipSum += nodeOffsetFromRoot(sp, m, tnode);
            ++tipCount;
        }
    }
    m.axisTip = (tipCount > 0)
                  ? m.worldPos + tipSum * (1.0f / static_cast<float>(tipCount))
                  : m.bboxCenter;
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

    // --- Refresh per-node grown positions for every module first; the
    // worldPos pass below needs the parent's grown terminal location to
    // attach children at the right (possibly partially-grown) spot.
    for (auto& m : mods) refreshModuleNodePositions(m);

    // --- World position pass (parents are guaranteed earlier in `mods`).
    // m.worldPos is the un-tropism attach point handed down from the
    // parent's terminal node; per-node tropism is applied inside
    // `nodeOffsetFromRoot`, which both the bbox below and the mesh
    // emitter consume.
    for (size_t i = 0; i < mods.size(); ++i) {
        auto& m = mods[i];
        if (m.parent == UINT32_MAX) {
            m.worldPos = plant.origin;
        } else {
            const auto& parent = mods[m.parent];
            m.worldPos = parent.worldPos
                       + nodeOffsetFromRoot(sp, parent, m.parentAttachTerminal);
        }
        computeBbox(sp, m);
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
