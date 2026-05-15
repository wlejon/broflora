#include "broflora/vigor.h"

#include <algorithm>

namespace broflora {

// Extended Borchert-Honda. Both passes assume `plant.modules` is in
// topological order (parents before children).
//
// TODO(paper §3.2): the real basipetal pass sums child light into each
// branching intersection inside the module's internal graph. The
// skeleton flattens that to "module light = sum of child-module light +
// own raw light". Good enough for end-to-end smoke testing; refine when
// implementing the prototype-graph traversal.

void runVigorPasses(Plant& plant) {
    auto& mods = plant.modules;
    if (mods.empty()) return;

    // --- Basipetal: leaves → root. Reverse-iterate; accumulate child
    // light into each module's parent.
    for (size_t i = mods.size(); i-- > 0; ) {
        // light starts at the value evaluateLightAndCollisions stamped.
        // No-op here unless we have a parent to push to.
        if (mods[i].parent != UINT32_MAX) {
            mods[mods[i].parent].light += mods[i].light;
        }
    }

    // --- Acropetal: root → leaves. The root receives the clamped total
    // root vigor; every internal split divides by λ-weighted light.
    mods[0].vigor = std::min(mods[0].light, plant.effectiveRootVigorMax);

    const float lambda = plant.flowering
        ? plant.species.apicalControlMature
        : plant.species.apicalControl;

    for (size_t i = 0; i < mods.size(); ++i) {
        auto& u = mods[i];

        // Find direct children of u (sibling pairs in the paper's
        // formulation are "main" u_m and "lateral" u_l). For the
        // skeleton we treat the first child as main and split between
        // all children using the λ-weighted formula generalised:
        //
        //   v(c_k) = v(u) * (λ if k=main else (1-λ)/(#lat)) * Q(c_k)
        //           normalised by Σ over children.

        // Two-pass: total denominator, then per-child share.
        float denom = 0.0f;
        bool firstChild = true;
        for (size_t j = i + 1; j < mods.size(); ++j) {
            if (mods[j].parent != static_cast<uint32_t>(i)) continue;
            float w = firstChild ? lambda : (1.0f - lambda);
            denom += w * mods[j].light;
            firstChild = false;
        }

        if (denom <= 0.0f) continue;

        firstChild = true;
        for (size_t j = i + 1; j < mods.size(); ++j) {
            if (mods[j].parent != static_cast<uint32_t>(i)) continue;
            float w = firstChild ? lambda : (1.0f - lambda);
            mods[j].vigor = u.vigor * (w * mods[j].light) / denom;
            firstChild = false;
        }
    }
}

} // namespace broflora
