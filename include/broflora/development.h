#pragma once

// Step C — Module development & geometry updating.
//
//   age increment:  Υ(u) = S((v̄ - v̄_min) / (v̄_max - v̄_min)) * ḡ_p
//   branch length:  l_b  = min(l_max, β · a_b)         where a_b = max(0, a_u - a_n)
//   branch diameter (pipe model): d_b = (Σ d_c^2.5)^(1/2.5)
//   tropism offset: τ(a_b) = g1 · ĝ · g2 / (a_b + g1)
//
// Paper §3.3.

#include "broflora/plant.h"

namespace broflora {

// Advance ages and update bounding spheres for every module.
void developModules(Plant& plant, float dt);

} // namespace broflora
