#pragma once

// Step B — Extended Borchert-Honda vigor distribution. Two passes:
//
//   basipetal: leaves → root, accumulating light:   Q(u) = Q(u_m) + Q(u_l)
//   acropetal: root → leaves, splitting vigor by apical control λ:
//
//     v̄(u_m) = v̄(u) * λ Q(u_m) / (λ Q(u_m) + (1-λ) Q(u_l))
//
// Total root vigor is clamped to species.rootVigorMax (or the plant's
// senescence/climate-scaled effectiveRootVigorMax).
//
// Paper §3.2.

#include "broflora/plant.h"

namespace broflora {

// Run both passes in place on `plant.modules`. Requires modules in
// topological order (parents before children).
void runVigorPasses(Plant& plant);

} // namespace broflora
