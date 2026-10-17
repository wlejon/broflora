#include "broflora/vigor.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace broflora {

// Extended Borchert-Honda. Both passes assume `plant.modules` is in
// topological order (parents before children).
//
// `light` is the local Q_eff stamped by the light pass — *not* touched
// here. `subtreeLight` is the basipetal accumulation; the acropetal
// split reads it via the λ-weighted formula.

void runVigorPasses(Plant& plant) {
    auto& mods = plant.modules;
    if (mods.empty()) return;

    // --- Basipetal: leaves → root. Initialise each module's subtree
    // total to its own local light, then push into the parent in reverse
    // topological order so every accumulation flows upward exactly once.
    for (auto& m : mods) m.subtreeLight = m.light;
    for (size_t i = mods.size(); i-- > 0; ) {
        if (mods[i].parent != UINT32_MAX) {
            mods[mods[i].parent].subtreeLight += mods[i].subtreeLight;
        }
    }

    // --- Acropetal: root → leaves. Root receives the clamped total root
    // vigor; every internal split divides between the main child (weight
    // λ) and lateral children (weight (1 - λ), shared by light).
    mods[0].vigor = std::min(mods[0].subtreeLight, plant.effectiveRootVigorMax);

    const float lambda = plant.flowering
        ? plant.species.apicalControlMature
        : plant.species.apicalControl;
    // Apical dominance (Species::apicalDominance): how much of the main
    // child's share is λ outright rather than λ against its laterals'
    // summed light. At 0 (the paper) a whorl's boughs, their light summed,
    // overtake the leader once they are big; at 1 the leader keeps λ of the
    // vigor however big its boughs grow, as a conifer's does.
    const float dom = std::clamp(plant.species.apicalDominance, 0.0f, 1.0f);

    // Each module's children, in index order (a counting sort by parent), so
    // a split walks its own children rather than every later module; the
    // sums run in the same order, so the result is the same to the bit.
    const size_t n = mods.size();
    thread_local std::vector<uint32_t> first, kids;
    first.assign(n + 1, 0);
    for (size_t j = 1; j < n; ++j)
        if (mods[j].parent < j) ++first[mods[j].parent + 1];
    for (size_t i = 0; i < n; ++i) first[i + 1] += first[i];
    kids.resize(first[n]);
    {
        thread_local std::vector<uint32_t> at;
        at.assign(first.begin(), first.end() - 1);
        for (size_t j = 1; j < n; ++j)
            if (mods[j].parent < j) kids[at[mods[j].parent]++] = static_cast<uint32_t>(j);
    }

    for (size_t i = 0; i < n; ++i) {
        auto& u = mods[i];
        const uint32_t* k0 = kids.data() + first[i];
        const uint32_t* k1 = kids.data() + first[i + 1];

        // Two passes over children: first total the denominator (sum of
        // weighted subtree lights), then assign each child's share.
        float denom = 0.0f, lateralLight = 0.0f;
        bool hasMain = false, hasLateral = false;
        for (const uint32_t* k = k0; k != k1; ++k) {
            const size_t j = *k;
            float w = mods[j].isMainChild ? lambda : (1.0f - lambda);
            denom += w * mods[j].subtreeLight;
            if (mods[j].isMainChild) hasMain = true;
            else { hasLateral = true; lateralLight += mods[j].subtreeLight; }
        }
        if (denom <= 0.0f) continue;
        // The fixed split applies where both a main child and laterals are
        // there to split between.
        const float d = hasMain && hasLateral && lateralLight > 0.0f ? dom : 0.0f;

        for (const uint32_t* k = k0; k != k1; ++k) {
            const size_t j = *k;
            float w = mods[j].isMainChild ? lambda : (1.0f - lambda);
            if (d <= 0.0f) {
                mods[j].vigor = u.vigor * (w * mods[j].subtreeLight) / denom;
                continue;
            }
            const float share = (w * mods[j].subtreeLight) / denom;
            const float fixed = mods[j].isMainChild ? lambda : (1.0f - lambda) * mods[j].subtreeLight / lateralLight;
            mods[j].vigor = u.vigor * (share + (fixed - share) * d);
        }
    }
}

} // namespace broflora
