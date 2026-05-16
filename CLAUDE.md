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

`World::step(dt)` runs:

- **A.** Evaluate spatial constraints, local + global light.
- **B.** Two-pass vigor distribution (basipetal accumulation, then
  acropetal redistribution gated by apical control λ).
- **C.** Module development — age increment via smooth-step on vigor,
  pipe-model diameters `d_b^2.5 = Σ d_c^2.5`, tropism offset.
- **D.** Spawn new modules at mature terminal nodes; pick prototype
  via 2D Voronoi over `(D', λ)`; gradient-descent orientation to
  minimise collision + tropism penalty.
- **E.** Senescence (linear vigor decay past `p_max`), climate-scaled
  seeding via 2D Gaussian over `(T, P)` vs species optima.

Each step is its own header under `include/broflora/` so the loop
in `world.cpp` reads top-down through the paper's algorithm.

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

Strategy doc: `docs/auto-flora-strategy.md` (TODO — implementation map
onto the paper's sections).
