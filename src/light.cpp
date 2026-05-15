#include "broflora/light.h"

#include "bromath/scalar.h"
#include "bromath/sphere.h"
#include "bromath/vec.h"
#include "internal_geom.h"

#include <algorithm>
#include <cmath>

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

void evaluateLightAndCollisions(WorldState& world) {
    // Reset the shadow grid to full sun each tick.
    std::fill(world.shadow.qg.begin(), world.shadow.qg.end(), 1.0f);

    // --- 1. Collisions — pairwise sphere-intersection sums across every
    // module in every plant (O(N²) over the whole world). Fine for the
    // foundation; swap in a BVH later if N grows past a few thousand.
    struct CollideSphere { Vec3 c; float r; size_t plant; size_t mod; };
    std::vector<CollideSphere> spheres;
    for (size_t p = 0; p < world.plants.size(); ++p) {
        const auto& pl = world.plants[p];
        for (size_t i = 0; i < pl.modules.size(); ++i) {
            const auto& m = pl.modules[i];
            if (m.bboxRadius > 0.0f) {
                spheres.push_back({m.bboxCenter, m.bboxRadius, p, i});
            }
        }
    }

    std::vector<float> collisions(spheres.size(), 0.0f);
    for (size_t i = 0; i < spheres.size(); ++i) {
        for (size_t j = i + 1; j < spheres.size(); ++j) {
            float v = bromath::sintersectVolume(
                bromath::Sphere{spheres[i].c, spheres[i].r},
                bromath::Sphere{spheres[j].c, spheres[j].r});
            collisions[i] += v;
            collisions[j] += v;
        }
    }

    // Q(u) = exp(-f_collisions(u)). Stash on the module.
    for (size_t k = 0; k < spheres.size(); ++k) {
        auto& m = world.plants[spheres[k].plant].modules[spheres[k].mod];
        m.light = std::exp(-collisions[k]);
    }

    // For modules with zero radius (just spawned, no extent yet), give
    // them full sun so they don't starve before development runs.
    for (auto& pl : world.plants) {
        for (auto& m : pl.modules) {
            if (m.bboxRadius <= 0.0f) m.light = 1.0f;
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
