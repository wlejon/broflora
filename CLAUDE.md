# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`broflora` is a C++20 static library implementing the multi-scale plant
ecosystem model from Makowski et al. 2019, "Synthetic Silviculture"
(ACM TOG 38(4) Article 131, DOI 10.1145/3306346.3323039). It is a
stateful runtime simulation — *not* a mesh generator. Geometry is emitted
at the boundary via `bromesh`.

Sibling to `bromesh`, `broaudio`, `brogameagent`. Consumed by the `bro`
runtime as a static library (`target_link_libraries(... broflora)`).

## Build and test

```sh
cmake -S . -B build
cmake --build build --config Release

cd build && ctest --build-config Release --output-on-failure
./build/tests/Release/broflora_test     # Windows / MSVC
./build/tests/broflora_test             # single-config generators
```

CMake 3.24+, C++20. MSVC uses static CRT (`/MT[d]`) only when broflora is
the top-level project; as a subdirectory it inherits the parent's runtime.

The test harness is the same custom `TEST(name) { ... }` macro pattern
bromesh uses — register at static init, dispatch from `main()` in
`test_main.cpp`. No GoogleTest.

## Architecture

### Tiers (paper §1)

1. **Branch Module** — a small graph `G = (N, E)` of nodes and edges,
   one root node and one or more terminal nodes. A *prototype* is the
   static template; an *instance* holds the runtime state (vigor,
   light, age, bbox, Euler orientation).
2. **Plant** — an ordered tree of module instances rooted at `u_root`,
   plus species params (max vigor, apical control λ, determinacy D,
   shade tolerance s_tol).
3. **Ecosystem / World** — a spatial environment (TerrainMap, SoilMap,
   ShadowGrid, GlobalClimate) containing many `Plant` instances.

### Per-tick simulation loop (paper §3)

`step(WorldState&, float dt)` (in `src/world.cpp`) runs A → E in order:

- **A.** `evaluateLightAndCollisions` — spatial constraints, local +
  global light. Stamps `module.light` (Q_eff) and `module.lightExposure`
  (raw Q·Q_G for shade-driven foliage culling). (`src/light.cpp`)
- **B.** `vigorPasses` — two-pass vigor distribution: basipetal
  accumulation into `subtreeLight`, then acropetal redistribution gated
  by apical control λ. (`src/vigor.cpp`)
- **C.** `developModules` — age increment via smooth-step on vigor,
  pipe-model diameters `d_b^2.5 = Σ d_c^2.5`, tropism offset; refreshes
  `nodePositions`, `worldPos`, bbox sphere, and the directional collision
  capsule (`axisTip`). (`src/development.cpp`)
- **D.** `spawnModules` — spawn new modules at mature terminal nodes; pick
  prototype via 2D Voronoi over `(D', λ)`; gradient-descent orientation to
  minimise capsule-collision + tropism penalty (lifted toward the up axis
  by `orthotropy` so crowns spread rather than lean into a column).
  (`src/spawning.cpp`)
- **E.** `ecosystemTick` — senescence (linear vigor decay past `p_max`,
  in-place shed + parent-index remap), climate-scaled seeding via 2D
  Gaussian over `(T, P)` vs species optima, light-gated recruitment,
  world-bounds + slope containment. (`src/senescence.cpp`)

`stepWithObserver(world, dt, observer)` runs the same loop but invokes the
optional `StepObserver` callbacks (`postLight`, `postVigor`,
`postDevelopment`, `preSpawn`, `postSpawn`, `postSenescence`) between
phases — for snapshotting intermediate state from `bro`. `step` is
`stepWithObserver(w, dt, {})`.

Each step is its own header under `include/broflora/` so the loop
in `world.cpp` reads top-down through the paper's algorithm.

### Builders and removal (`world.h`)

`BranchModuleInstance` holds a raw pointer to its prototype and modules
reference siblings by index, so construction order matters. Use the
builders rather than `push_back`-ing the `WorldState` vectors directly:
`addPrototype` (returns a stable index), `addVoronoiSite`, and `addPlant`.
`removePlant` swap-and-pops; plant indices are NOT stable across
`removePlant` or `step` (senescence erases dead plants and appends
seedlings). `validate(const WorldState&)` / `validate(const Plant&)`
(`validate.h`) enforce the topo-order and pointer invariants.

### Prototype library (`prototypes.h`)

Built-in `BranchModulePrototype` factories so callers get full 3D crowns
without authoring node/edge graphs: `straightModule` (I-pole), `forkModule`
(planar Y), `whorlModule(arms, spread)` (candelabra, the workhorse for
rounded crowns). All built around +Y growth; tropism and per-module
orientation tilt them from there.

### Mesh-emit boundary (`mesh_emit.h`)

The only place the simulation core meets the geometry side. Emits straight
into `bromesh` types (no copy): `emitPlantMesh` / `emitWorldMesh`
(tapered-cylinder branch mesh), `emitPlantSegments` / `emitWorldSegments`
(`bromesh::BranchSegment` skeleton for `placeLeavesOnBranches` /
`scatterLeaves`), `emitPlantFoliage` / `emitWorldFoliage` (per-segment
`FoliageSample` — density `mass` plus the raw maturity / vigor / light /
senescence scalars), and `emitPlantBloomAnchors` / `emitWorldBloomAnchors`
(`BloomAnchor` candidates on flowering plants' terminal twigs). The
foliage and segment lists are index-aligned per the documented invariant.

### Terrain coupling (broflora extension, not in the paper)

`Species::terrainAnchorWeight` blends the root module's orientation toward
the terrain surface normal at the plant origin (trunks tilt on slopes);
`maxSeedingSlope` rejects seeding candidates on slopes steeper than the
threshold.

## Conventions

- **Flat parallel arrays.** Same as bromesh — no AoS structs in
  hot data. Per-bone-style 10-float stride patterns where relevant.
  Keeps the surface bindings-friendly (TypedArray transfer to JS).
- **Column-major 4×4 matrices**, `float[16]`. Quaternions `xyzw`.
  Euler angles stored as `(φ, θ, ψ)` per the paper.
- **Topological order on bones / modules.** Parents precede children
  in any vector — basipetal/acropetal passes rely on this.
- **Deterministic.** All randomness (attractor sampling, seed
  placement) takes an explicit `uint64_t seed` — no global rngs.

## Reference

Paper PDF / supplemental: https://storage.googleapis.com/pirk.io/projects/synthetic_silviculture/index.html

Strategy doc: `docs/auto-flora-strategy.md` — section-by-section map from
the paper's algorithmic steps onto the broflora source that realises each.
