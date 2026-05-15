#include "broflora/spawning.h"

#include "broflora/rng.h"
#include "broflora/vec_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace broflora {

// Nearest Voronoi site in (D, λ) parameter space. Returns nullptr if the
// world has no prototypes registered. Exposed (non-anonymous) so seeding
// in senescence.cpp can reuse the same juvenile-(D, λ) lookup.
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

namespace {

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

    // Pack (parent index, terminal id) into a 64-bit key so the
    // already-attached check is O(1). Drops the spawn pass from O(N²) to
    // O(N · #terminals) which matters once a plant has more than a few
    // hundred modules.
    auto key = [](uint32_t pidx, uint32_t term) {
        return (static_cast<uint64_t>(pidx) << 32) | term;
    };
    std::unordered_set<uint64_t> occupied;
    occupied.reserve(mods.size() * 2);
    for (const auto& m : mods) {
        if (m.parent != UINT32_MAX) {
            occupied.insert(key(m.parent, m.parentAttachTerminal));
        }
    }

    std::vector<BranchModuleInstance> spawns;
    const uint32_t modCount = static_cast<uint32_t>(mods.size());

    for (uint32_t i = 0; i < modCount; ++i) {
        auto& u = mods[i];
        if (!u.prototype) continue;
        if (u.age < sp.moduleMatureAge) continue;

        const auto& terms = u.prototype->terminalNodes;
        if (terms.empty()) continue;

        // Local Q at the parent — `light` is stamped by the light pass
        // and untouched by the basipetal accumulation (which writes to
        // `subtreeLight`). Paper §3.4: q(n_i) = Q(u) / #n.
        const float perTerminal = u.light / static_cast<float>(terms.size());
        if (perTerminal <= sp.minVigor) continue;

        const float dPrime = u.vigor * D / vmax;

        for (uint32_t k = 0; k < terms.size(); ++k) {
            uint32_t termNode = terms[k];
            if (occupied.count(key(i, termNode))) continue;

            const BranchModulePrototype* proto = pickPrototype(world, dPrime, lambda);
            if (!proto) continue;

            BranchModuleInstance child;
            child.prototype = proto;
            child.parent = i;
            child.parentAttachTerminal = termNode;
            child.age = 0.0f;
            child.vigor = perTerminal;
            child.light = perTerminal;
            // The prototype's first listed terminal grows the main
            // meristem (paper §3.2). Tagging at spawn time keeps the
            // λ-weighted vigor split independent of insertion order.
            child.isMainChild = (k == 0);
            child.orientation.psi   = yawForTerminal(k, static_cast<uint32_t>(terms.size()), rng);
            child.orientation.theta = 0.15f;  // slight outward pitch
            // worldPos / bbox will be filled in next development tick.
            spawns.push_back(child);
            occupied.insert(key(i, termNode));
        }
    }

    if (!spawns.empty()) {
        mods.insert(mods.end(), spawns.begin(), spawns.end());
    }
}

} // namespace broflora
