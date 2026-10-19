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
#include <limits>
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

    // Bark kinks (emit only; not in the paper, and the simulation never
    // reads it): every prototype node off a module's root is nudged
    // sideways by this fraction of its incoming edge's length, and the
    // rest of the branch beyond it follows, so limbs bend at their nodes
    // instead of running ruler-straight. 0 emits the grown skeleton exactly.
    float kink = 0.2f;

    // Bark (emit only, like `kink`: the simulation never reads it). What
    // the renderer draws on the wood (sway.h packBarkWord carries the first
    // three in every bark record) and the shape of the wood at the ground
    // and at its forks (tubeSegments):
    //   barkFurrow    0 smooth (beech) .. 1 deep furrows between ridges (oak)
    //   barkScale     the ridges' spacing, x the default (which already
    //                 widens with the girth)
    //   barkMoss      moss and lichen, 0 none .. 1 lush
    //   barkRibs      0 bark of plates parted by furrows; 1 ribbed: unbroken
    //                 ridges running the length of the stem, the same number
    //                 round it however thick (a cactus's), more the smaller
    //                 barkScale, as deep as barkFurrow
    //   rootFlare     the trunk's swell where it meets the ground, x its radius
    //   branchCollar  a side branch's swell where it leaves its parent, x its radius
    float barkFurrow   = 0.55f;
    float barkScale    = 1.0f;
    float barkMoss     = 0.5f;
    float barkRibs     = 0.0f;
    float rootFlare    = 0.6f;
    float branchCollar = 0.5f;

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

    // Per-plant phenotype variation (broflora extension, not in the paper).
    // 0 = fully deterministic: every plant of a species grows into a
    // bit-identical clone, because the sim is deterministic and its only
    // stochastic input (a tiny spawn-orientation jitter) never cascades into
    // topology — so a stand reads as one plant stamped N times. > 0 gives
    // each plant a distinct but reproducible individuality: addPlant() seeds
    // a per-plant RNG from the origin and (a) rotates the whole plant by a
    // random azimuth and (b) offsets growthScale / moduleMatureAge /
    // orthotropy by up to this fraction. Typical: 0.1–0.2.
    float individualVariation          = 0.0f;

    // Habit (broflora extension, not in the paper): which modules a plant
    // grows, and how its laterals carry themselves.
    //
    // A plant's axes are its LEADER (the root module and every main child
    // down from it: terminal 0 of each module continues it) and its
    // LATERALS (every other module, and everything grown from one). The
    // leader picks its modules from the Voronoi sites of `prototypeGroup`,
    // a lateral from those of `lateralGroup` (kSameGroup: the leader's). So
    // a conifer's leader can grow a whorl of boughs each tier while each
    // bough grows flat, two-ranked sprays, and a shrub's stems can rise from
    // a basal clump. Sites of a group no site carries fall back to every
    // site, so worlds with one group behave as before.
    //   lateralElevation   a lateral's gravitropic set-point angle: the
    //                      elevation (radians from the horizon) its new
    //                      growth returns to; 0 level boughs (fir), negative
    //                      drooping (spruce, weeping crowns), positive rising
    //                      (pine), near pi/2 turned up (a columnar cypress)
    //   lateralOrthotropy  how strongly a lateral's new growth returns to
    //                      that angle from the way its arm points: 0 goes on
    //                      along the arm, 1 aims at the set point. NaN (the
    //                      default): laterals grow as the leader does, lifted
    //                      by `orthotropy` and never aimed below the horizon
    //   apicalDominance    0 (the paper): a main child's vigor is λ against
    //                      its siblings' summed light, so a whorl of big
    //                      boughs overtakes its leader; 1: the main child
    //                      keeps λ of its parent's vigor outright (the
    //                      laterals share the rest by light), the excurrent
    //                      leader of a conifer
    //   maxBranchOrder     the deepest axis order that still branches (the
    //                      leader is order 0, its boughs 1, their side
    //                      shoots 2, ...); an axis of that order only
    //                      lengthens. 0: no limit
    //   crownSlope         the crown envelope: a lateral grows no further out
    //                      from the plant's axis than this times its depth
    //                      below the plant's top (0.35: a fir's cone; 0: no
    //                      envelope), plus `crownBase` ...
    //   crownBase          ... at the top (a blunt spire or a rounded top)
    //   crownRadiusMax     ... and never past this (a column, a flat-topped
    //                      pine; 0: no cap)
    //   crownLift          self-pruning: a bough leaving the leader below
    //                      this fraction of the plant's height is shed
    //                      (0.5: a pine's crown on the top half of a bare
    //                      trunk; 0: boughs to the ground)
    //   heightMax          no shoot buds above this height (m): the leader
    //                      stops and the upper boughs fill in under it, a
    //                      mature pine's flat top (0: no limit)
    static constexpr uint32_t kSameGroup = 0xffffffffu;
    float apicalDominance    = 0.0f;
    float maxBranchOrder     = 0.0f;
    float crownSlope         = 0.0f;
    float crownBase          = 0.0f;
    float crownRadiusMax     = 0.0f;
    float crownLift          = 0.0f;
    float heightMax          = 0.0f;
    uint32_t prototypeGroup  = 0;
    uint32_t lateralGroup    = kSameGroup;
    float lateralOrthotropy  = std::numeric_limits<float>::quiet_NaN();
    float lateralElevation   = 0.0f;

    // Foliage and colour (emit only, like the bark fields: the simulation
    // never reads them). What a renderer draws the plant's leaves as and in:
    //   leafForm    LeafForm: broad leaves, needles, or scale-leaf sprays
    //   leafColor   the leaves' mean albedo (linear; 0,0,0: the renderer's own)
    //   barkColor   the bark's (0,0,0: the renderer's own)
    //   clumpScale  the crown clumps' size, x what the emit's options give;
    //               0 none: leafless (a succulent's leaves drawn as its wood)
    //   leafSize    a leaf's size, x the renderer's card (a boxwood's small
    //               leaves 0.3); its clumps carry 1/leafSize^2 the leaves,
    //               so they cover as much
    //   clumpFlat   a clump's height over its width: 1 a broadleaf's round
    //               lobes, ~0.4 the flat pads of a fir's level sprays (the
    //               clumps pack closer up and down by as much)
    //   clumpReach  a clump reaches no further across than this times its
    //               distance from the plant's axis, plus a quarter of its
    //               size (0: no limit): a conifer's short top boughs carry
    //               small clumps, so its crown narrows to a spire
    //   clumpFill   0 (a broadleaf tree's): foliage on the crown's outermost
    //               shoots, its interior shoots thinly leaved; 1: every shoot
    //               tip carries foliage however its module has branched on,
    //               new shoots leaved as they grow (a conifer's clothed
    //               boughs and leader, a shrub's dense mound)
    //   clumpInner  0: a shoot that has branched on carries a tenth of a
    //               tip's foliage and its shade thins it further; 1: as much
    //               as a tip in half light (a shrub's mound, leaved inside
    //               and down to the ground, its stems hidden), and the
    //               clumps packed up to 40% closer so they merge into one mass
    //   clumpGap    the clumps' spacing, x the emit's (1): above 1 they stand
    //               apart as separate tufts with sky between them (a pine's
    //               open crown of tufts at its shoots' ends)
    //   bloomForm   BloomForm: what it flowers as once flowering (none, an
    //               open flower, a round head of florets, an upright spike)
    //   bloomColor  its blooms' albedo (linear)
    //   bloomSize   a bloom's size (m): a flower's or a head's across, a
    //               spike's length
    // A broad leaf's own look (leafForm Broad; the renderer cuts the
    // outline per pixel, so none of it costs geometry):
    //   leafWidth   its width over its length (0: the renderer's, ~0.5): a
    //               boxwood's near-round leaf 0.6, a willow's 0.15
    //   leafTip     0 drawn to a point (acuminate: hydrangea), 1 blunt and
    //               rounded (obtuse: boxwood)
    //   leafTeeth   teeth along each side of the margin (0: entire)
    //   leafToothDepth  how deep each cuts in, of the half-width: a fine
    //               serration ~0.05, a holly's spines ~0.3 (each spine then
    //               a sharp point, not a rounded tooth)
    //   leafGloss   0 a matte leaf (the renderer's roughness), 1 a waxy,
    //               glossy one (holly): a tighter, brighter sun highlight
    // How a broad leaf hangs on its shoot (the renderer grows each clump's
    // leaves on twigs: bro.scene's scene_forest_shoot.glsl, bromesh's
    // leafOnShoot):
    //   phyllotaxis Phyllotaxis: its leaves alternate in two ranks (beech,
    //               birch, holly), stand in opposite pairs each a quarter
    //               turn from the last (hydrangea, boxwood: decussate), or
    //               spiral round the shoot at 2/5 (hibiscus), or are
    //               leaflets lining the wood itself in two ranks, no twigs
    //               (pinnate: a palm's fronds)
    //   petiole     its leaf stalk's length over the blade's (0 sessile; a
    //               birch's ~0.3, a holly's short 0.1)
    float leafWidth          = 0.0f;
    float leafTip            = 0.0f;
    float leafTeeth          = 0.0f;
    float leafToothDepth     = 0.0f;
    float leafGloss          = 0.0f;
    uint32_t phyllotaxis     = 0;
    float petiole            = 0.2f;
    // How far the skeleton sags under its own load when emitted
    // (emitPlantSegments; the simulation never reads it): each joint turns
    // its shoot toward hanging by the load beyond it (wood, and leaves on
    // the thin shoots) times its lever over the shoot's stiffness (E I / L,
    // I from its radius), times this. 0: none (the default); 1: green wood's
    // own; a willow's whips, wetter and heavier with leaves, several times.
    float sag                = 0.0f;
    // A climber's (an ivy's) turns a metre about the wood it climbs: a plant
    // of it standing where a plant that does not climb stands (their
    // origins one, across) climbs that plant's way up (Plant::support), its
    // stems wound round the wood, hugging it, this many turns a metre as
    // they rise (emitPlantSegments; the simulation grows it as a column).
    // 0: it stands on its own (the default).
    float climb              = 0.0f;
    // A needle or scale-leaf spray's set in its clump (leafForm Needle or
    // Scale):
    //   sprayBrush  0: the sprays lie round the clump, facing out (a fir's,
    //               a cypress's smooth mass); 1: they radiate from the
    //               shoot's end and sweep up, a brush of needles (a pine's
    //               tufts)
    float sprayBrush         = 0.0f;
    uint32_t leafForm        = 0;
    bromath::Vec3 leafColor  = {0.0f, 0.0f, 0.0f};
    bromath::Vec3 barkColor  = {0.0f, 0.0f, 0.0f};
    float clumpScale         = 1.0f;
    float leafSize           = 1.0f;
    float clumpFlat          = 1.0f;
    float clumpReach         = 0.0f;
    float clumpFill          = 0.0f;
    float clumpInner         = 0.0f;
    float clumpGap           = 1.0f;
    uint32_t bloomForm      = 0;
    bromath::Vec3 bloomColor = {0.0f, 0.0f, 0.0f};
    float bloomSize          = 0.0f;
};

