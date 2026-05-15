# broflora

A C++20 library for **multi-scale plant ecosystem simulation**. Stateful
runtime sim — branch modules, plants, and ecosystems — that ticks forward
in time and emits geometry at the boundary (via `bromesh`).

Sibling to `bromesh`, `broaudio`, `brogameagent`; consumed by the `bro`
runtime as a static library.

## Status

Foundation. Data structures and the simulation skeleton are in place;
algorithmic content of the per-tick passes (vigor, development, spawning,
senescence) is stubbed and being filled in.

## Building

```sh
cmake -S . -B build
cmake --build build --config Release

# Tests
./build/tests/Release/broflora_test     # Windows / MSVC
./build/tests/broflora_test             # single-config generators
```

CMake 3.24+, C++20 (MSVC 2022, GCC 12+, Clang 15+). No external deps in the
core library — the optional `bromesh` mesh-emit boundary is gated by an
`EXISTS` check on a sibling `../bromesh/` checkout.

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
climate-driven seeding, and senescence. See `docs/auto-flora-strategy.md`
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
