#pragma once

// Built-in branch-module prototypes — ready-made templates so callers get
// full, three-dimensional crowns without hand-authoring node/edge graphs
// and the basipetal-ordering invariants they must satisfy.
//
// Each factory returns a `BranchModulePrototype` by value; register it with
// `addPrototype(world, ...)`. The returned prototype's `name` points at a
// static string literal (safe to keep). Node `ageAtBirth` / `lengthMax` /
// `thickening` are tuned so segments grow in a sensible basipetal order
// (trunk before arms) under the default development model.
//
// All prototypes are built around +Y growth; the simulation's tropism and
// per-module orientation tilt and fan them from there.

#include "broflora/module.h"

namespace broflora {

// Single straight segment, one terminal. The "I" pole — minimal growth
// unit, useful as the juvenile / shade-suppressed Voronoi extreme where a
// plant just extends upward without branching.
BranchModulePrototype straightModule(const char* name = "straight");

// A symmetric two-terminal fork in one plane — the classic "Y". Cheap and
// recognisable; left planar on purpose so the fan-out comes from the
// spawner's per-terminal yaw rather than the prototype itself.
BranchModulePrototype forkModule(const char* name = "fork");

// A short trunk topped by `arms` terminals spread evenly around +Y and
// pitched outward — a candelabra / whorl. This is the workhorse for full,
// rounded crowns: every spawn event adds `arms` three-dimensional shoots,
// so crowns fill volumetrically instead of stacking into a vertical whip.
// `arms` is clamped to [2, 8]; `spread` (0..1) sets how far the arms lean
// out from vertical. Terminals are listed CCW starting at +X.
BranchModulePrototype whorlModule(uint32_t arms = 3, float spread = 0.55f,
                                  const char* name = "whorl");

// Monopodial leader module: central dominant leader extending upward
// (terminal 0 = apical tip) with `lateralBranches` (1..4) side arms branching
// out at realistic acute/spreading angles. Node ages and lengths are tuned so
// the leader extends strongly before laterals, giving true excurrent /
// conifer / monopodial growth. Curved and subdivided with curveModule.
BranchModulePrototype monopodialLeaderModule(uint32_t lateralBranches = 2,
                                             float lateralSpread = 0.7f,
                                             const char* name = "monopodial");

// Sympodial fork module: asymmetrical fork with one dominant arm (primary,
// terminal 0) and one secondary arm (terminal 1), capturing decurrent
// spreading crowns (oaks, maples, elms). Curved and subdivided with curveModule.
BranchModulePrototype sympodialForkModule(float primarySpread = 0.3f,
                                          float lateralSpread = 0.7f,
                                          const char* name = "sympodial");

// Horizontal plagiotropic shelf tiers (pines, cedars, dogwoods). `arms`
// (clamped to [2, 8]) extend out near horizontal with small vertical rise
// and wide spread. Curved and subdivided with curveModule.
BranchModulePrototype horizontalTierModule(uint32_t arms = 3,
                                           float spread = 0.85f,
                                           const char* name = "tier");

// Weeping module: pendulous downward-curving lateral shoots (weeping willows,
// weeping birches). Arches outward and droops downward. Curved and
// subdivided with curveModule.
BranchModulePrototype weepingModule(float spread = 0.6f,
                                    float droop = 0.4f,
                                    const char* name = "weeping");

// --- Habits (presets.h builds species from these) ---------------------------
//
// Each takes a `scale` on its whole layout (and on its growth rates, so a
// module takes as long to grow whatever its size): a shrub's modules are a
// third of a tree's.

// A conifer's leader: one internode `internode` long (terminal 0, the next
// leader attaches there) topped by a whorl of `arms` (2..8) short bough
// stubs, pitched `armPitch` radians above the horizontal and `armLength`
// long, each the start of a bough (its child grows from the lateral group).
// The leader extends before the whorl buds, so the whorls stack up the stem
// as tiers. Arm azimuths are staggered a little, never a perfect star.
BranchModulePrototype leaderWhorlModule(uint32_t arms = 5, float armPitch = 0.1f,
                                        float internode = 0.8f, float armLength = 0.3f,
                                        float scale = 1.0f, const char* name = "leaderWhorl");

// A plagiotropic (flat, two-ranked) shoot: an axis along +Y `length` long
// (terminal 0 continues it) with `pairs` (1..4) pairs of side shoots in its
// own XY plane, alternating left and right, `angle` radians off the axis and
// `sideLength` long. The spawner never rolls a module, so the plane stays
// level about the axis: a conifer bough branches into a flat spray, a
// dogwood shelf into a flat tier.
BranchModulePrototype plagiotropicModule(uint32_t pairs = 2, float angle = 0.75f,
                                         float length = 0.6f, float sideLength = 0.4f,
                                         float scale = 1.0f, const char* name = "plagiotropic");

// A shrub's crown from the ground: `stems` (2..8) stems from the root, one
// up the middle (terminal 0) and the rest fanned `spread` (0..1) out from
// vertical, `height` tall: a multi-stemmed clump rather than a trunk.
BranchModulePrototype basalStemsModule(uint32_t stems = 5, float spread = 0.5f,
                                       float height = 0.5f, float scale = 1.0f,
                                       const char* name = "basalStems");

// `proto` with its layout scaled by `scale` (positions, lengths and growth
// rates), so it grows to `scale` times the size in the same time.
BranchModulePrototype scaledModule(BranchModulePrototype proto, float scale);

} // namespace broflora

