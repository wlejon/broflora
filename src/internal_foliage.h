#pragma once

// Internal leaf-area model — the single source of truth for "how much
// foliage does this module bear." Shared by light.cpp (so leaves cast
// shade and overlapping crowns form an ecological canopy) and
// mesh_emit.cpp (so the emitted FoliageSample.mass agrees with the shade
// the same leaves cast). NOT public surface: the policy is a broflora
// modelling choice, not an API contract.
//
// The proxy is deliberately independent of a module's *instantaneous*
// light (Q_eff): the light pass needs to deposit leaf shade *before* it
// has computed this tick's light, so leaf area can only depend on state
// carried across the tick (diameter, age, vigor). mesh_emit, which runs
// after the light pass, multiplies the proxy by Q_eff for the final
// emit density.

#include "broflora/plant.h"

#include <algorithm>

namespace broflora::internal {

inline float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// Leaf "grade" of a module by branch thickness. Leaves grow on young
// shoots, not the trunk: 1.0 at leaf thickness, falling to 0 on
// branches thicker than `kTwigSpan`× the species leaf diameter. The
// pipe-model `diameter` makes interior twigs thin and the trunk thick, so
// this naturally keeps foliage in the crown and off the bole.
inline float leafGrade(const Species& sp, float diameter) {
    const float leafD = sp.leafDiameter > 0.0f ? sp.leafDiameter : 0.02f;
    const float kTwigSpan = 2.2f;
    const float twigMax = leafD * kTwigSpan;
    if (diameter <= leafD) return 1.0f;
    if (diameter >= twigMax) return 0.0f;
    float t = (twigMax - diameter) / (twigMax - leafD);
    return t * t;
}

// Health gate: module maturity (age vs moduleMatureAge) × vigor fraction.
// In [0,1]. A just-born or starved module bears no leaves.
inline float leafHealth(const Species& sp, float age, float vigor) {
    const float ageGate = sp.moduleMatureAge > 0.0f
        ? clamp01f(age / sp.moduleMatureAge) : 1.0f;
    const float vig = sp.maxVigor > 0.0f ? clamp01f(vigor / sp.maxVigor) : 0.0f;
    return ageGate * vig;
}

// Leaf-area proxy in [0,1] — how much foliage a module carries,
// independent of this tick's light. Drives both the canopy shadow
// optical depth and the foliage emit mass so leaves and shade agree.
inline float leafAreaProxy(const Species& sp, const BranchModuleInstance& m) {
    return leafGrade(sp, m.diameter) * leafHealth(sp, m.age, m.vigor);
}

} // namespace broflora::internal
