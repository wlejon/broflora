#include "broflora/light.h"

#include "bromath/scalar.h"
#include "bromath/sphere.h"
#include "bromath/spatial_hash.h"
#include "bromath/vec.h"
#include "internal_foliage.h"
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

    // --- 2. Global shadow stamping — Beer-Lambert beam integration along
    // the sun direction (paper §3.1). Sun is treated as a parallel beam
    // from +Y (straight down) for now; supporting an arbitrary sunDir on
    // GlobalClimate is a separate scope. Each module deposits an optical
    // depth κ_u = shadowK·r² into every shadow cell its xz-projection
    // covers at a lower y than the module itself. Final Q_G = exp(-Στ) so
    // stacked canopy composes correctly (multiplicative transmission /
    // additive optical depth), and a module wider than one shadow cell
    // shades the cells around it instead of only its own column.
    // Optical depth per module has a small woody term from the branch
    // bbox (so even a bare crown casts some shade) plus the dominant leaf-
    // area term — foliage is what closes a canopy. The leaf term reuses
    // the leaf-area proxy that drives the emitted FoliageSample mass, so
    // simulated shade and rendered leaves agree, and overlapping leafy
    // crowns compound into a shaded understory.
    const float shadowK     = 0.5f;  // woody (branch) extinction
    const float leafShadowK = 1.5f;  // leaf-area extinction (canopy closer)
    const size_t totalCells = world.shadow.qg.size();
    std::vector<float> depTau(totalCells, 0.0f);
    std::vector<float> tau(totalCells, 0.0f);
    const float cellSize = world.shadow.cellSize;
    const int W = static_cast<int>(world.shadow.width);
    const int H = static_cast<int>(world.shadow.height);
    const int D = static_cast<int>(world.shadow.depth);
    for (const auto& pl : world.plants) {
        for (const auto& m : pl.modules) {
            if (m.bboxRadius <= 0.0f) continue;
            uint32_t cx, cy, cz;
            if (!shadowCellOf(world.shadow, m.bboxCenter, cx, cy, cz)) continue;
            if (cy == 0) continue;  // nothing below to shade

            const float opticalDepth = shadowK * m.bboxRadius * m.bboxRadius
                + leafShadowK * internal::leafAreaProxy(pl.species, m);
            const float rCells       = m.bboxRadius / cellSize;
            const int   rxz          = static_cast<int>(std::ceil(rCells));
            const float r2Cells      = rCells * rCells;
            const uint32_t topY      = cy - 1;

            for (int dx = -rxz; dx <= rxz; ++dx) {
                const int nx = static_cast<int>(cx) + dx;
                if (nx < 0 || nx >= W) continue;
                for (int dz = -rxz; dz <= rxz; ++dz) {
                    if (dx * dx + dz * dz > r2Cells) continue;
                    const int nz = static_cast<int>(cz) + dz;
                    if (nz < 0 || nz >= D) continue;
                    uint32_t idx = shadowIndex(world.shadow,
                                               static_cast<uint32_t>(nx),
                                               topY,
                                               static_cast<uint32_t>(nz));
                    depTau[idx] += opticalDepth;
                }
            }
        }
    }
    for (int nx = 0; nx < W; ++nx) {
        for (int nz = 0; nz < D; ++nz) {
            float accum = 0.0f;
            for (int y = H - 1; y >= 0; --y) {
                uint32_t idx = shadowIndex(world.shadow,
                                           static_cast<uint32_t>(nx),
                                           static_cast<uint32_t>(y),
                                           static_cast<uint32_t>(nz));
                accum += depTau[idx];
                tau[idx] = accum;
            }
        }
    }
    for (size_t i = 0; i < world.shadow.qg.size(); ++i) {
        world.shadow.qg[i] = std::exp(-tau[i]);
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
            // Raw illumination (collision self-shading × global shadow) before
            // the shade-tolerance lerp — stashed for renderers that want the
            // true shadow gradient; the sim keeps using the lerped Q_eff.
            m.lightExposure = m.light * qg;
            m.light = lerp(sTol, 1.0f, m.lightExposure);
        }
    }
}

} // namespace broflora
