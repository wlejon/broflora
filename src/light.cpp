#include "broflora/light.h"

#include "bromath/scalar.h"
#include "bromath/sphere.h"
#include "bromath/spatial_hash.h"
#include "bromath/vec.h"
#include "internal_geom.h"
#include "internal_spatial.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace broflora {

using bromath::Vec3;
using bromath::lerp;

namespace {

// Find the ShadowGrid cell containing world-space `p`. Returns false if
// outside the grid.
bool shadowCellOf(const ShadowGrid& g, Vec3 p, uint32_t& x, uint32_t& y, uint32_t& z) {
    if (g.cellSize <= 0.0f) return false;
    float fx = (p.x - g.origin.x) / g.cellSize;
    float fy = (p.y - g.origin.y) / g.cellSize;
    float fz = (p.z - g.origin.z) / g.cellSize;
    if (fx < 0.0f || fy < 0.0f || fz < 0.0f) return false;
    x = static_cast<uint32_t>(fx);
    y = static_cast<uint32_t>(fy);
    z = static_cast<uint32_t>(fz);
    return x < g.width && y < g.height && z < g.depth;
}

} // namespace

void evaluateLightAndCollisions(WorldState& world,
                                const bromath::SpatialHash3D& index) {
    // Reset the shadow grid to full sun each tick.
    std::fill(world.shadow.qg.begin(), world.shadow.qg.end(), 1.0f);

    // --- 1. Collisions — for each module, broad-phase the neighbourhood
    // via the spatial hash (built by step() before this call) and only
    // call the closed-form sphere intersection on candidates. Pairs are
    // deduplicated by packed-id ordering so each (i,j) overlap is
    // counted once and applied to both sides, matching the previous
    // O(N²) walk's symmetric contribution.
    //
    // m.light is repurposed as the f_collisions accumulator here and
    // finalised to exp(-fc) at the end of step 1. Zero every extent-
    // bearing module first so we don't carry last tick's Q_eff in.
    for (auto& pl : world.plants) {
        for (auto& m : pl.modules) {
            if (m.bboxRadius > 0.0f) m.light = 0.0f;
        }
    }

    std::vector<int32_t> candidates;
    for (size_t p = 0; p < world.plants.size(); ++p) {
        auto& pl = world.plants[p];
        for (size_t i = 0; i < pl.modules.size(); ++i) {
            auto& m = pl.modules[i];
            if (m.bboxRadius <= 0.0f) continue;
            const int32_t selfId =
                internal::packEntryId(static_cast<uint32_t>(p),
                                      static_cast<uint32_t>(i));
            candidates.clear();
            index.radiusQuery(m.bboxCenter, m.bboxRadius, candidates);
            for (int32_t nid : candidates) {
                if (nid <= selfId) continue;  // skip self + already-paired
                uint32_t np, nmi;
                internal::unpackEntryId(nid, np, nmi);
                auto& n = world.plants[np].modules[nmi];
                float v = bromath::sintersectVolume(
                    bromath::Sphere{m.bboxCenter, m.bboxRadius},
                    bromath::Sphere{n.bboxCenter, n.bboxRadius});
                m.light += v;
                n.light += v;
            }
        }
    }

    // Q(u) = exp(-f_collisions(u)). Modules with no extent (just spawned,
    // no bbox yet) get full sun so they don't starve before development
    // runs.
    for (auto& pl : world.plants) {
        for (auto& m : pl.modules) {
            if (m.bboxRadius <= 0.0f) {
                m.light = 1.0f;
            } else {
                m.light = std::exp(-m.light);
            }
        }
    }

    // --- 2. Global shadow stamping. Each module attenuates Q_G of every
    // cell strictly *below* its own cell in the shadow grid. Attenuation
    // factor is exp(-bboxRadius²·k) — a coarse stand-in for a real beam
    // integration. The paper's formulation is plant-specific (each
    // species reads with its own s_tol); we do the read in step 3.
    const float shadowK = 0.5f;  // tuneable falloff
    for (const auto& pl : world.plants) {
        for (const auto& m : pl.modules) {
            uint32_t cx, cy, cz;
            if (!shadowCellOf(world.shadow, m.bboxCenter, cx, cy, cz)) continue;
            float occ = 1.0f - std::exp(-shadowK * m.bboxRadius * m.bboxRadius);
            float attenuate = 1.0f - occ;
            for (uint32_t y = 0; y < cy; ++y) {
                uint32_t idx = shadowIndex(world.shadow, cx, y, cz);
                world.shadow.qg[idx] *= attenuate;
            }
        }
    }

    // --- 3. Effective light per module: Q_eff = lerp(s_tol, 1, Q · Q_G).
    for (auto& pl : world.plants) {
        const float sTol = pl.species.shadeTolerance;
        for (auto& m : pl.modules) {
            float qg = 1.0f;
            uint32_t cx, cy, cz;
            if (shadowCellOf(world.shadow, m.bboxCenter, cx, cy, cz)) {
                qg = world.shadow.qg[shadowIndex(world.shadow, cx, cy, cz)];
            }
            m.light = lerp(sTol, 1.0f, m.light * qg);
        }
    }
}

} // namespace broflora
