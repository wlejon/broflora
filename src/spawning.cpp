#include "broflora/spawning.h"

#include "broflora/rng.h"
#include "broflora/vec_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace broflora {

namespace {

// Nearest Voronoi site in (D, λ) parameter space. Returns nullptr if the
// world has no prototypes registered.
const BranchModulePrototype* pickPrototype(const WorldState& world,
                                           float dPrime, float lambda) {
    const BranchModulePrototype* best = nullptr;
    float bestD2 = std::numeric_limits<float>::infinity();
    for (const auto& site : world.voronoi) {
        if (!site.prototype) continue;
        float dd = site.determinacy - dPrime;
        float dl = site.apicalControl - lambda;
        float d2 = dd * dd + dl * dl;
        if (d2 < bestD2) { bestD2 = d2; best = site.prototype; }
    }
    return best;
}

// Spread N attachment points around the unit circle (XZ plane) — used
// to give sibling modules distinct yaws so they don't pile on top of
// each other. Quick stand-in for the paper's f_distribution gradient
// descent; works well enough for typical fan-out factors of 2–5.
float yawForTerminal(uint32_t terminalSlot, uint32_t totalTerminals, uint64_t& rng) {
    if (totalTerminals == 0) return 0.0f;
    float base = (2.0f * 3.14159265358979f) * terminalSlot / totalTerminals;
    // Tiny jitter to break determinism between sibling branches when the
    // user wants visual variety.
    return base + randFloatSigned(rng) * 0.05f;
}

} // namespace

void spawnModules(Plant& plant, WorldState& world, uint64_t& rng) {
    auto& mods = plant.modules;
    if (mods.empty()) return;

    const auto& sp = plant.species;
    const float lambda = plant.flowering ? sp.apicalControlMature : sp.apicalControl;
    const float D      = plant.flowering ? sp.determinacyMature   : sp.determinacy;
    const float vmax   = sp.maxVigor > 0.0f ? sp.maxVigor : 1.0f;

    // Map (parent module index, terminal node id) → already attached.
    // O(N) modules; tiny vector of pairs is fine.
    std::vector<std::pair<uint32_t, uint32_t>> occupied;
    occupied.reserve(mods.size());
    for (const auto& m : mods) {
        if (m.parent != UINT32_MAX) {
            occupied.emplace_back(m.parent, m.parentAttachTerminal);
        }
    }
    auto isOccupied = [&](uint32_t pidx, uint32_t term) {
        for (auto& pp : occupied) {
            if (pp.first == pidx && pp.second == term) return true;
        }
        return false;
    };

    std::vector<BranchModuleInstance> spawns;
    const uint32_t modCount = static_cast<uint32_t>(mods.size());

    for (uint32_t i = 0; i < modCount; ++i) {
        auto& u = mods[i];
        if (!u.prototype) continue;
        if (u.age < sp.moduleMatureAge) continue;

        const auto& terms = u.prototype->terminalNodes;
        if (terms.empty()) continue;

        const float perTerminal = u.light / static_cast<float>(terms.size());
        if (perTerminal <= sp.minVigor) continue;

        const float dPrime = u.vigor * D / vmax;

        for (uint32_t k = 0; k < terms.size(); ++k) {
            uint32_t termNode = terms[k];
            if (isOccupied(i, termNode)) continue;

            const BranchModulePrototype* proto = pickPrototype(world, dPrime, lambda);
            if (!proto) continue;

            BranchModuleInstance child;
            child.prototype = proto;
            child.parent = i;
            child.parentAttachTerminal = termNode;
            child.age = 0.0f;
            child.vigor = perTerminal;
            child.light = perTerminal;
            child.orientation.psi   = yawForTerminal(k, static_cast<uint32_t>(terms.size()), rng);
            child.orientation.theta = 0.15f;  // slight outward pitch
            // worldPos / bbox will be filled in next development tick.
            spawns.push_back(child);
            occupied.emplace_back(i, termNode);
        }
    }

    if (!spawns.empty()) {
        mods.insert(mods.end(), spawns.begin(), spawns.end());
    }
}

} // namespace broflora
