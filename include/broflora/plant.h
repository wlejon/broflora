#pragma once

// Plant — an ordered tree of BranchModuleInstance rooted at u_root, plus
// the species configuration. Module instances are stored in a single
// flat vector in topological order (parents precede children) so the
// basipetal (reverse iteration) and acropetal (forward iteration)
// passes are just `for` loops over `modules`.
//
// Paper §1 (plant scale) and §3 (per-plant params).

#include "broflora/module.h"
#include "bromath/vec.h"

#include <cstdint>
#include <vector>

namespace broflora {

// Species / phenotype configuration. Constant over a plant's life,
// except for the (λ, D) mature-overrides applied at first flowering
// (paper §3.5).
struct Species {
    float maxVigor       = 1.0f;   // v̄_max — vigor scale
    float minVigor       = 0.01f;  // v̄_min — threshold for branching / shedding
    float rootVigorMax   = 1.0f;   // v̂_rootmax — clamp on basal vigor

    float apicalControl  = 0.55f;  // λ — main vs lateral split (paper §3.2)
    float determinacy    = 0.5f;   // D — module-selection coord
    float shadeTolerance = 0.3f;   // s_tol — Q_eff floor in shadowed cells

    // Mature overrides (paper §3.5): once the plant first seeds, λ and D
    // are replaced by these to bias crown growth.
    float apicalControlMature = 0.85f;
    float determinacyMature   = 0.2f;

    // Tropism (paper §3.3): gravity direction, two strength factors.
    bromath::Vec3 tropismDir = {0.0f, -1.0f, 0.0f};
    float tropismG1  = 1.0f;
    float tropismG2  = 1.0f;

    // Per-frame growth scalar ḡ_p (paper §3.3).
    float growthScale = 1.0f;

    // Climate optima T_A, P_A and 2D Gaussian σ (paper §3.5).
    float climateOptT  = 15.0f;    // °C
    float climateOptP  = 1000.0f;  // mm/yr
    float climateSigT  = 10.0f;
    float climateSigP  = 500.0f;

    // Lifespan + flowering.
    float maxAge        = 100.0f;   // p_max
    float floweringAge  = 10.0f;    // F_age
    float seedingRadius = 5.0f;     // Gaussian σ on seed placement

    // Module maturity threshold a_mature for spawning (paper §3.4).
    float moduleMatureAge = 1.0f;

    // Pipe-model exponent (paper §3.3); 2.5 in da Vinci / Shinozaki.
    float pipeExp = 2.5f;

    // Tip diameter assigned to terminal modules — base case of the
    // recursive d_b sum.
    float leafDiameter = 0.02f;

    // Terrain anchoring (broflora extension, not in the paper).
    //
    // terrainAnchorWeight in [0,1] blends the root module's per-tick
    // orientation toward the terrain surface normal at the plant's
    // origin. 0 = no terrain influence (the orientation set when the
    // module was created is left alone); 1 = root grows exactly along
    // the surface normal. Used to make trunks tilt on slopes.
    // maxSeedingSlope (radians) rejects seeding candidates whose
    // terrain slope exceeds this value — the angle is measured from
    // +Y, so π/2 (the default) accepts any non-overhang slope.
    float terrainAnchorWeight = 0.0f;
    float maxSeedingSlope     = 1.5707963f;  // π/2 — accept everything

    // Spawn-time orientation gradient descent (paper §3.4):
    //   f_distribution(u) = ω1·f_collisions(u) + ω2·f_tropism(u)
    //   f_tropism(u)      = 1 - cos(growthAxis(u), growthTarget(u))
    // Each child's growthTarget continues the *arm it sprouts from* —
    // the world-space direction of the parent terminal it attaches to —
    // lifted toward the species' up axis (= -tropismDir) by `orthotropy`.
    // This is what makes a crown: a child off an outward-leaning arm keeps
    // leaning outward (it does not reset to vertical), so the whorl's arms
    // broaden into a candelabra instead of every meristem poling straight
    // up. Targeting an absolute direction instead leaves the azimuth
    // unconstrained and collapses the plant into a single leaning column.
    //   orthotropy = 0 : branches hold their arm direction (max spread)
    //   orthotropy = 1 : every branch turns straight up (columnar)
    float distributionWeightCollisions = 1.0f;   // ω1
    float distributionWeightTropism    = 0.5f;   // ω2
    float orthotropy                   = 0.3f;   // arm-dir → up lift, [0,1]
};

// One plant instance.
//
// Invariants (enforced by `validate(const Plant&)` in validate.h):
//   - modules[0] is the root (parent == UINT32_MAX).
//   - For every i > 0, modules[i].parent < i — i.e. parents always
//     precede children in the vector. The basipetal/acropetal vigor
//     passes (vigor.cpp) walk the vector once and rely on this order.
//   - Every module.prototype is non-null and refers to a prototype
//     whose lifetime exceeds this plant's (typically owned by the
//     enclosing WorldState).
//   - Newly spawned modules are appended at the end by spawnModules,
//     preserving the order. Senescence shedding (senescence.cpp)
//     compacts in place and remaps parent indices.
struct Plant {
    Species species;

    // Position of u_root in world space.
    bromath::Vec3 origin = {0.0f, 0.0f, 0.0f};

    // Plant age p_t.
    float age = 0.0f;

    // Topologically sorted modules — `modules[0]` is u_root, every
    // `modules[i].parent` is < i (or UINT32_MAX for the root).
    std::vector<BranchModuleInstance> modules;

    // Effective root-vigor cap, scaled by climate adaptation σ and by
    // senescence ramp once age > maxAge.
    float effectiveRootVigorMax = 1.0f;

    // Has this plant entered the post-flowering "mature" regime
    // (paper §3.5)? If true, species.apicalControlMature / determinacyMature
    // are used in spawning instead of the juvenile values.
    bool flowering = false;

    // Has senescence begun (age >= species.maxAge)?
    bool senescing = false;
};

} // namespace broflora
