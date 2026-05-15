#include "broflora/senescence.h"

#include "broflora/rng.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace broflora {

namespace {

// Climate adaptation σ — 2D Gaussian over (T - T_A), (P - P_A).
float climateAdapt(const Species& sp, const GlobalClimate& clim, float elevation) {
    float T = climateTempAt(clim, elevation);
    float P = clim.annualPrecip;
    float dT = (T - sp.climateOptT) / (sp.climateSigT > 0.0f ? sp.climateSigT : 1.0f);
    float dP = (P - sp.climateOptP) / (sp.climateSigP > 0.0f ? sp.climateSigP : 1.0f);
    return std::exp(-0.5f * (dT * dT + dP * dP));
}

// 2D-grid lookup that returns false when (x,z) is out of footprint.
template <typename T>
bool sampleGrid(const GridFootprint2D& fp, const std::vector<T>& cells,
                Vec3 world, T& out) {
    if (fp.cellSize <= 0.0f || fp.width == 0 || fp.depth == 0) return false;
    float fx = (world.x - fp.origin.x) / fp.cellSize;
    float fz = (world.z - fp.origin.y) / fp.cellSize;
    if (fx < 0.0f || fz < 0.0f) return false;
    uint32_t ix = static_cast<uint32_t>(fx);
    uint32_t iz = static_cast<uint32_t>(fz);
    if (ix >= fp.width || iz >= fp.depth) return false;
    out = cells[ix * fp.depth + iz];
    return true;
}

float terrainHeightAt(const TerrainMap& t, Vec3 world, float fallback) {
    float h;
    if (sampleGrid<float>(t.footprint, t.height, world, h)) return h;
    return fallback;
}

bool soilBlockedAt(const SoilMap& s, Vec3 world) {
    uint8_t v;
    if (sampleGrid<uint8_t>(s.footprint, s.blocked, world, v)) return v != 0;
    return false;
}

// Build a single-module seedling using the first valid prototype found
// in the Voronoi library. Returns false if no prototype available.
bool makeSeedling(const WorldState& world, const Species& species, Vec3 origin,
                  Plant& out) {
    const BranchModulePrototype* proto = nullptr;
    for (const auto& site : world.voronoi) {
        if (site.prototype) { proto = site.prototype; break; }
    }
    if (!proto) return false;

    out.species = species;
    out.origin  = origin;
    out.age     = 0.0f;
    out.effectiveRootVigorMax = species.rootVigorMax;
    out.flowering = false;
    out.senescing = false;

    BranchModuleInstance root;
    root.prototype = proto;
    root.parent = UINT32_MAX;
    root.parentAttachTerminal = 0;
    root.age = 0.0f;
    root.vigor = species.minVigor * 2.0f;  // just above shed threshold
    root.light = 1.0f;
    out.modules = {root};
    return true;
}

} // namespace

void ecosystemTick(WorldState& world, float dt, uint64_t& rng) {
    std::vector<Plant> newPlants;

    for (auto& plant : world.plants) {
        const auto& sp = plant.species;

        // --- Climate adaptation σ scales vigor cap.
        const float sigma = climateAdapt(sp, world.climate, plant.origin.y);
        plant.effectiveRootVigorMax = sp.rootVigorMax * sigma;

        // --- Senescence ramp once past p_max.
        if (plant.age >= sp.maxAge) {
            plant.senescing = true;
            float over = plant.age - sp.maxAge;
            float life = sp.maxAge > 0.0f ? sp.maxAge : 1.0f;
            float ramp = std::max(0.0f, 1.0f - over / life);
            plant.effectiveRootVigorMax *= ramp;
        }

        // --- Shed modules whose vigor has fallen below the threshold.
        // Skip freshly-spawned modules (age 0) so a not-yet-developed
        // node isn't immediately culled.
        const float vmin = sp.minVigor;
        plant.modules.erase(
            std::remove_if(plant.modules.begin(), plant.modules.end(),
                [vmin](const BranchModuleInstance& m) {
                    return m.vigor < vmin && m.age > 0.0f;
                }),
            plant.modules.end());

        // --- First-flowering switch.
        if (!plant.flowering && plant.age > sp.floweringAge) {
            plant.flowering = true;
        }

        // --- Seeding (paper §3.5).
        //   F_eff = F_age · v̂_rootmax / v̄_root
        // The faster the plant's root vigor rises, the sooner it seeds.
        if (plant.flowering && !plant.senescing) {
            float rootVigor = plant.modules.empty() ? 0.0f : plant.modules.front().vigor;
            if (rootVigor > 1e-4f && sp.seedingRadius > 0.0f) {
                float fEff = sp.floweringAge * sp.rootVigorMax / rootVigor;
                if (plant.age > fEff) {
                    // Probability scales with σ — bad climates seed rarely.
                    float p = sigma * std::min(1.0f, dt);
                    if (randFloat01(rng) < p) {
                        Vec2 off = randGaussian2D(rng, sp.seedingRadius);
                        Vec3 origin = {plant.origin.x + off.x, plant.origin.y, plant.origin.z + off.y};
                        // Drop onto terrain if available; reject if soil blocked.
                        origin.y = terrainHeightAt(world.terrain, origin, plant.origin.y);
                        if (!soilBlockedAt(world.soil, origin)) {
                            Plant seedling;
                            if (makeSeedling(world, sp, origin, seedling)) {
                                newPlants.push_back(std::move(seedling));
                            }
                        }
                    }
                }
            }
        }
    }

    // Append new seedlings after iteration so we don't invalidate the loop.
    if (!newPlants.empty()) {
        world.plants.reserve(world.plants.size() + newPlants.size());
        for (auto& s : newPlants) world.plants.push_back(std::move(s));
    }

    // Drop fully-dead plants — senesced past zero AND no modules left.
    world.plants.erase(
        std::remove_if(world.plants.begin(), world.plants.end(),
            [](const Plant& p) {
                return p.effectiveRootVigorMax <= 0.0f && p.modules.empty();
            }),
        world.plants.end());
}

} // namespace broflora