// What a plant flowers as (Species::bloomForm).
enum class BloomForm : uint32_t {
    None = 0,
    Flower = 1,   // one open flower at a shoot's tip (a hibiscus)
    Head = 2,     // a round head of many small florets (a hydrangea)
    Spike = 3,    // florets up a bare upright stalk (lavender)
};
inline constexpr uint32_t kBloomFormCount = 4;

// How a broad leaf's leaves stand on their shoot (Species::phyllotaxis).
enum class Phyllotaxis : uint32_t {
    Alternate = 0,   // one a node, alternating in two ranks: a flat spray
    Opposite = 1,    // two a node, each pair a quarter turn from the last
    Spiral = 2,      // one a node, 2/5 of a turn apart
    Pinnate = 3,     // leaflets in two ranks along the wood itself, no
                     // twigs: a palm frond's, on its rachis
};
inline constexpr uint32_t kPhyllotaxisCount = 4;

// What a plant's leaves are drawn as (Species::leafForm).
enum class LeafForm : uint32_t {
    Broad  = 0,   // broad leaves: oak, maple, boxwood
    Needle = 1,   // needle sprays: pine, spruce, fir, redwood
    Scale  = 2,   // scale-leaf sprays: cypress, juniper, thuja
};
inline constexpr uint32_t kLeafFormCount = 3;

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

    // A climber's (Species::climb) support: the way up of the plant standing
    // where it stands, as that plant last stood (refreshSupports, each step):
    // the joints from its root to its highest tip while its wood is at
    // least kSupportMinRadius thick, 4 floats each, the joint (world) and
    // the wood's radius there. Empty: it climbs nothing.
    std::vector<float> support;
    // ... and how that plant's stem sways (emitPlantSway's): its height and
    // its trunk's cantilever (freq, sway, length, shape), which the
    // climber's wood takes as its own, so it moves with the bark it holds.
    float supportSway[5] = {};

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
