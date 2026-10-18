#include "broflora/mesh_emit.h"

#include "bromath/quat.h"
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

// A well-mixed hash of an integer into [0, 1).
float hash01(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<float>((x >> 40) * (1.0 / 16777216.0));
}

// A plant's seed for the emit's own randomness: its origin, which is fixed
// for its life.
uint64_t plantEmitSeed(const Plant& plant) {
    return static_cast<uint64_t>(std::llround(plant.origin.x * 1013.0f)) * 0x9E3779B97F4A7C15ULL
         ^ static_cast<uint64_t>(std::llround(plant.origin.z * 7919.0f));
}

// The parent-prototype node a child module hangs from: its attach
// terminal as a node index when that node ends an edge, else as an index
// into the terminal list (the two conventions emitPlantSegmentsInto
// accepts). UINT32_MAX when neither resolves.
uint32_t attachNodeOf(const BranchModulePrototype& pp, uint32_t attach) {
    if (attach < pp.nodes.size()) {
        for (const auto& e : pp.edges) if (e.b == attach) return attach;
    }
    if (attach < pp.terminalNodes.size()) return pp.terminalNodes[attach];
    return UINT32_MAX;
}

// The kinks (Species::kink): per module, per prototype node, how far the
// emitted node sits from the grown one. Each node off a module's root
// steps sideways from its predecessor by `kink` times the edge between
// them, in a direction hashed from the plant, module and node; everything
// beyond it (the rest of the module, and every module hanging from it)
// carries the step, so the branch bends there and keeps its joints.
std::vector<std::vector<Vec3>> kinkOffsets(const Plant& plant) {
    const size_t nm = plant.modules.size();
    std::vector<std::vector<Vec3>> off(nm);
    const float k = plant.species.kink;
    const uint64_t seed = plantEmitSeed(plant) ^ 0x6b696e6bULL;
    for (size_t mi = 0; mi < nm; ++mi) {
        const auto& m = plant.modules[mi];
        if (!m.prototype) continue;
        const auto& proto = *m.prototype;
        const size_t n = proto.nodes.size();
        Vec3 base{0.0f, 0.0f, 0.0f};
        if (m.parent != UINT32_MAX && m.parent < mi && plant.modules[m.parent].prototype) {
            const uint32_t a = attachNodeOf(*plant.modules[m.parent].prototype, m.parentAttachTerminal);
            if (a < off[m.parent].size()) base = off[m.parent][a];
        }
        auto& o = off[mi];
        o.assign(n, base);
        if (!(k > 0.0f)) continue;
        for (const auto& e : proto.edges) {
            if (e.a >= n || e.b >= n) continue;
            const Vec3 d = worldNodePos(plant.species, m, e.b) - worldNodePos(plant.species, m, e.a);
            const float len = bromath::vlen(d);
            o[e.b] = o[e.a];
            if (len < 1e-6f) continue;
            const Vec3 t = d * (1.0f / len);
            Vec3 e1 = bromath::vcross(t, Vec3{0.0f, 1.0f, 0.0f});
            if (bromath::vdot(e1, e1) < 1e-8f) e1 = bromath::vcross(t, Vec3{1.0f, 0.0f, 0.0f});
            e1 = bromath::vnorm(e1);
            const Vec3 e2 = bromath::vcross(t, e1);
            const uint64_t h = seed + (static_cast<uint64_t>(mi) << 20) * 0x2545F4914F6CDD1DULL + e.b * 0x9E3779B97F4A7C15ULL;
            const float ang = hash01(h) * 6.2831853f;
            const float mag = k * len * (0.4f + 0.6f * hash01(h ^ 0x5bd1e995ULL));
            o[e.b] = o[e.a] + (e1 * std::cos(ang) + e2 * std::sin(ang)) * mag;
        }
    }
    return off;
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

// Smoothly weld / bridge branch fork junctions connecting parent cylinder rings
// to child branch root rings.
void weldBranchJunctions(MeshData& mesh, const std::vector<bromesh::BranchSegment>& segs, int sides) {
    if (segs.empty() || mesh.empty() || sides < 3) return;

    // Topology analysis matching bromesh::meshBranches chain extraction
    std::vector<std::vector<int>> children(segs.size());
    for (size_t i = 0; i < segs.size(); ++i) {
        int p = segs[i].parent;
        if (p >= 0 && static_cast<size_t>(p) < segs.size()) {
            children[p].push_back(static_cast<int>(i));
        }
    }

    auto isChainStart = [&](int i) -> bool {
        int p = segs[i].parent;
        if (p < 0) return true;
        return children[p].size() != 1;
    };

    struct ChainInfo {
        int startSeg;
        int endSeg;
        size_t pathLen;
        size_t vertStart;
        bool capStart;
        bool capEnd;
    };

    std::vector<ChainInfo> chains;
    size_t currentVertOffset = 0;

    // Map each segment to its {chainIdx, ringIdx} in the swept mesh
    std::vector<std::pair<int, int>> segEndRing(segs.size(), {-1, -1});
    std::vector<int> chainOfStartSeg(segs.size(), -1);

    for (size_t i = 0; i < segs.size(); ++i) {
        if (!isChainStart(static_cast<int>(i))) continue;

        size_t pathLen = 1;
        int cur = static_cast<int>(i);
        std::vector<int> segsInChain;
        while (true) {
            const bromesh::BranchSegment& s = segs[cur];
            if (bromath::vdist2(s.from, s.to) > 1e-12f) {
                segsInChain.push_back(cur);
                ++pathLen;
            }
            if (children[cur].size() != 1) break;
            cur = children[cur][0];
        }

        if (pathLen < 2) continue;

        int chainIdx = static_cast<int>(chains.size());
        chainOfStartSeg[i] = chainIdx;

        bool capStart = (segs[i].parent < 0);
        bool capEnd = children[cur].empty();

        chains.push_back({static_cast<int>(i), cur, pathLen, currentVertOffset, capStart, capEnd});

        for (size_t ring = 0; ring < segsInChain.size(); ++ring) {
            segEndRing[segsInChain[ring]] = {chainIdx, static_cast<int>(ring + 1)};
        }

        size_t numRingVerts = pathLen * static_cast<size_t>(sides);
        size_t numCapVerts = (capStart ? 1 : 0) + (capEnd ? 1 : 0);
        currentVertOffset += (numRingVerts + numCapVerts);
    }

    if (currentVertOffset != mesh.vertexCount()) {
        return;
    }

    // For every chain with a valid parent segment, bridge its root ring (ring 0)
    // to the parent segment's end ring.
    for (size_t c = 0; c < chains.size(); ++c) {
        const auto& chain = chains[c];
        int pSeg = segs[chain.startSeg].parent;
        if (pSeg < 0 || static_cast<size_t>(pSeg) >= segs.size()) continue;

        auto pRingInfo = segEndRing[pSeg];
        if (pRingInfo.first < 0) continue;

        const auto& pChain = chains[pRingInfo.first];
        size_t pRingIdx = static_cast<size_t>(pRingInfo.second);
        if (pRingIdx >= pChain.pathLen) continue;

        size_t cBase = chain.vertStart;
        size_t pBase = pChain.vertStart + pRingIdx * static_cast<size_t>(sides);

        int bestDelta = 0;
        float bestDist2 = 1e30f;
        for (int d = 0; d < sides; ++d) {
            float sumD2 = 0.0f;
            for (int p = 0; p < sides; ++p) {
                uint32_t cIdx = static_cast<uint32_t>(cBase + p);
                uint32_t pIdx = static_cast<uint32_t>(pBase + ((p + d) % sides));
                Vec3 cPos{mesh.positions[cIdx * 3], mesh.positions[cIdx * 3 + 1], mesh.positions[cIdx * 3 + 2]};
                Vec3 pPos{mesh.positions[pIdx * 3], mesh.positions[pIdx * 3 + 1], mesh.positions[pIdx * 3 + 2]};
                sumD2 += bromath::vdist2(cPos, pPos);
            }
            if (sumD2 < bestDist2) {
                bestDist2 = sumD2;
                bestDelta = d;
            }
        }

        for (int p = 0; p < sides; ++p) {
            int pNext = (p + 1) % sides;
            int k0 = (p + bestDelta) % sides;
            int k1 = (p + 1 + bestDelta) % sides;

            uint32_t c0 = static_cast<uint32_t>(cBase + p);
            uint32_t c1 = static_cast<uint32_t>(cBase + pNext);
            uint32_t p0 = static_cast<uint32_t>(pBase + k0);
            uint32_t p1 = static_cast<uint32_t>(pBase + k1);

            mesh.indices.push_back(c0);
            mesh.indices.push_back(c1);
            mesh.indices.push_back(p1);

            mesh.indices.push_back(c0);
            mesh.indices.push_back(p1);
            mesh.indices.push_back(p0);
        }
    }
}

// Bake hierarchical wind deformation parameters into vertex colors:
// R = wind sway bend deflection in [0, 1]
// G = normalized branch hierarchy depth in [0, 1]
// B = branch stiffness in [0, 1]
// A = normalized vertical height / canopy envelope in [0, 1]
void bakeWindDeformation(MeshData& mesh,
                         const std::vector<bromesh::BranchSegment>& segs,
                         int sides,
                         float leafDiameter) {
    if (mesh.empty()) return;
    const size_t vc = mesh.vertexCount();
    mesh.colors.resize(vc * 4);

    if (segs.empty()) {
        for (size_t i = 0; i < vc; ++i) {
            mesh.colors[i * 4 + 0] = 0.0f;
            mesh.colors[i * 4 + 1] = 0.0f;
            mesh.colors[i * 4 + 2] = 1.0f;
            mesh.colors[i * 4 + 3] = 1.0f;
        }
        return;
    }

    int maxDepth = 0;
    float maxRadius = 0.0f;
    for (const auto& s : segs) {
        if (s.depth > maxDepth) maxDepth = s.depth;
        if (s.radius > maxRadius) maxRadius = s.radius;
    }
    const float leafR = 0.5f * (leafDiameter > 0.0f ? leafDiameter : 0.02f);
    if (maxRadius < leafR) maxRadius = leafR + 0.05f;

    float minY = 1e30f, maxY = -1e30f;
    for (size_t i = 0; i < vc; ++i) {
        float y = mesh.positions[i * 3 + 1];
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    const float spanY = std::max(1e-4f, maxY - minY);
    const float depthDenom = static_cast<float>(std::max(1, maxDepth + 1));
    const float radiusDenom = std::max(1e-4f, maxRadius - leafR);

    // Topological mapping matching swept chains
    std::vector<std::vector<int>> children(segs.size());
    for (size_t i = 0; i < segs.size(); ++i) {
        int p = segs[i].parent;
        if (p >= 0 && static_cast<size_t>(p) < segs.size()) {
            children[p].push_back(static_cast<int>(i));
        }
    }

    auto isChainStart = [&](int i) -> bool {
        int p = segs[i].parent;
        if (p < 0) return true;
        return children[p].size() != 1;
    };

    size_t currentVertOffset = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (!isChainStart(static_cast<int>(i))) continue;

        std::vector<int> segsInChain;
        int cur = static_cast<int>(i);
        while (true) {
            const bromesh::BranchSegment& s = segs[cur];
            if (bromath::vdist2(s.from, s.to) > 1e-12f) {
                segsInChain.push_back(cur);
            }
            if (children[cur].size() != 1) break;
            cur = children[cur][0];
        }

        if (segsInChain.empty()) continue;

        size_t pathLen = segsInChain.size() + 1;
        bool capStart = (segs[i].parent < 0);
        bool capEnd = children[cur].empty();

        int p0 = segs[i].parent;
        float r0 = (p0 >= 0 && segs[p0].radius > 0.0f) ? segs[p0].radius
                  : (segs[i].radius > 0.0f ? segs[i].radius : leafR);

        // Ring 0
        {
            float effDepth = static_cast<float>(segs[i].depth);
            float normDepth = std::clamp(effDepth / depthDenom, 0.0f, 1.0f);
            float stiffness = std::clamp((r0 - leafR) / radiusDenom, 0.0f, 1.0f);
            float flex = 1.0f - 0.85f * stiffness;

            for (int p = 0; p < sides; ++p) {
                size_t vIdx = currentVertOffset + p;
                if (vIdx < vc) {
                    float y = mesh.positions[vIdx * 3 + 1];
                    float normHeight = std::clamp((y - minY) / spanY, 0.0f, 1.0f);
                    float windBend = std::clamp(normDepth * flex * (0.15f + 0.85f * normHeight), 0.0f, 1.0f);

                    mesh.colors[vIdx * 4 + 0] = windBend;
                    mesh.colors[vIdx * 4 + 1] = normDepth;
                    mesh.colors[vIdx * 4 + 2] = stiffness;
                    mesh.colors[vIdx * 4 + 3] = normHeight;
                }
            }
        }

        // Subsequent rings (r = 1 .. segsInChain.size())
        for (size_t r = 0; r < segsInChain.size(); ++r) {
            int sIdx = segsInChain[r];
            const auto& s = segs[sIdx];
            float effDepth = static_cast<float>(s.depth) + 1.0f;
            float normDepth = std::clamp(effDepth / depthDenom, 0.0f, 1.0f);
            float stiffness = std::clamp((s.radius - leafR) / radiusDenom, 0.0f, 1.0f);
            float flex = 1.0f - 0.85f * stiffness;

            size_t ringBase = currentVertOffset + (r + 1) * static_cast<size_t>(sides);
            for (int p = 0; p < sides; ++p) {
                size_t vIdx = ringBase + p;
                if (vIdx < vc) {
                    float y = mesh.positions[vIdx * 3 + 1];
                    float normHeight = std::clamp((y - minY) / spanY, 0.0f, 1.0f);
                    float windBend = std::clamp(normDepth * flex * (0.15f + 0.85f * normHeight), 0.0f, 1.0f);

                    mesh.colors[vIdx * 4 + 0] = windBend;
                    mesh.colors[vIdx * 4 + 1] = normDepth;
                    mesh.colors[vIdx * 4 + 2] = stiffness;
                    mesh.colors[vIdx * 4 + 3] = normHeight;
                }
            }
        }

        size_t totalRingVerts = pathLen * static_cast<size_t>(sides);
        size_t nextOffset = currentVertOffset + totalRingVerts;

        if (capStart && nextOffset < vc) {
            size_t vIdx = nextOffset++;
            float y = mesh.positions[vIdx * 3 + 1];
            float normHeight = std::clamp((y - minY) / spanY, 0.0f, 1.0f);
            float effDepth = static_cast<float>(segs[i].depth);
            float normDepth = std::clamp(effDepth / depthDenom, 0.0f, 1.0f);
            float stiffness = std::clamp((r0 - leafR) / radiusDenom, 0.0f, 1.0f);
            float flex = 1.0f - 0.85f * stiffness;
            float windBend = std::clamp(normDepth * flex * (0.15f + 0.85f * normHeight), 0.0f, 1.0f);

            mesh.colors[vIdx * 4 + 0] = windBend;
            mesh.colors[vIdx * 4 + 1] = normDepth;
            mesh.colors[vIdx * 4 + 2] = stiffness;
            mesh.colors[vIdx * 4 + 3] = normHeight;
        }

        if (capEnd && nextOffset < vc) {
            size_t vIdx = nextOffset++;
            int lastSegIdx = segsInChain.back();
            const auto& lastSeg = segs[lastSegIdx];
            float y = mesh.positions[vIdx * 3 + 1];
            float normHeight = std::clamp((y - minY) / spanY, 0.0f, 1.0f);
            float effDepth = static_cast<float>(lastSeg.depth) + 1.0f;
            float normDepth = std::clamp(effDepth / depthDenom, 0.0f, 1.0f);
            float stiffness = std::clamp((lastSeg.radius - leafR) / radiusDenom, 0.0f, 1.0f);
            float flex = 1.0f - 0.85f * stiffness;
            float windBend = std::clamp(normDepth * flex * (0.15f + 0.85f * normHeight), 0.0f, 1.0f);

            mesh.colors[vIdx * 4 + 0] = windBend;
            mesh.colors[vIdx * 4 + 1] = normDepth;
            mesh.colors[vIdx * 4 + 2] = stiffness;
            mesh.colors[vIdx * 4 + 3] = normHeight;
        }

        currentVertOffset = nextOffset;
    }

    for (size_t i = currentVertOffset; i < vc; ++i) {
        float y = mesh.positions[i * 3 + 1];
        float normHeight = std::clamp((y - minY) / spanY, 0.0f, 1.0f);
        mesh.colors[i * 4 + 0] = 0.5f * normHeight;
        mesh.colors[i * 4 + 1] = 0.5f;
        mesh.colors[i * 4 + 2] = 0.5f;
        mesh.colors[i * 4 + 3] = normHeight;
    }
}

} // namespace

