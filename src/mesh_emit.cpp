#include "broflora/mesh_emit.h"

#include "bromath/scalar.h"
#include "bromath/vec.h"
#include "bromesh/manipulation/normals.h"
#include "bromesh/procedural/branches.h"
#include "internal_foliage.h"
#include "internal_geom.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace broflora {

using bromath::Vec3;
using internal::nodeOffsetFromRoot;

namespace {

// World-space position of a module-local prototype-node index, including
// the module's yaw+pitch rotation, the per-node tropism curvature, and
// the module's worldPos. Routes through the same internal helper that
// development.cpp uses to build bboxes and that spawning.cpp uses to
// pick attach points, so the mesh matches the simulator's geometry
// exactly.
Vec3 worldNodePos(const Species& sp, const BranchModuleInstance& m, uint32_t nodeIdx) {
    if (!m.prototype || nodeIdx >= m.prototype->nodes.size()) return m.worldPos;
    return m.worldPos + nodeOffsetFromRoot(sp, m, nodeIdx);
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

} // namespace

MeshData emitPlantMesh(const Plant& plant, uint32_t sides) {
    // The branch skeleton emitSegments already produces is exactly the
    // BranchSegment input bromesh::meshBranches wants. Meshing through it
    // sweeps each single-child chain as one continuous parallel-transport
    // tube — smooth welded joints, UVs, and end caps — instead of the
    // faceted, unwelded, UV-less per-edge cylinders this used to emit.
    MeshData mesh = bromesh::meshBranches(emitPlantSegments(plant), static_cast<int>(sides));
    // The swept tube carries UVs + normals, so complete the material set
    // with tangents — bark normal maps need them, and nothing downstream
    // could recover them once the segment topology is gone.
    bromesh::generateTangents(mesh);
    return mesh;
}

MeshData emitWorldMesh(const WorldState& world, uint32_t sides) {
    // emitWorldSegments concatenates every plant with absolute parent
    // indices, and each plant's root segments carry parent == -1, so a
    // single meshBranches call over the whole list meshes all plants at
    // once with no cross-plant chain bleed.
    MeshData mesh = bromesh::meshBranches(emitWorldSegments(world), static_cast<int>(sides));
    bromesh::generateTangents(mesh);
    return mesh;
}

namespace {

// Append this plant's segments to `out` and update `moduleAttachSeg` —
// indexed by module-in-this-plant, value is the index into `out` of the
// segment that terminates at the module's attach terminal (i.e. the
// segment a child module's root edge should set as its parent). Returns
// the number of segments appended.
size_t emitPlantSegmentsInto(const Plant& plant,
                             std::vector<bromesh::BranchSegment>& out) {
    const size_t startSize = out.size();

    // Per-module: prototype-node-index → segment-index-in-`out` of the
    // segment terminating at that node. Built incrementally as each
    // module's edges emit.
    std::vector<std::vector<int32_t>> nodeToSeg(plant.modules.size());

    for (size_t mi = 0; mi < plant.modules.size(); ++mi) {
        const auto& m = plant.modules[mi];
        if (!m.prototype) continue;

        const auto depth = nodeDepths(*m.prototype);
        uint32_t maxDepth = 0;
        for (uint32_t d : depth) if (d > maxDepth) maxDepth = d;
        const float invMaxDepth = (maxDepth > 0)
            ? 1.0f / static_cast<float>(maxDepth) : 0.0f;

        const float tipR  = 0.5f * plant.species.leafDiameter;
        const float rootR = std::max(tipR, 0.5f * m.diameter);

        auto radiusForNode = [&](uint32_t idx) -> float {
            if (idx >= depth.size() || maxDepth == 0) return rootR;
            const float t = static_cast<float>(depth[idx]) * invMaxDepth;
            return rootR + (tipR - rootR) * t;
        };

        // Pre-size the per-module lookup; -1 means "no segment terminates
        // here yet." Multiple edges may terminate at the same node only
        // for malformed prototypes; we just overwrite, last-write-wins.
        nodeToSeg[mi].assign(m.prototype->nodes.size(), -1);

        // Resolve the parent module's segment that this module's root
        // edges attach to. UINT32_MAX module-parent means plant root.
        int32_t moduleParentSeg = -1;
        if (m.parent != UINT32_MAX && m.parent < plant.modules.size()) {
            const auto& pm = plant.modules[m.parent];
            if (pm.prototype && m.parentAttachTerminal < pm.prototype->terminalNodes.size()) {
                const uint32_t attachNode = pm.prototype->terminalNodes[m.parentAttachTerminal];
                if (attachNode < nodeToSeg[m.parent].size()) {
                    moduleParentSeg = nodeToSeg[m.parent][attachNode];
                }
            }
        }

        for (const auto& e : m.prototype->edges) {
            bromesh::BranchSegment seg;
            seg.from   = worldNodePos(plant.species, m, e.a);
            seg.to     = worldNodePos(plant.species, m, e.b);
            // Representative thickness for the whole segment: the mean of
            // the (thicker) parent-side and (thinner) tip-side radii. Using
            // only e.b would report the tip radius for every segment — every
            // edge's b-node is a max-depth terminal — collapsing the trunk to
            // leaf thickness for any consumer that scatters by seg.radius.
            seg.radius = 0.5f * (radiusForNode(e.a) + radiusForNode(e.b));

            // Parent: prefer an earlier segment within this module that
            // terminates at e.a. If none (i.e. e.a is the module's root
            // node), fall back to the parent module's attach segment.
            int32_t parentSeg = -1;
            if (e.a < nodeToSeg[mi].size() && nodeToSeg[mi][e.a] >= 0) {
                parentSeg = nodeToSeg[mi][e.a];
            } else if (e.a == m.prototype->rootNode) {
                parentSeg = moduleParentSeg;
            }
            seg.parent = parentSeg;
            seg.depth  = (parentSeg >= 0)
                ? static_cast<int>(out[parentSeg].depth) + 1
                : 0;

            const int32_t idx = static_cast<int32_t>(out.size());
            out.push_back(seg);

            if (e.b < nodeToSeg[mi].size()) {
                nodeToSeg[mi][e.b] = idx;
            }
        }
    }

    return out.size() - startSize;
}

} // namespace

std::vector<bromesh::BranchSegment> emitPlantSegments(const Plant& plant) {
    std::vector<bromesh::BranchSegment> out;
    emitPlantSegmentsInto(plant, out);
    return out;
}

std::vector<bromesh::BranchSegment> emitWorldSegments(const WorldState& world) {
    std::vector<bromesh::BranchSegment> out;
    for (const auto& plant : world.plants) {
        emitPlantSegmentsInto(plant, out);
    }
    return out;
}

namespace {

// Count child modules per module index — the topology gate for
// "terminal module" in the plant's module-tree. A module is terminal
// iff no other module references it as parent.
std::vector<uint32_t> moduleChildCounts(const Plant& plant) {
    std::vector<uint32_t> counts(plant.modules.size(), 0u);
    for (const auto& m : plant.modules) {
        if (m.parent != UINT32_MAX && m.parent < counts.size()) {
            ++counts[m.parent];
        }
    }
    return counts;
}

float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

// Plant-level senescence ramp:
//   0 while plant.age <= species.maxAge,
//   linearly to 1 over the next 20% of maxAge,
//   1 thereafter.
// Centralised so the default `mass` policy and the raw `senescence01`
// scalar in FoliageSample stay in agreement.
float senescenceRamp(const Plant& plant) {
    const float maxAge = plant.species.maxAge;
    if (maxAge <= 0.0f) return 0.0f;
    const float over = plant.age - maxAge;
    if (over <= 0.0f) return 0.0f;
    const float window = maxAge * 0.2f;
    if (window <= 0.0f) return 1.0f;
    return clamp01(over / window);
}

// Build one FoliageSample for a module. Same per-module derivation for
// every edge — the sample doesn't change within a module — so callers
// that iterate by segment can reuse this across all edges of a module.
FoliageSample sampleForModule(const Plant& plant,
                              const BranchModuleInstance& m,
                              bool isTerminal,
                              float senescence) {
    const auto& sp = plant.species;
    FoliageSample s;
    s.isTerminal   = isTerminal;
    s.age01        = (sp.moduleMatureAge > 0.0f)
                       ? std::min(2.0f, std::max(0.0f, m.age / sp.moduleMatureAge))
                       : 0.0f;
    s.vigor01      = (sp.maxVigor > 0.0f)
                       ? clamp01(m.vigor / sp.maxVigor)
                       : 0.0f;
    s.light01         = clamp01(m.light);
    s.lightExposure01 = clamp01(m.lightExposure);
    s.senescence01    = senescence;
    s.twigGrade01     = internal::leafGrade(sp, m.diameter);

    // Default mass policy (matches the FoliageSample doc in mesh_emit.h).
    // Foliage is distributed through the whole crown, not just terminal
    // tips: leaf-area proxy (thin, mature, vigorous branches) modulated by
    // the module's effective light and thinned as the plant senesces.
    // `isTerminal` is still reported but no longer hard-gates mass.
    s.mass = internal::leafAreaProxy(sp, m) * s.light01 * (1.0f - clamp01(senescence));
    return s;
}

// Append this plant's foliage samples to `out`, in lockstep with the
// segment order produced by emitPlantSegmentsInto. Same walk shape:
// modules in topo order, each module's edges in declaration order.
size_t emitPlantFoliageInto(const Plant& plant,
                            std::vector<FoliageSample>& out) {
    const size_t startSize = out.size();
    if (plant.modules.empty()) return 0;

    const auto childCounts = moduleChildCounts(plant);
    const float senescence = senescenceRamp(plant);

    for (size_t mi = 0; mi < plant.modules.size(); ++mi) {
        const auto& m = plant.modules[mi];
        if (!m.prototype) continue;
        const bool isTerminal = (childCounts[mi] == 0u);
        const FoliageSample s = sampleForModule(plant, m, isTerminal, senescence);
        // One sample per edge — broadcast the per-module value.
        for (size_t e = 0; e < m.prototype->edges.size(); ++e) {
            out.push_back(s);
        }
    }
    return out.size() - startSize;
}

} // namespace

std::vector<FoliageSample> emitPlantFoliage(const Plant& plant) {
    std::vector<FoliageSample> out;
    emitPlantFoliageInto(plant, out);
    return out;
}

std::vector<FoliageSample> emitWorldFoliage(const WorldState& world) {
    std::vector<FoliageSample> out;
    for (const auto& plant : world.plants) {
        emitPlantFoliageInto(plant, out);
    }
    return out;
}

namespace {

// Index of the prototype edge whose `b` endpoint is `terminalNode`, or
// SIZE_MAX if no such edge exists (malformed prototype, or `terminalNode`
// isn't actually a leaf of the prototype graph). Used to compute the
// outward direction at an anchor — the direction along the incoming
// segment.
size_t incomingEdgeIndex(const BranchModulePrototype& proto, uint32_t terminalNode) {
    for (size_t i = 0; i < proto.edges.size(); ++i) {
        if (proto.edges[i].b == terminalNode) return i;
    }
    return SIZE_MAX;
}

size_t emitPlantBloomAnchorsInto(const Plant& plant,
                                 std::vector<BloomAnchor>& out) {
    const size_t startSize = out.size();
    if (!plant.flowering || plant.modules.empty()) return 0;

    const auto childCounts = moduleChildCounts(plant);
    const float senescence = senescenceRamp(plant);

    for (size_t mi = 0; mi < plant.modules.size(); ++mi) {
        const auto& m = plant.modules[mi];
        if (!m.prototype) continue;
        if (childCounts[mi] != 0u) continue;  // non-terminal module — no blooms

        const auto& sp = plant.species;
        // Per-module life-state scalars — same definitions as the
        // FoliageSample policy. Centralising would require pulling
        // sampleForModule into a header; the trade-off favors duplication
        // here over surface-area growth.
        const float age01 = (sp.moduleMatureAge > 0.0f)
            ? std::min(2.0f, std::max(0.0f, m.age / sp.moduleMatureAge))
            : 0.0f;
        const float vigor01 = (sp.maxVigor > 0.0f)
            ? clamp01(m.vigor / sp.maxVigor)
            : 0.0f;

        for (uint32_t terminalNode : m.prototype->terminalNodes) {
            if (terminalNode >= m.prototype->nodes.size()) continue;

            BloomAnchor a;
            a.position        = worldNodePos(plant.species, m, terminalNode);
            a.age01           = age01;
            a.vigor01         = vigor01;
            a.senescence01    = senescence;
            a.lightExposure01 = clamp01(m.lightExposure);

            const size_t edgeIdx = incomingEdgeIndex(*m.prototype, terminalNode);
            if (edgeIdx != SIZE_MAX) {
                const auto& e = m.prototype->edges[edgeIdx];
                const bromath::Vec3 fromPos = worldNodePos(plant.species, m, e.a);
                const bromath::Vec3 dir = a.position - fromPos;
                const float len = bromath::vlen(dir);
                if (len > 1e-6f) {
                    a.normal = dir * (1.0f / len);
                }
                // else: leave default +Y normal
            }

            out.push_back(a);
        }
    }
    return out.size() - startSize;
}

} // namespace

std::vector<BloomAnchor> emitPlantBloomAnchors(const Plant& plant) {
    std::vector<BloomAnchor> out;
    emitPlantBloomAnchorsInto(plant, out);
    return out;
}

std::vector<BloomAnchor> emitWorldBloomAnchors(const WorldState& world) {
    std::vector<BloomAnchor> out;
    for (const auto& plant : world.plants) {
        emitPlantBloomAnchorsInto(plant, out);
    }
    return out;
}

} // namespace broflora
