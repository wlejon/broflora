// The foliage samples and bloom anchors of a plant, in lockstep with the
// segments mesh_emit.cpp's emitPlantSegmentsInto walks (declared in
// include/broflora/mesh_emit.h).

#include "broflora/mesh_emit.h"

#include "bromath/vec.h"
#include "internal_foliage.h"
#include "internal_kinks.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace broflora {

using bromath::Vec3;
using internal::kinkOffsets;
using internal::worldNodePos;

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

size_t plantSegmentCount(const Plant& plant) {
    size_t n = 0;
    for (const auto& m : plant.modules) {
        if (m.prototype && !m.prototype->nodes.empty()) n += m.prototype->edges.size();
    }
    return n;
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
