# broflora

[![CI](https://github.com/wlejon/broflora/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/broflora/actions/workflows/ci.yml)
[![CodeQL](https://github.com/wlejon/broflora/actions/workflows/codeql.yml/badge.svg)](https://github.com/wlejon/broflora/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A C++20 library for **multi-scale plant ecosystem simulation**. Stateful
runtime sim — branch modules, plants, and ecosystems — that ticks forward
in time and emits geometry at the boundary (via `bromesh`).

Sibling to `bromesh`, `broaudio`, `brogameagent`; consumed by the `bro`
runtime as a static library.

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

# Tests
./build/tests/Release/broflora_test     # Windows / MSVC
./build/tests/broflora_test             # single-config generators
```

CMake 3.24+, C++20 (MSVC 2022, GCC 12+, Clang 15+). Depends on sibling
`../bromath` (header-only) and `../bromesh` (geometry emit target); both
are added via `add_subdirectory` when not already provided by the
enclosing build.

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
