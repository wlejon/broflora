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

} // namespace broflora
