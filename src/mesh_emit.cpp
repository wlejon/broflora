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
    MeshData mesh = bromesh::meshBranches(emitWorldSegments(world), static_cast<int>(sides));
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
    const size_t numModules = plant.modules.size();
    if (numModules == 0) return 0;

    const float pe = plant.species.pipeExp > 0.0f ? plant.species.pipeExp : 2.5f;
    const float invPe = 1.0f / pe;
    const float leafR = 0.5f * (plant.species.leafDiameter > 0.0f ? plant.species.leafDiameter : 0.02f);

    // Map each (parent_module_idx, attach_terminal_node) to the list of child module indices.
    std::vector<std::vector<std::vector<uint32_t>>> attachedChildren(numModules);
    for (size_t mi = 0; mi < numModules; ++mi) {
        if (plant.modules[mi].prototype) {
            attachedChildren[mi].resize(plant.modules[mi].prototype->nodes.size());
        }
    }
    for (size_t ci = 0; ci < numModules; ++ci) {
        const auto& ch = plant.modules[ci];
        if (ch.parent != UINT32_MAX && ch.parent < numModules) {
            const auto& pm = plant.modules[ch.parent];
            if (pm.prototype && !attachedChildren[ch.parent].empty()) {
                uint32_t attachNode = ch.parentAttachTerminal;
                bool foundInTerminals = false;
                if (attachNode < pm.prototype->nodes.size()) {
                    for (uint32_t t : pm.prototype->terminalNodes) {
                        if (t == attachNode) { foundInTerminals = true; break; }
                    }
                }
                if (!foundInTerminals && attachNode < pm.prototype->terminalNodes.size()) {
                    attachNode = pm.prototype->terminalNodes[attachNode];
                }
                if (attachNode < attachedChildren[ch.parent].size()) {
                    attachedChildren[ch.parent][attachNode].push_back(static_cast<uint32_t>(ci));
                }
            }
        }
    }

    // Per-module: prototype-node-index → segment-index-in-`out` of the
    // segment terminating at that node. Built incrementally as each
    // module's edges emit.
    std::vector<std::vector<int32_t>> nodeToSeg(numModules);

    for (size_t mi = 0; mi < numModules; ++mi) {
        const auto& m = plant.modules[mi];
        if (!m.prototype) continue;
        const auto& proto = *m.prototype;
        const size_t numNodes = proto.nodes.size();
        if (numNodes == 0) continue;

        const auto depth = nodeDepths(proto);

        // Reachable terminal nodes for each node in proto graph.
        std::vector<std::vector<uint32_t>> reach(numNodes);
        for (uint32_t tNode : proto.terminalNodes) {
            if (tNode < numNodes) {
                reach[tNode].push_back(tNode);
            }
        }
        for (size_t ei = proto.edges.size(); ei-- > 0; ) {
            uint32_t a = proto.edges[ei].a, b = proto.edges[ei].b;
            if (a > b) std::swap(a, b);
            if (a < numNodes && b < numNodes) {
                for (uint32_t t : reach[b]) {
                    if (std::find(reach[a].begin(), reach[a].end(), t) == reach[a].end()) {
                        reach[a].push_back(t);
                    }
                }
            }
        }
        for (size_t i = 0; i < numNodes; ++i) {
            if (reach[i].empty()) {
                reach[i] = proto.terminalNodes;
            }
        }

        const float rootR = std::max(leafR, 0.5f * m.diameter);

        // Pipe-model tip radius for each terminal node:
        // tipR(terminal) = ( sum_{child attached to terminal} (0.5 * child.diameter)^pipeExp )^(1 / pipeExp)
        // If no children are attached to that terminal node: tipR(terminal) = 0.5 * species.leafDiameter.
        std::vector<float> tipR(numNodes, leafR);
        for (uint32_t tNode : proto.terminalNodes) {
            if (tNode >= numNodes) continue;
            const auto& chList = (tNode < attachedChildren[mi].size())
                ? attachedChildren[mi][tNode]
                : std::vector<uint32_t>{};
            if (chList.empty()) {
                tipR[tNode] = leafR;
            } else {
                float sumChildR_pe = 0.0f;
                for (uint32_t ci : chList) {
                    float childR = 0.5f * plant.modules[ci].diameter;
                    sumChildR_pe += std::pow(std::max(leafR, childR), pe);
                }
                float baseTipR = std::pow(sumChildR_pe, invPe);
                // Subtle branch collar / flare swelling at junctions to avoid harsh geometric pinches.
                float collarFactor = (chList.size() >= 2) ? 1.05f : 1.0f;
                tipR[tNode] = baseTipR * collarFactor;
            }
        }

        // Partition rootR across terminals according to pipe-model proportions.
        float sumTipR_pe = 0.0f;
        for (uint32_t tNode : proto.terminalNodes) {
            if (tNode < numNodes) {
                sumTipR_pe += std::pow(tipR[tNode], pe);
            }
        }

        std::vector<float> rootR_T(numNodes, 0.0f);
        for (uint32_t tNode : proto.terminalNodes) {
            if (tNode >= numNodes) continue;
            if (sumTipR_pe > 1e-12f) {
                float fraction = std::pow(tipR[tNode], pe) / sumTipR_pe;
                rootR_T[tNode] = std::pow(fraction, invPe) * rootR;
            } else {
                float fraction = 1.0f / static_cast<float>(std::max(1u, (uint32_t)proto.terminalNodes.size()));
                rootR_T[tNode] = std::pow(fraction, invPe) * rootR;
            }
        }

        // Continuous radius for each node in the module.
        std::vector<float> nodeRadius(numNodes, rootR);
        for (size_t i = 0; i < numNodes; ++i) {
            if (reach[i].empty()) {
                nodeRadius[i] = rootR;
                continue;
            }
            float sumR_pe = 0.0f;
            for (uint32_t t : reach[i]) {
                float maxD = static_cast<float>(depth[t]);
                float curD = static_cast<float>(depth[i]);
                float tNorm = (maxD > 0.0f) ? (curD / maxD) : 0.0f;
                tNorm = std::min(1.0f, std::max(0.0f, tNorm));
                float r_t = rootR_T[t] + (tipR[t] - rootR_T[t]) * tNorm;
                sumR_pe += std::pow(std::max(0.0f, r_t), pe);
            }
            nodeRadius[i] = std::pow(sumR_pe, invPe);
        }

        // Pre-size the per-module lookup; -1 means "no segment terminates
        // here yet."
        nodeToSeg[mi].assign(numNodes, -1);

        // Resolve the parent module's segment that this module's root
        // edges attach to. UINT32_MAX module-parent means plant root.
        int32_t moduleParentSeg = -1;
        if (m.parent != UINT32_MAX && m.parent < numModules) {
            const auto& pm = plant.modules[m.parent];
            if (pm.prototype && !nodeToSeg[m.parent].empty()) {
                uint32_t attachNode = m.parentAttachTerminal;
                if (attachNode < nodeToSeg[m.parent].size() && nodeToSeg[m.parent][attachNode] >= 0) {
                    moduleParentSeg = nodeToSeg[m.parent][attachNode];
                } else if (attachNode < pm.prototype->terminalNodes.size()) {
                    uint32_t tNode = pm.prototype->terminalNodes[attachNode];
                    if (tNode < nodeToSeg[m.parent].size() && nodeToSeg[m.parent][tNode] >= 0) {
                        moduleParentSeg = nodeToSeg[m.parent][tNode];
                    }
                }
            }
        }

        for (const auto& e : proto.edges) {
            bromesh::BranchSegment seg;
            seg.from   = worldNodePos(plant.species, m, e.a);
            seg.to     = worldNodePos(plant.species, m, e.b);
            seg.radius = (e.b < nodeRadius.size()) ? nodeRadius[e.b] : leafR;

            // Parent: prefer an earlier segment within this module that
            // terminates at e.a. If none (i.e. e.a is the module's root
            // node), fall back to the parent module's attach segment.
            int32_t parentSeg = -1;
            if (e.a < nodeToSeg[mi].size() && nodeToSeg[mi][e.a] >= 0) {
                parentSeg = nodeToSeg[mi][e.a];
            } else if (e.a == proto.rootNode) {
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