MeshData emitPlantMesh(const Plant& plant, uint32_t sides) {
    auto segs = emitPlantSegments(plant);
    if (segs.empty()) return {};
    MeshData mesh = bromesh::meshBranches(segs, static_cast<int>(sides));
    if (mesh.empty()) return {};

    weldBranchJunctions(mesh, segs, static_cast<int>(sides));
    bromesh::computeNormals(mesh);
    bromesh::generateTangents(mesh);
    bakeWindDeformation(mesh, segs, static_cast<int>(sides), plant.species.leafDiameter);
    bakeVegetation(mesh, segs, emitPlantSway(plant), static_cast<int>(sides));
    return mesh;
}

MeshData emitWorldMesh(const WorldState& world, uint32_t sides) {
    auto segs = emitWorldSegments(world);
    if (segs.empty()) return {};
    MeshData mesh = bromesh::meshBranches(segs, static_cast<int>(sides));
    if (mesh.empty()) return {};

    weldBranchJunctions(mesh, segs, static_cast<int>(sides));
    bromesh::computeNormals(mesh);
    bromesh::generateTangents(mesh);
    bakeWindDeformation(mesh, segs, static_cast<int>(sides), 0.02f);
    bakeVegetation(mesh, segs, emitWorldSway(world), static_cast<int>(sides));
    return mesh;
}


