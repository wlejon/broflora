#include "broflora/senescence.h"

#include "broflora/rng.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace broflora {

// Declared in spawning.cpp — same juvenile (D, λ) Voronoi lookup used at
// spawn time. Reused here so seedlings start from a prototype consistent
// with the species' juvenile parameters instead of `voronoi.front()`.
const BranchModulePrototype* pickPrototype(const WorldState& world,
                                           float dPrime, float lambda);

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

// Build a single-module seedling. Picks the prototype using the species'
// juvenile (D, λ) — the same Voronoi lookup spawnModules does — so the
// first module a seedling grows is consistent with the species character.
bool makeSeedling(const WorldState& world, const Species& species, Vec3 origin,
                  Plant& out) {
    // For a seedling the parent vigor at instantiation is rootVigorMax —
    // hand that into the dPrime formula so dPrime = D (the juvenile pole).
    const float vmax = species.maxVigor > 0.0f ? species.maxVigor : 1.0f;
    const float dPrime = species.rootVigorMax * species.determinacy / vmax;
    const BranchModulePrototype* proto =
        pickPrototype(world, dPrime, species.apicalControl);
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
        //
        // Critical: dropping a module also kills every descendant — a
        // branch can't survive its parent. After marking, we compact in
        // place and remap the surviving `parent` indices via an
        // old → new index table. Without the cascade + remap, surviving
        // children would silently point at the wrong slot.
        const float vmin = sp.minVigor;
        auto& mods = plant.modules;
        std::vector<uint8_t> shed(mods.size(), 0);
        for (size_t i = 0; i < mods.size(); ++i) {
            const auto& m = mods[i];
            bool selfShed = (m.vigor < vmin && m.age > 0.0f);
            bool parentShed = (m.parent != UINT32_MAX) && shed[m.parent];
            if (selfShed || parentShed) shed[i] = 1;
        }
        // Remap table: old index → new index (UINT32_MAX if shed).
        std::vector<uint32_t> remap(mods.size(), std::numeric_limits<uint32_t>::max());
        uint32_t writeIdx = 0;
        for (size_t i = 0; i < mods.size(); ++i) {
            if (!shed[i]) {
                remap[i] = writeIdx;
                if (writeIdx != i) mods[writeIdx] = mods[i];
                if (mods[writeIdx].parent != UINT32_MAX) {
                    mods[writeIdx].parent = remap[mods[writeIdx].parent];
                }
                ++writeIdx;
            }
        }
        mods.resize(writeIdx);

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
