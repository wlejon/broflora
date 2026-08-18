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

} // namespace broflora

