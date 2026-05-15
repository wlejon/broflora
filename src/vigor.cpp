#include "broflora/vigor.h"

#include <algorithm>

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

    for (size_t i = 0; i < mods.size(); ++i) {
        auto& u = mods[i];

        // Two passes over children: first total the denominator (sum of
        // weighted subtree lights), then assign each child's share.
        float denom = 0.0f;
        for (size_t j = i + 1; j < mods.size(); ++j) {
            if (mods[j].parent != static_cast<uint32_t>(i)) continue;
            float w = mods[j].isMainChild ? lambda : (1.0f - lambda);
            denom += w * mods[j].subtreeLight;
        }
        if (denom <= 0.0f) continue;

        for (size_t j = i + 1; j < mods.size(); ++j) {
            if (mods[j].parent != static_cast<uint32_t>(i)) continue;
            float w = mods[j].isMainChild ? lambda : (1.0f - lambda);
            mods[j].vigor = u.vigor * (w * mods[j].subtreeLight) / denom;
        }
    }
}

} // namespace broflora
