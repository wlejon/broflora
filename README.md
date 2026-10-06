# broflora

[![CI](https://github.com/wlejon/broflora/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/broflora/actions/workflows/ci.yml)
[![CodeQL](https://github.com/wlejon/broflora/actions/workflows/codeql.yml/badge.svg)](https://github.com/wlejon/broflora/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A C++20 library for **multi-scale plant ecosystem simulation**. Stateful
runtime sim — branch modules, plants, and ecosystems — that ticks forward
in time and emits geometry at the boundary (via `bromesh`).

broflora is one of the engine libraries of the
[bro ecosystem](https://github.com/wlejon/bro/blob/main/docs/ecosystem.md).
It builds on [bromath](https://github.com/wlejon/bromath) and
[bromesh](https://github.com/wlejon/bromesh), and [bro](https://github.com/wlejon/bro)
links it (under `BRO_WITH_FLORA`) and exposes it to apps as `bro.flora`
through the JavaScript binding in `src/api/` (`broflora_api`), which needs
[bronze](https://github.com/wlejon/bronze) and [brass](https://github.com/wlejon/brass).

The simulation runs on the CPU (OpenMP where the compiler has it). It is built
and tested on Windows (MSVC), Linux (GCC and Clang) and macOS (arm64).

## Features

Each `step()` runs the full per-tick loop of the paper: spatial light /
shadow, two-pass Borchert-Honda vigor distribution, pipe-model development
with tropism, prototype-Voronoi module spawning, and senescence /
climate-driven seeding. At the boundary, the mesh-emit layer produces
branch geometry, branch segments for leaf scatter, per-segment foliage
state, and bloom / fruit anchors. A built-in prototype library (straight /
fork / whorl) and an optional per-phase `StepObserver` round it out. See
`docs/auto-flora-strategy.md` for the section-by-section map onto the paper.

## Building

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake 3.24+, C++20 (MSVC 2022, GCC 12+, Clang 15+).

bromath and bromesh resolve the way every repo in the ecosystem resolves a
sibling: an existing target wins (bro adds both first), then a checkout beside
this one (`../bromath`, `../bromesh`; override with `-DBROMATH_DIR` /
`-DBROMESH_DIR`), then the `third_party/` submodules, which carry both:

```sh
# Sibling layout (development): bromath, bromesh, bronze, brass beside broflora
cmake -S . -B build

# Fresh clone: bromath and bromesh come from third_party/
git clone --recursive https://github.com/wlejon/broflora
```

The JavaScript binding needs bronze and brass beside this repository in either
layout (or `-DBRONZE_DIR=<path>`); they have no submodule, because the binding
has to be compiled against the same bronze as the program that loads it.

`ctest` runs `broflora_test` (the simulation and emit suite), `test_sdf_mesh`
and the binding's `broflora_api_test`. `examples/grove.cpp` (`broflora_grove`)
is a minimal end-to-end driver that grows a world and writes it out as OBJ.
CI builds and tests on Linux (GCC and Clang), Windows (MSVC) and macOS/arm64
against the siblings' main branches, builds once more from the `third_party/`
submodules alone, and reports coverage of `include/broflora/` and `src/`.

## What this implements

broflora implements the multi-scale ecosystem model from:

> **Synthetic Silviculture: Multi-scale Modeling of Plant Ecosystems**
> Miłosz Makowski, Torsten Hädrich, Jan Scheffczyk, Dominik L. Michels,
> Sören Pirk, and Wojtek Pałubicki. 2019.
> *ACM Transactions on Graphics*, Vol. 38, No. 4, Article 131.
> [DOI: 10.1145/3306346.3323039](https://doi.org/10.1145/3306346.3323039)

Three architectural tiers — Branch Module, Plant Architecture, Ecosystem —
with an extended Borchert-Honda vigor distribution (basipetal + acropetal
passes), pipe-model branch thickening, spatial light/shadow grids,
climate-driven seeding, and senescence. A terrain-coupling extension
(origin snap, root tilt to the surface normal, slope-gated seeding) is
added on top of the paper's model.

Geometry leaves the simulation through `broflora/mesh_emit.h`, which emits
straight into `bromesh::MeshData` and `bromesh::BranchSegment`:

- `emitPlantMesh` / `emitWorldMesh` — faceted tapered-cylinder branch mesh.
- `emitPlantSegments` / `emitWorldSegments` — branch skeleton as
  `bromesh::BranchSegment`, the shape `bromesh::placeLeavesOnBranches` /
  `scatterLeaves` consume.
- `emitPlantFoliage` / `emitWorldFoliage` — per-segment `FoliageSample`
  (density mass plus the raw maturity / vigor / light / senescence scalars
  it derives from), index-aligned with the segment lists.
- `emitPlantBloomAnchors` / `emitWorldBloomAnchors` — world-space
  bloom / fruit `BloomAnchor` candidates on terminal twigs of flowering
  plants.

Beyond the paper, two emit paths build finished geometry:

- `broflora/leaf_cluster.h` — botanical leaf arrangement (alternate,
  opposite, spiral, pine fascicle, compound pinnate phyllotaxy) on twigs and
  shoots, through bromesh's leaf and scatter generators.
- `broflora/sdf_mesh.h` — a whole plant or world as one watertight organic
  mesh: branch capsules smooth-unioned in a `bromesh::SdfGraph` and meshed by
  surface nets or marching cubes (JIT-compiled through brass).

`include/broflora/prototypes.h` ships ready-made `straightModule`,
`forkModule`, and `whorlModule` templates so callers get full crowns
without hand-authoring node/edge graphs. See `docs/auto-flora-strategy.md`
for the implementation map onto the paper's sections.

## Acknowledgements

The core ecosystem simulation logic, multi-scale growth algorithms, and
plant–environment interaction models in this project are based on the
research cited above. All credit for the underlying model belongs to the
original authors; any implementation choices or bugs are mine.

### BibTeX

```bibtex
@article{makowski2019synthetic,
  author = {Makowski, Mi\l{}osz and H\"{a}drich, Torsten and Scheffczyk, Jan and Michels, Dominik L. and Pirk, S\"{o}ren and Pa\l{}ubicki, Wojtek},
  title = {Synthetic Silviculture: Multi-Scale Modeling of Plant Ecosystems},
  year = {2019},
  issue_date = {July 2019},
  publisher = {Association for Computing Machinery},
  volume = {38},
  number = {4},
  issn = {0730-0301},
  url = {https://doi.org/10.1145/3306346.3323039},
  doi = {10.1145/3306346.3323039},
  journal = {ACM Trans. Graph.},
  articleno = {131},
  numpages = {14}
}
```

## License

MIT. See [LICENSE](LICENSE).