namespace {

// Append this plant's segments to `out` and update `moduleAttachSeg` —
// indexed by module-in-this-plant, value is the index into `out` of the
// segment that terminates at the module's attach terminal (i.e. the
// segment a child module's root edge should set as its parent). Returns
// the number of segments appended.
// The skeleton's sag under its own load (Species::sag), on segments
// [start, end) of `out` (parents before children, indices absolute). Each
// joint (a segment's start) turns the segment and everything beyond it
// toward hanging by M / k times `sag`, small-deflection beam statics: M the
// load beyond it times its lever across (wood at kSagWood N/m^3, and
// kSagLeaves N/m on the shoots under kSagLeafRadii leaf radii, where the
// leaves are) about the joint, k the segment's stiffness E I / L (I = pi
// r^4 / 4, E green wood's), never past straight down. Worked root first:
// each segment turned with every joint below it, then by its own.
void sagSegments(const Species& sp, float leafR, std::vector<bromesh::BranchSegment>& out, size_t start) {
    constexpr float kSagWood = 700.0f * 9.81f;   // green wood's weight, N/m^3
    constexpr float kSagLeaves = 2.0f;           // a leafy shoot's leaves, N/m
    constexpr float kSagLeafRadii = 3.0f;
    constexpr float kSagE = 1.0e10f;             // green wood's modulus, Pa
    const size_t n = out.size() - start;
    if (n == 0 || !(sp.sag > 0.0f)) return;
    const auto parentOf = [&](size_t i) -> int64_t {
        const int64_t p = out[start + i].parent;
        return p >= static_cast<int64_t>(start) && p < static_cast<int64_t>(start + i) ? p - static_cast<int64_t>(start)
                                                                                       : -1;
    };
    // Each segment's weight and its middle, and the moment about each
    // joint of everything beyond it (its own included): a segment adds its
    // weight times its lever across to every joint below it.
    std::vector<float> moment(n, 0.0f);
    // How far each segment's end is from its furthest tip (children after
    // parents, so tips first from the back), and the plant's height.
    std::vector<float> reach(n, 0.0f);
    float lo = std::numeric_limits<float>::infinity(), hi = -lo;
    for (size_t j = n; j-- > 0;) {
        const auto& s = out[start + j];
        lo = std::min({lo, s.from.y, s.to.y});
        hi = std::max({hi, s.from.y, s.to.y});
        const int64_t p = parentOf(j);
        if (p >= 0)
            reach[static_cast<size_t>(p)] =
                std::max(reach[static_cast<size_t>(p)], reach[j] + bromath::vlen(s.to - s.from));
    }
    const float whip = 0.15f * std::max(hi - lo, 0.1f);
    // The leader, the way from the root to the highest tip, stands.
    std::vector<char> leader(n, 0);
    size_t top = 0;
    for (size_t j = 1; j < n; ++j)
        if (out[start + j].to.y > out[start + top].to.y) top = j;
    for (int64_t i = static_cast<int64_t>(top); i >= 0; i = parentOf(static_cast<size_t>(i)))
        leader[static_cast<size_t>(i)] = 1;
    for (size_t j = 0; j < n; ++j) {
        const auto& s = out[start + j];
        const float L = bromath::vlen(s.to - s.from);
        const float w = kSagWood * bromath::PI * s.radius * s.radius * L +
                        (s.radius < kSagLeafRadii * leafR ? kSagLeaves * L : 0.0f);
        const Vec3 mid = (s.from + s.to) * 0.5f;
        for (int64_t i = static_cast<int64_t>(j); i >= 0; i = parentOf(static_cast<size_t>(i))) {
            const Vec3 d = mid - out[start + static_cast<size_t>(i)].from;
            moment[static_cast<size_t>(i)] += w * std::hypot(d.x, d.z);
        }
    }
    // Root first: a segment's start follows its parent's end, its way turns
    // with its parent's (the rotation carried), then by its own joint's.
    const Vec3 down{0.0f, -1.0f, 0.0f};
    std::vector<bromath::Quat> turn(n, bromath::qidentity());
    std::vector<Vec3> oldTo(n);
    for (size_t i = 0; i < n; ++i) oldTo[i] = out[start + i].to;
    for (size_t i = 0; i < n; ++i) {
        auto& s = out[start + i];
        const int64_t p = parentOf(i);
        const bromath::Quat q = p >= 0 ? turn[static_cast<size_t>(p)] : bromath::qidentity();
        const Vec3 off = s.to - s.from;
        const float L = bromath::vlen(off);
        Vec3 from = s.from;
        if (p >= 0) {
            const auto& ps = out[start + static_cast<size_t>(p)];
            from = ps.to + bromath::qrotate(q, s.from - oldTo[static_cast<size_t>(p)]);
        }
        Vec3 dir = L > 1e-9f ? bromath::qrotate(q, off * (1.0f / L)) : down;
        bromath::Quat t = q;
        const float r = std::max(s.radius, 1e-4f);
        const float k = kSagE * bromath::PI * r * r * r * r * 0.25f / std::max(L, 1e-3f);
        // Only the young shoots, within `whip` of their furthest tip (fading
        // out by twice it): the pipe model's are far thicker than a willow's
        // whips (so `sag` is large), and the limbs carrying them stand, as
        // does the leader.
        const float young =
            leader[i] ? 0.0f : 1.0f - std::clamp((reach[i] + L - whip) / whip, 0.0f, 1.0f);
        const float theta = sp.sag * young * moment[i] / k;
        const Vec3 axis = bromath::vcross(dir, down);
        const float al = bromath::vlen(axis);
        if (theta > 0.0f && al > 1e-5f) {
            const float toDown = std::acos(std::clamp(bromath::vdot(dir, down), -1.0f, 1.0f));
            const bromath::Quat j = bromath::qaxisAngle(axis * (1.0f / al), std::min(theta, toDown));
            dir = bromath::qrotate(j, dir);
            t = bromath::qmul(j, q);
        }
        turn[i] = t;
        s.from = from;
        s.to = from + dir * L;
    }
}

size_t emitPlantSegmentsInto(const Plant& plant,
                             std::vector<bromesh::BranchSegment>& out) {
    const size_t startSize = out.size();
    const size_t numModules = plant.modules.size();
    if (numModules == 0) return 0;

    const float pe = plant.species.pipeExp > 0.0f ? plant.species.pipeExp : 2.5f;
    const float invPe = 1.0f / pe;
    const float leafR = 0.5f * (plant.species.leafDiameter > 0.0f ? plant.species.leafDiameter : 0.02f);
    const auto kinks = kinkOffsets(plant);

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

        // Trunk root flare / buttress swelling for the root module (m.parent == UINT32_MAX).
        // Naturally widens the base radius at soil level (node 0) by ~1.40x (1.35x - 1.5x)
        // and tapers smoothly upward over the first ~20% of trunk height, eliminating the
        // stark "telephone pole" cylinder look at the terrain interface.
        if (m.parent == UINT32_MAX && numNodes > 0) {
            const uint32_t rootNodeIdx = proto.rootNode < numNodes ? proto.rootNode : 0u;
            const float rootY = proto.nodes[rootNodeIdx].position.y;
            float maxSpanY = 0.0f;
            for (size_t i = 0; i < numNodes; ++i) {
                maxSpanY = std::max(maxSpanY, proto.nodes[i].position.y - rootY);
            }
            if (maxSpanY <= 1e-4f) {
                for (size_t i = 0; i < numNodes; ++i) {
                    float d = bromath::vlen(proto.nodes[i].position - proto.nodes[rootNodeIdx].position);
                    maxSpanY = std::max(maxSpanY, d);
                }
            }
            const float flareBaseScale = 1.40f;
            const float flareZone = 0.20f;
            for (size_t i = 0; i < numNodes; ++i) {
                float h = proto.nodes[i].position.y - rootY;
                float u = (maxSpanY > 1e-4f) ? std::clamp(h / maxSpanY, 0.0f, 1.0f) : 0.0f;
                if (u < flareZone) {
                    float t = u / flareZone;
                    float smoothT = (1.0f - t) * (1.0f - t);
                    float flareFactor = 1.0f + (flareBaseScale - 1.0f) * smoothT;
                    nodeRadius[i] *= flareFactor;
                }
            }
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
            seg.from   = worldNodePos(plant.species, m, e.a) + (e.a < kinks[mi].size() ? kinks[mi][e.a] : Vec3{});
            seg.to     = worldNodePos(plant.species, m, e.b) + (e.b < kinks[mi].size() ? kinks[mi][e.b] : Vec3{});
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

    sagSegments(plant.species, leafR, out, startSize);
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
    // Foliage is distributed on thin, mature, vigorous shoots.
    // Non-terminal modules (structural limbs that have branched) have heavy attenuation.
    const float termFactor = isTerminal ? 1.0f : 0.20f;
    s.mass = internal::leafAreaProxy(sp, m) * s.light01 * (1.0f - clamp01(senescence)) * termFactor;
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

float foliageDensity(const FoliageSample& f) {
    const float exposure = 0.12f + 0.88f * f.lightExposure01;
    const float maturity = std::min(1.0f, f.age01);
    const float alive    = 1.0f - f.senescence01;
    float stem           = f.twigGrade01 * f.twigGrade01;
    if (!f.isTerminal) stem *= 0.10f;
    return exposure * maturity * alive * stem;
}

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
                                 std::vector<BloomAnchor>& out,
                                 size_t segBase) {
    const size_t startSize = out.size();
    if (!plant.flowering || plant.modules.empty()) return 0;

    const auto childCounts = moduleChildCounts(plant);
    const float senescence = senescenceRamp(plant);
    const auto kinks = kinkOffsets(plant);
    auto nodePos = [&](size_t mi, uint32_t node) {
        const auto& km = kinks[mi];
        return worldNodePos(plant.species, plant.modules[mi], node) + (node < km.size() ? km[node] : Vec3{});
    };

    // emitPlantSegmentsInto emits every edge of every module that has a
    // prototype, module by module: module mi's edge e is segment
    // segBase + (edges of the modules before it) + e.
    size_t moduleSegBase = segBase;
    for (size_t mi = 0; mi < plant.modules.size(); ++mi) {
        const auto& m = plant.modules[mi];
        if (!m.prototype) continue;
        const size_t thisBase = moduleSegBase;
        moduleSegBase += m.prototype->edges.size();
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
            a.position        = nodePos(mi, terminalNode);
            a.age01           = age01;
            a.vigor01         = vigor01;
            a.senescence01    = senescence;
            a.lightExposure01 = clamp01(m.lightExposure);

            const size_t edgeIdx = incomingEdgeIndex(*m.prototype, terminalNode);
            if (edgeIdx != SIZE_MAX) {
                a.segment = static_cast<int32_t>(thisBase + edgeIdx);
                const auto& e = m.prototype->edges[edgeIdx];
                const bromath::Vec3 fromPos = nodePos(mi, e.a);
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

namespace {

size_t plantSegmentCount(const Plant& plant) {
    size_t n = 0;
    for (const auto& m : plant.modules) {
        if (m.prototype && !m.prototype->nodes.empty()) n += m.prototype->edges.size();
    }
    return n;
}

} // namespace

std::vector<BloomAnchor> emitPlantBloomAnchors(const Plant& plant) {
    std::vector<BloomAnchor> out;
    emitPlantBloomAnchorsInto(plant, out, 0);
    return out;
}

std::vector<BloomAnchor> emitWorldBloomAnchors(const WorldState& world) {
    std::vector<BloomAnchor> out;
    size_t segBase = 0;
    for (const auto& plant : world.plants) {
        emitPlantBloomAnchorsInto(plant, out, segBase);
        segBase += plantSegmentCount(plant);
    }
    return out;
}

} // namespace broflora
