# auto-flora strategy

Implementation map onto Makowski et al. 2019, *Synthetic Silviculture*
(ACM TOG 38(4) Article 131, DOI 10.1145/3306346.3323039). Each section
below quotes the paper's algorithmic step and points at the broflora
source that realises it.

## §1 — Architectural tiers

| Paper                  | Code                                      |
| ---------------------- | ----------------------------------------- |
| Branch module `G=(N,E)` | `BranchModulePrototype`, `BranchModuleInstance` in `include/broflora/module.h` |
| Plant (tree of modules) | `Plant` in `include/broflora/plant.h` (modules in a flat topo-sorted vector) |
| Ecosystem               | `WorldState` in `include/broflora/world.h` |

Modules are stored in a single `std::vector` per plant, parents always
preceding children. This ordering is the contract every step below
relies on.

## §3 — Per-tick simulation loop

`World::step(dt)` runs A → E in `src/world.cpp`:

### A. Spatial constraints + light — `src/light.cpp`

- Pairwise sphere-intersection sum over all bounding spheres in the
  world (`sphereIntersectVolume`). Local exposure `Q(u) = exp(-Σ V)`.
- Global shadow grid `ShadowGrid` reset each tick, then attenuated
  cell-by-cell beneath each module's footprint.
- Effective light `Q_eff = lerp(s_tol, 1, Q · Q_G)` stamped into
  `BranchModuleInstance::light`.

Approximations vs. paper: the shadow stamp uses
`1 - exp(-k·r²)` per module instead of a beam integration; the inner
loop is O(N²) until a BVH is needed.

### B. Vigor distribution — `src/vigor.cpp`

- Basipetal: single reverse-topo pass accumulating each module's local
  light into `subtreeLight`.
- Acropetal: forward pass splits parent vigor between the
  `isMainChild` meristem (weight λ) and the lateral siblings
  (weight 1−λ). Root clamped to `effectiveRootVigorMax`.

`isMainChild` is set at spawn time from the prototype's terminal
order so the split is insertion-order independent.

### C. Module development — `src/development.cpp`

- Age increment `dau/dt = S((v−v_min)/(v_max−v_min)) · g_p`.
- `refreshModuleNodePositions` walks each module's prototype tree and
  grows segments as `l_b = min(l_max, β·a_b)`, also clipped to the
  static prototype edge length. Cached on
  `BranchModuleInstance::nodePositions` for downstream use.
- World position pass attaches each child at its parent's *grown*
  terminal node (so immature parents hand off short attach points).
- Tropism offset `τ(a_b) = g1·ĝ·g2 / (a_b + g1)` applied to worldPos.
- `computeBbox` produces the bounding sphere from the grown rotated
  node cloud.
- Pipe-model diameters: terminals seeded with `leafDiameter`, parents
  recursively accumulate `Σ d_c^pipeExp` then take the `1/pipeExp`
  root.

### D. Spawning — `src/spawning.cpp`

- Maturity gate (`age > moduleMatureAge`); per-terminal vigor
  `q(n_i) = Q(u)/#n` thresholded against `v_min`.
- Effective determinacy `D' = v_parent · D / v_max`; nearest-site
  lookup against `WorldState::voronoi` (`pickPrototype`).
- Orientation: coordinate descent on (θ, ψ) minimising
  `ω1·f_collisions + ω2·f_tropism`. Hypothesis bbox built from the
  prototype's static extent (so newborn zero-size modules can still be
  reasoned about); each settled sibling is appended to the descent's
  neighbour list before the next sibling runs.

### E. Senescence + climate + seeding — `src/senescence.cpp`

- Climate σ via 2D Gaussian over (T(h), P) vs species optima;
  multiplies `effectiveRootVigorMax`.
- Senescence linear ramp past `p_max`.
- Module shedding: descendants of a shed module are cascaded, then a
  single in-place compact remaps surviving `parent` indices via an
  old→new table.
- First-flowering switch (`plant.flowering = true`) reroutes vigor and
  spawning to the species' mature `(λ, D)`.
- Seeding when `p_t > F_age · v̂_rootmax / v̄_root`, σ-scaled
  probability per tick. Seedling placement: Gaussian offset, terrain
  height drop, soil-blocked rejection. New plants instantiated via
  `makeSeedling` using the species' juvenile (D, λ) Voronoi lookup.

## Boundaries

- `include/broflora/mesh_emit.h` is the only place broflora meets
  bromesh. bromesh is a hard sibling dependency wired in
  `CMakeLists.txt`; emit writes straight into `bromesh::MeshData`.
  Emits one tapered cylinder per branch edge, sized by the module's
  pipe-model diameter and the segment's grown endpoints.
- Determinism: every randomness source (`Plant`'s `rng` thread,
  attractor sampling, seed placement) takes an explicit `uint64_t&`
  state — see `include/broflora/rng.h`.

## Open / approximations

- Acropetal vigor split is generalised to N children (paper writes
  the two-child form). Mainline gets λ·Q_main, laterals share
  (1−λ)·Q_lat weighted by their own subtree light.
- Shadow attenuation is a heuristic, not a beam integration.
- Seeding probability `σ·min(1,dt)` is a per-tick scaling, not a
  derivation from the paper's continuous formulation.
- Spawn orientation: coordinate descent (8 iters, halving step) rather
  than analytic gradient. Sufficient for typical fan-out factors.
