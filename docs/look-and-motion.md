# Look and motion: broflora beautiful by default

broflora's simulation grows good plants, but what reaches the screen does not
look or move like a tree. This plan adopts what looked best in a search that
built the same valley six ways from code (`../opus-worlds`, a local scratch
repo: `blocks/<id>/`, `gallery/<id>/`, `notes/<id>.md`). The aim is that any app
calling broflora and bro.scene with defaults gets trees, shrubs and grass that
are beautiful standing still and move like plants.

## What looked best, and why

| Quality | Reference | What makes it work |
|---|---|---|
| Trees standing still | volume (`blocks/volume/trees.js`, `sprites.js`, `gallery/volume/r2_*`) | kinked branches (never straight), smooth-unioned bark; the crown is **clumps** of leaf mass with gaps between them, not a uniform spray; a crown envelope gives each point an interior depth, so the crown is dark inside and lit at the rim |
| Tree motion | dense (`blocks/dense/flora.js` `treeBend`, `element.js`) and growth (`blocks/growth/flora.js`) | hierarchical sway: the trunk leans into a **travelling gust** (offset grows with height squared); each limb bobs on its own phase, weighted by branch weight squared (0 on the trunk, 1 at a twig tip); each leaf flutters about its own stem and shows its paler underside; all of it in the vertex shader, nothing on the CPU |
| Lighting a mass of leaves | dense (bulk normals) | each leaf is lit partly by the normal of the mass it belongs to (crown centre outward), plus ao that darkens leaves deep inside: thousands of cards read as one rounded form with a lit side and a shade side; translucency where the sun is behind |
| Grass | dense (`blocks/dense/ground.js`, `flora.js`, `gallery/dense/r2_ground.png`) | dense procedural blades placed from the instance id over tiles around the camera (no per-blade data), lit by the ground's bulk normal, bent by the same travelling gusts, parted by things moving through it |

What did **not** work: volume's trees move as a single blob, because a
volume can only bend as a whole. bro.scene's current wind (`windDelta` in
`src/scene/vulkan/shaders/scene_camera.glsl`) has the same problem: one sine per vertex,
weighted by vertex colour R, with no gusts and no hierarchy.

## Where each piece lives

- **broflora (C++ emit)**: the geometry and the per-vertex data motion and
  lighting need: branch weight, a per-limb phase, the leaf's stem anchor and
  flutter axis, the plant's root and height, the crown centre and radius. Better
  geometry: kinked, smooth-joined branches, leaf clumps, crown interior depth.
- **bro.scene (engine)**: a wind field with travelling gusts, which replaces the
  single sine and stays backward compatible with `setWind`. A vegetation path in
  the mesh/instanced shaders: hierarchical sway, bulk-normal blend, crown ao,
  translucency, a two-sided leaf underside. The same sway runs in the shadow pass.
  A grass node.
- **bromesh**: leaf-card and scatter changes only where the emit needs them.
- **bro.flora JS**: defaults that wire it all up, so the simplest app looks right.
- **broworkshop demos/flora-lab** (or a new grove demo): the showcase and the
  oracle. A grove of grown trees and a meadow on uneven ground at golden hour,
  judged in motion against the reference galleries.

## Iteration loop

What a look change costs, and how to keep it cheap (broworkshop
`demos/forest` and `demos/grove`):

- **Growth is cached.** The demos grow through
  `world.grow(ticks, dt, { cache: bro.app.cacheDir })` (C++ `growCached`,
  `include/broflora/world_io.h`). The key covers the world before growth,
  ticks, dt, the build and `BROFLORA_SIM_HASH`, a build-time hash of the
  step's source and bromath's headers. An edit to the step regrows once
  (the forest nursery takes about 5 s). An edit to emit, clumps or the JS
  keeps the cache. A forest boot is about 5 s to the first frame, a grove
  boot about 1 s.
- **Shader edits need no rebuild.** `BRO_SCENE_SHADER_SOURCE=1` compiles
  bro's built-in scene shaders from its source tree at start-up
  (`bro/docs/hot-reload.md`). Rebuild once when you are done.
- **C++ edits rebuild only what they touch.** Each built-in shader
  recompiles only when an include it reads changes. A one-file broflora or
  engine edit relinks libbro: about 30 s on Windows (35 s for a shared
  .glsl).
- **One boot, many captures.** Join `capture.js` commands with `+`, e.g.
  `-- still above,glade + handover impostors + trace`.
- **Profile the step** with `broflora_bench_nursery`. It runs the forest's
  nursery and prints ms per phase and a checksum of the grown world, so a
  faster step can be shown to grow the same plants.

## Step 1 as built: the vegetation records

broflora computes one `SegmentSway` per segment (`emitPlantSway` /
`emitWorldSway`, `include/broflora/sway.h`) and every
geometry emitter turns it into **vegetation records**, the data bro.scene's
vegetation shaders (`src/scene/vulkan/shaders/scene_vegetation.glsl`) sway in
the scene's wind field. Per vertex (`MeshData::vegetation`, bromesh) or per
instance a record is 16 floats in the emit's space:

| Floats | Meaning |
|---|---|
| 0-3 | root.xyz, height: the plant's origin and height above it |
| 4-7 | crown.xyz, crownRadius: the leaf-mass weighted crown centre and the farthest leafy segment end from it; radius 0 on wood (no bulk normal) |
| 8-11 | anchor.xyz, branch weight: a leaf's or bloom's attachment point on its segment, or a bark vertex's skeleton point; the weight there |
| 12-15 | axis, twig, limb, trunk: packed words (step 4): a leaf's stem direction, 0 on wood (no flutter); the limb's phase and its sprays' cantilever; the limb's; the trunk's |

Per segment (the GPU tube and scatter paths) it is 16 floats too: root.xyz,
height, crown.xyz, crownRadius, weightFrom, weightTo, 0, 0, 0, twig, limb,
trunk. (Steps 1-3 had axis.xyz and phase in 12-15 and 12 floats a segment.)

| Emitter | Records |
|---|---|
| `emitPlantMesh` / `emitWorldMesh` (`bakeVegetation`) | per bark vertex, wood, the weight at its ring |
| leaf meshes (JS `emitFoliageMesh`, `emitPlantFoliageMesh`; C++ `leafVegetation` over `LeafPlacements::segment`) | per leaf vertex: anchor = placement point, axis = leaf forward |
| bloom meshes (`emitBloomMesh`) | per bloom vertex: anchor = the bloom, axis = its normal, from `BloomAnchor::segment` |
| `emitBranchTubes`, `emitScatterSegments` | 16 per kept segment (`appendSegmentVegetation`) |
| `emitFoliageVegetation`, `emitSegmentVegetation` | 16 per leaf / segment instance, in lockstep with the transforms |

bro's side (the wind field, `setWind` / `windAt`, the shaders, the shadow
pass) is documented in bro's `docs/scene-api.js` and `docs/scene-nodes-api.js`
"Vegetation"; the JS surface in bro's `docs/flora-api.js` "Vegetation records".

## Step 2 as built: the look

What changed, and where:

- **Crown clumps** (`include/broflora/clumps.h`, bromesh
  `procedural/leaf_clumps.h`).
  - A plant's leaf mass is a set of clumps picked greedily from its leafy
    twig tips, outermost first, each kept only where it stands clear of the
    others (`ClumpOptions::spacing`), so the silhouette fills first and sky
    shows between clumps.
  - A clump's radius follows its crown; its density follows its twig's
    `foliageDensity` (the density policy now lives in the library, not the
    API).
  - bromesh `placeLeavesInClumps` fills three overlapping lobes per clump,
    leaves in a shell under the lobe surface, facing outward.
  - bro.scene's scatter node does the same on the GPU (`clumps: true`, the
    same hash draws). A leaf's records are its twig tip's, so a clump rides
    its twig.
  - Clumps are the default of every foliage emit; `clumps: false` gives the
    old per-twig spray.
- **Kinked bark** (`Species::kink`, default 0.2). Each node of each module
  steps sideways by a hashed fraction of its edge, accumulated down the
  module tree, in `emitPlantSegments` (and so in every bark, bloom and leaf
  emit).
  - Joints stay closed.
  - Emit only; the simulation never sees the kinks.
- **Joined tubes.** `emitBranchTubes` writes `tangents`, the bisector at each
  joint between a segment and the child that continues it, and the tube
  node mitres its rings on it. A continuation starts at its parent's radius;
  a side shoot starts with a small collar.
- **The rigid-segment seam.** The `emitSegmentVegetation` records are now
  the segment form (`writeSegmentVegetation`): crown floats = tip,
  -(1 + weightTo). The shader interpolates the weight along each instanced
  cylinder, so a segment's tip bends with its child's base.
- **Colour from the records** (bro `vegetationTint`). Each plant leans warm
  or cool, and each limb and leaf varies in value. Leaves deep in the crown
  are darker and cooler, and the rim is lighter. Bark varies by plant. A
  node's colour is the species mean, and the crowns come out varied around
  it.
- **Reach.** A limb's sway grows with plant height, because longer limbs
  sweep further. This makes a 7 m tree's motion read at grove distance.

The grove demo (broworkshop `demos/grove`) has these features:

- uneven, colour-varied ground rising to hills;
- plants standing on it;
- thicker trunks (`pipeExp` 2.0, `leafDiameter` 0.028);
- a darker leaf mean;
- golden-hour haze pushed past the grove;
- an under-the-crown close shot.

**Left for step 3:** the grass node. The meadow is still a coloured
heightfield. Its colour patches are where grass density and hue should come
from.

## Step 3 as built: the grass

- **`scene.createGrass(opts)`** (bro `scene/grass_node.h`,
  `vulkan/shaders/scene_grass.glsl`, `scene_mesh_drawer_grass.cpp`; API in
  bro `docs/scene-nodes-api.js`). Dense's meadow, ported: no per-blade data,
  three rings of cells around the camera (6 cm, 20 cm, 55 cm cells out to
  20 / 55 / 150 m at the default range; step 5 made them four rings of
  tufts), each one instanced draw over only
  the cells of its visible sector. Each blade comes from its cell's hash, the
  clump noise and the node's **field**: a grid of ground height, density,
  dryness and a height multiplier that the JS side bakes once from the ground
  (a mesh node's top surface, a terrain, a clipmap or a height function) and
  from `density` / `dry` / `tall`, numbers or functions of (x, z, y).
- **Wind** is the scene's field: the steady lean, a per-blade flutter and the
  travelling gust bands, which lift the sheen of what they bend. A calm
  (`speed 0, gust 0`) is still to the pixel.
- **Light**: blades take most of their shading from the ground's normal
  (bulk 0.72), dark at the root, translucent like leaves (the same mesh.frag
  vegetation path, with the blade's ao and fade in `vegShade.w`), each blade
  its own value and a little hue. Far off, a blade widens and loses its dark
  root and its translucency and is lit as the ground, so a hillside reads as
  turf rather than as a field of bright tips.
- **Movers**: `grass.addMover(node, {radius, strength, trample})` and
  `grass.push(x, z, ...)` stamp a time-stamped influence map (a 102 m window
  of 20 cm cells that follows the camera; sub-rect uploads). The shader decays
  it from the stamp time: blades spring back in about half a second, and
  flattened grass lies the way it was pushed, leaning from the root, paler,
  rising over about 20 s.
- **bro.flora**: `world.groundCover({scale})` is the `density` function for
  grass among grown plants: thin in the crowns' shade (the shadow grid's light
  just above the ground), clear at each trunk.
- **Grove**: the meadow and hills are grass, lush to sun-dried from the same
  patches that colour the (now darker, turf-coloured) soil, thin under the
  trees by `groundCover`. A ball rolls a loop through it as a mover. Shots
  `meadow`, `ground` (in the grass), `parting` and `roller`; `capture.js perf`
  measures the grass's GPU cost.
- **Cost** (1280x720, RTX 4090, perf.gpuFrameMs over 60 frames): the grass is
  1.6 to 2.2 ms of a 12.7 to 13 ms grove frame (10.7 to 11.4 ms without it),
  about 210k / 110k / 100k instances in the three rings.

Still short of dense: close up, the sward is less crowded and less
contrasty than dense's (`r2_walker`, `r2_ground`), partly the golden-hour
light; a flattened lane is visible but quieter than a walker's in dense; the
far ring's sunlit hills still show individual blades rather than a texture;
only the near ring casts shadows, and none by default.

## Step 4 as built: wood and leaves that move like wood and leaves

### The model

A plant is three levels of **cantilever**, each a beam fixed at its base in
its first bending mode (`include/broflora/sway.h`, `src/sway.cpp`):

- **trunk**: the leader chain, root to the plant's height;
- **limb**: each subtree leaving the trunk, from where it leaves to its
  farthest tip;
- **spray**: the leafy shoots at a limb's tips. The simulation stops
  resolving shoots at its thinnest segment (about 2 cm through), so a spray
  is at least `kSwaySprayLength` (0.5 m) long and at most
  `kSwaySprayDiameter` (8 mm) thick at its base.

Each level has two terms, baked once at emit from the simulation's geometry
(the pipe model's diameters and lengths, the foliage):

- **freq**, `sqrt(k / m) / 2 pi` (Rayleigh). The tip stiffness is
  `k = 3 E I / L^3`, with `I = pi d^4 / 64` at the base diameter. The
  mass the mode moves is `m`: every segment's wood and leaves times
  `phi(s)^2`, where `phi = s^2 (3 - s) / 2` at its place `s` along the
  level.
  - For a bare beam this is Euler-Bernoulli's `f ~ d / L^2`, to 1.4%.
    Thick and short is quick, long and slender is slow.
  - What a stem holds far out slows it further.
  - So the trunk, long and holding the crown, is the slowest even though
    it is the thickest.
- **sway**, the static tip deflection over the length at 10 m/s:
  `F / (k L)`.
  - `F` is the drag on the leaves the level carries,
    `0.5 rho Cd A U^2`, each piece weighted by `phi(s)`.
  - `A` is the crown's leaf face: `kSwayCrownFill` (half) of the disc of
    its radius. It is shared among segments by foliage (the simulation's
    density times length is only relative).
  - At one length and load, twice as thick deflects 16x less, and twice as
    long deflects 4x more (the new `sway_cantilever_terms` test).

The physical constants are textbook values: green hardwood E 9 GPa and
800 kg/m^3, Cd 0.3 for a crown streamlined at 10 m/s, and 0.5 kg of leaves
per m^2 of leaf face. The **gains** divide each level's stiffness for what
a uniform beam on a rigid footing leaves out:

- trunk 10: its taper and the root plate turning in the soil;
- limb 4: the give at its fork;
- spray 1.

They were set against field ranges. The grove's trees (about 10 m drawn)
come out like this:

| Level | Frequency | Deflection at 10 m/s |
|---|---|---|
| Trunk | 0.56-0.74 Hz | 1.2-1.9% of height |
| Limb (median) | 0.53 Hz | 2.9% |
| Spray | 5.6 Hz | 2.9% of 0.76 m |

bro.scene (`scene_vegetation.glsl`) drives each level with the drag at the
plant, `(U / 10 m/s)^1.3`; a crown streamlines, so its drag grows slower
than `U^2`. Each level answers as a **forced, damped oscillator**:

- **Mean bend**: its static bend under the drag a quarter period back, so a
  heavy trunk lags the gust.
- **Ringing** at its own frequency, combining two RMS terms:
  - the turbulence the gust field does not resolve, at resonance. This is
    the narrow-band response to a -5/3 spectrum: mean drag times
    `f^-1/3 / sqrt(zeta)`.
  - the gusts' own change over its last half period. Only a level slower
    than that change rings from it.
- **Damping ratios**: trunk 0.15, limb 0.08, spray 0.05. The trunk is
  damped most because its limbs take its energy (multiple-mass damping,
  James et al.).
- **Shape and hierarchy**: a level bends in a loaded cantilever's shape
  `phi(s)`, and the levels add down the hierarchy.
- **Rotation**: the wood rotates with the bend's slope, so bark normals
  follow it.
- **The drag's past** is the travelling gust field read at t, t - 0.4 s and
  t - 1.3 s: three reads a vertex, shared by the levels.

**The oracle** is broworkshop `demos/grove/sway_check.js`. It reads the
grove's own terms from the records, then compares the shader's response,
ported line for line, against an integrated damped oscillator. The
oscillator runs under the same drag over 60 s, plus 15% unresolved
turbulence. Deflection RMS and speed agree within 15% at every level, in
breeze and in gusty.

### Leaves

`vegLeaf` adds three motions on top of the wood:

- A leaf **turns with the wood it hangs from** (the pose's rotation at its
  anchor), so its normal and shading follow its limb.
- It **flutters about its petiole**: a twist about its stem and a smaller
  pitch, at 3-9 Hz on its own phase. The amplitude is `0.35 sqrt(drag)` in
  bursts, so a calm is still.
- In a **gust** (drag past a light breeze's) it **turns part way downwind**
  and **folds along its midrib**, both halves rising.

### Leaf shading and edges

- **Lighting** (`scene_lighting.glsl`): a leaf's diffuse wraps past the
  terminator, `(N.L + 0.3) / 1.3`.
- **Shadow**: one shadow lookup with a filter twice as wide, floored at
  0.1, shared by the direct and the through-leaf light. Before, two lookups
  stepped hard from leaf to leaf.
- **Cup**: the grove's card is cupped (`cup` 0.55; bromesh `leafCard`
  recomputes normals), so its normal varies across the blade.
- **Edges**: leaves are cut-out geometry, not alpha-tested, so the grove
  turns on 4x MSAA.

### Trunks close up

- Tube rings are framed by a minimal rotation from +Y to the joint's
  tangent (`tubeRingFrame`), not `cross(axis, Y)`, which flipped on
  near-vertical trunks.
- The rings are placed in the joint plane at the segment's end point, so
  the two segments share the ring and its normals: there is no seam, crease
  or twist, and the kink stays.
- The grove draws 10 sides.

### Forest scale: what the LOD step should know

The pose is two layers:

- **`vegPose(VegPlant, VegLimb, VegWind, rest, weight)`** is the shared one.
  Every tier calls it with the same data and gets the same offset and
  rotation, so tiers crossfade without swimming:
  - per tree (`VegPlant`): the root, the height and the trunk word, from
    record floats 0-3 and 15;
  - per limb (`VegLimb`): the twig word (phase and spray cantilever) and the
    limb word, floats 13 and 14;
  - per point: its rest position and its branch weight (floats 8-11, or a
    segment's interpolated weight);
  - `VegWind`: the drag taps at the root, computed once per vertex.

  A clump card or an impostor needs only those three words plus a weight
  per point. One tree's whole motion fits in one record.
- **`vegLeaf`** is the near tier's own leaf layer (flutter, gust turn and
  fold), and needs the leaf's axis (float 12).

### Cost

GPU ms from `capture.js perf` at 1280x720 on an RTX 4090, mean
`perf.gpuFrameMs` over 60 frames:

| Shot | Before step 4 | After, no MSAA | After, 4x MSAA (the grove's default) |
|---|---|---|---|
| grove | 12.95 / 11.33 / leaves 11.02 | 4.94 / 3.67 / 3.33 | 6.75 / 5.45 / 5.06 |
| tree | 13.06 / 11.23 | 4.95 / 3.78 | 7.35 / 5.13 |
| close | 12.91 / 11.18 | 4.85 / 3.59 | 5.84 / 4.79 |

Each cell is frame / without grass / leaves.

- **Draw batching.** The 11 ms was not shading but the GPU front end paying
  per instance: 398k leaves, one 24-vertex instance each. The scatter now
  draws K copies of the leaf mesh per instance, each copy about a thousand
  vertices, with the leaf index computed from the instance and vertex.
  Leaves fell to 3.5 ms (bro 7a5974ab).
- **The new motion** costs no more than the old per vertex: leaves went
  from 3.48 to 3.33 ms over 9.6M leaf vertices a pass. That is 3 gust reads,
  3 cantilever levels (one `pow` each) and 2 small rotations.
- **MSAA** is the new cost: 2x adds about 0.8 ms, 4x about 1.7 ms.

**Still open:**

- The leaves' pose is computed per vertex, though all 24 vertices of a leaf
  share it.
- `windSwayReach` (the shadow-tile cull margin) still uses the old sway
  model's formula, which bounds the grove's motion.

## Step 5 as built: a lush sward

Step 3's near grass covered about half the ground with one 2 cm blade per
6 cm cell (about 280 blades a square metre), over soil the colour of the
blades. Real meadow blades are 3 to 6 mm wide, thousands to the square
metre.

### Tufts and rings

An instance is now a cell's several blades: the ring mesh is that many
copies of the blade end to end, and the shader finds its blade from the
vertex index (`GrassNode::ringMesh`, `scene_grass.glsl`). Four rings, at the
default range of 150 m:

| Ring | Out to | Cell | Blades a cell | Segments | Width | Blades a m² |
|---|---|---|---|---|---|---|
| 0 | 7 m | 10 cm | 14, a tuft | 4 | 4 mm | 1400 |
| 1 | 20 m | 16 cm | 8, a tuft | 3 | 1.6 cm | 310 |
| 2 | 55 m | 30 cm | 5, over the cell | 2 | 4 cm | 55 |
| 3 | 150 m | 75 cm | 5, over the cell | 2 | 10 cm | 9 |

- **Cover is the invariant.** Blades a square metre times width (the
  blades' share of a view across the ground) stays about level from ring to
  ring, while vertices a square metre fall about tenfold each step. Widths
  also grow 3% a metre and never fall under two thirds of a pixel.
- **Thinning.** Past half its outer edge, rings 0 and 1 keep `thinFrom / d`
  of a cell's blades (at least half), each widened by the inverse, so the
  outer edge of a ring already looks like the next.
- **Tufts.** A blade stands up to half a cell from the root, uniform over the
  disc. It leans out from the root, outer blades most (`fan`: 0.5 rad near,
  0.35 in ring 1), with its face turned out. The density thins blade by
  blade, so a thin patch is sparse tufts rather than missing ones.
- **Soil.** The grove's soil is darker and cooler than the blades (lush
  `[0.028, 0.038, 0.022]`): the shade at the foot of a sward, so the gaps
  read as depth.

The bulk normal, gusts, movers and dithered handovers are unchanged.

### Cost

A vertex costs about the same whether the shader culls it at its first test
or draws it. So culling inside the shader saves nothing; only not issuing
the vertex does. Two changes follow from that:

- **Tiles in view.** The drawer splits each ring's visible box into tiles of
  16 x 16 cells. It keeps only the tiles that reach the ring, where
  something grows, and whose box (ground to the tallest blade) is in the
  view frustum. The field's coarse blocks (`GrassNode::fieldRange`) give
  each tile's height range. It issues one draw per run of kept tiles in a
  row, with the run's cell rectangle in the model matrix's first row.
- **Shadow casters.** These keep a third of the near ring's blades,
  widened. A grass caster costs little; what costs is that a casting grass
  makes the whole shadow map re-render every frame (about 0.5 ms in the
  grove, mostly the leaves).

GPU ms from `capture.js perf` at 1280x720, RTX 4090, 4x MSAA, mean
`perf.gpuFrameMs` over 60 frames, grass casting:

| Shot | Frame before | Frame after | Cells issued before | Cells issued after |
|---|---|---|---|---|
| ground (in the grass) | 5.18 | 4.72 | 211k / 112k / 100k | 9k / 17k / 28k / 12k |
| meadow | 6.49 | 5.98 | 206k / 109k / 97k | 9k / 18k / 29k / 16k |
| grove | 6.61 | 5.75 | 213k / 114k / 101k | 0 / 15k / 29k / 17k |

The grass's own share (frame minus frame without it) in the ground shot fell
from 1.35 to 0.89 ms.

### What the forest LOD step should know

- **Blade work is per vertex issued.** Budget grass by vertices issued, not
  by blades drawn. The tile cull is the lever. A tighter cull would pay too:
  for example by the depth of the trees in front, or a GPU compaction pass.
- **Under a canopy.** `groundCover` thins the grass in shade, but the cells
  are still issued and culled vertex by vertex. A forest floor wants the
  density in the field's coarse blocks: `fieldRange` already skips tiles
  where density is 0. A density floor under it (0 below some shade) would
  skip whole tiles under closed canopy.
- **From above the canopy.** The rings follow the eye's x and z only. Seen
  from 50 m up, ring 0 is issued under the camera and is all sub-pixel. The
  rings should scale with the eye's height above the ground, or start
  thinning by distance in 3D rather than across the ground.
- **One shadow-map cost.** A casting grass re-renders the whole shadow map
  every frame. At forest scale, grass should cast only into the nearest
  cascade (or a tile of its own), or not cast and take a contact-shadow
  term instead.
- **Far turf.** Ring 3 out to 150 m is cheap in a meadow, but a forest view
  hides most of it behind trunks. Past the trees' impostor distance, the
  ground's own colour (darkened by the same dry and density masks) can stand
  in for blades.

## Step 6 as built: forest scale

A forest is many trees from a few grown specimens. Only the nearest few
trees deep need every leaf and its flutter; further out a crown is read by
its masses and its gusts. Every tier is posed by the same `vegPose` from the
same records, so a handover swaps detail and never moves the tree.

### The API

- **`world.emitForest(foliage)`** (broflora) emits the world's grown plants
  as specimens: one buffer of bark segments, crown clumps and leaves, each
  record in the vegetation layout, with a specimen table.
- **`bro.flora.plantForest(emit, {area, spacing, jitter, seed, ground,
  density, scale, sink})`** stands specimens on a jittered grid, 8 floats a
  tree.
- **`bro.flora.forestCover(emit, {instances, floor, closed})`** is a
  density function for `createGrass`: full in the open, down to `floor` in a
  crown's shade, nothing where `closed` crowns overlap, clear round a trunk.
- **`scene.createForest({forest, instances, leaf, lod, ...})`** draws them.
  `node.forestStats()` reports trees at each tier, vertices sent, and the
  detail factor; `setForestTier()` pins one tier for comparisons.

### The tiers

The tier is chosen per tree each frame by size on screen, so the distances
scale with the resolution and the field of view. Let `leafPx` be a leaf's
length in pixels at the crown (the nearest point of an upright cylinder
around the clumps, so a tree under an overhead crown counts as near), and
the widening `w = max(1, leafPixels / leafPx)`.

| Tier | When | What draws | Motion |
|---|---|---|---|
| leaves | `w` < `clumpFrom` (3) | every leaf while `w` is 1; past it `1 / w²` of them, each `w` times larger, so the crown's cover holds; 6-vertex cards past `cardWidening` (1.6) | `vegPose` + `vegLeaf`, flutter fading out by `flutterFade` (2) |
| clumps | `w` past `clumpTo` (4.5), until the mean clump is `crownFrom` (5 px) | each clump as three camera-facing lobes with a leafy cutout, lit as spheres, a leaf-scale shade inside | `vegPose` at the clump's centre on its limb, with its weight |
| crowns | mean clump under `crownTo` (3 px) | one leafy disc a tree | `vegPose` at the crown centre, on the central clump's limb at the clumps' mean weight |

- **Handovers** are dithered: through `clumpFrom..clumpTo` and
  `crownFrom..crownTo` both tiers draw, splitting each pixel between them,
  so a tree is covered once and never doubled.
- **Bark** keeps the segments thicker than `barkPixels * w` on screen,
  thickest first, measured to the tree's box at rest. Twigs go as fast as
  the leaves widen.
- **The horizon** is the crown tier. Each crown sprite carries the gust at
  its tree and turns paler as a gust shows the leaves' undersides, so gusts
  roll across the far canopy as lighter bands. There is no separate canopy
  surface.
- **Pose agreement** (bro `tests/flora/test_forest.js`): under a steady
  9 m/s wind, the silhouette of the same tree moves 3.14 / 2.82 / 3.03 px
  as leaves / clumps / crowns.

### The budget

`lod.budget` (14 M vertices) caps the leaves and bark sent. When the trees
in view would send more, a per-frame bisection scales `leafPixels` and
`barkPixels` together by a detail factor (at most 8). Every tree thins alike
and the furthest hand over to clumps first. Sprites are not counted, since
they cost one quad a clump. Shadows choose their tiers per shadow tile
against `lod.shadowBudget` (6 M), handing over to crowns twice as early.

### Grass at forest scale

Each item from step 5's list:

- **Under a closed canopy.** A field density under 0.06 counts as none, so
  whole tiles go unissued. `forestCover` gives the density.
- **From above.** Once the eye is 2 m over the field, ring distance is
  measured in 3D from the eye, so the near rings are not issued under a
  camera above the canopy.
- **One shadow-map cost.** Grass casts into each sun's first cascade only.
- **Far turf.** The forest demo stops the grass at 100 m and colours the
  ground by the same cover and dryness, so it stands in past that.
- **Canopy shade.** The near cascades' tree shadows fall on the grass, and
  `forestCover` thins it under crowns. There is no shade term past the
  shadow range.

### Cost

From broworkshop `demos/forest` (`capture.js trace`), at 1280x720 on an
RTX 4090 with 4x MSAA. The test scene is 12,878 trees from 24 specimens on
900 m of terrain. A 34 s flight runs from 150 m over the ground, down across
the crowns, into a glade and in among the trunks.

GPU ms a frame over the flight's 1021 frames, two runs:

| | Max | p95 | Median |
|---|---|---|---|
| run 1 | 8.26 | 5.09 | 4.30 |
| run 2 | 7.32 | 5.51 | 4.36 |

| Part of the flight | Frame | Leaves + bark sent | Trees as leaves / clumps / crowns |
|---|---|---|---|
| 150 m up | 3.1 ms | 2.7 M | 0 / 3.3k / 6.5k |
| skimming the crowns | 5.0 ms | 16.6 M | 150 / 3.3k / 3.5k |
| among the trunks | 4.0-4.7 ms | 16.4 M | 45 / 3.1k / 2.4k |

- **The max is a transient.** Each run has one plateau of about 25 frames
  (8.2, then 7.1, then 6.3 ms, stepping down every 8 frames). It falls at a
  different point of the flight in each run (8.3 s, 16.1 s), so it is not
  the scene. Without it, the worst second of the flight is 5.8 ms.
- **The budget holds.** The view's leaves and bark stay at the 14 M budget
  from 44 m up, with the detail factor rising to about 3 among the trunks.
  The shadow pass stays at its 6 M.
- **The grove still costs what it did:** 4.66 / 6.14 / 6.06 ms for the
  ground / grove / meadow shots, against 4.72 / 5.75 / 5.98 at step 5.

**Still open:**

- 24 specimens stand in for 12.9k trees, so repeats are visible from above.
- The dither of a handover shows as a stipple while it runs.
- Clumps read darker and rounder than the leaves they replace, so the
  leaves-to-clumps handover is visible in a dolly
  (`capture.js handover clumps`).
- Backlit near leaves read pale grey-white.
- The leaves' pose is still computed per vertex.

## Step 7a as built: far trees as impostors

**Removed** in the next section ("Step 7a, mid range"); kept here as the
record of what was tried and why it went.

The tree half of step 7 (the ground and the bark are 7b). The clumps and
crowns are replaced as the far tier by an impostor per tree: a card showing
its specimen's own leaves, so the far forest is the leaves tier seen
smaller, not a different drawing of it. Clumps and crowns remain behind
`lod.impostors: 0` and the demo's Clumps / Crowns buttons.

### The bake

`bro/src/scene/forest_impostor.cpp`, on the CPU, on the forest's first
frame (about 1.6 s for 24 specimens):

- Each specimen is drawn from 8 x 8 directions over the upper hemisphere
  (hemi-octahedral), 128 texels a side each (`impostorViews`,
  `impostorTexels`). Both atlases are 5120 x 5120 RGBA8, mipmapped.
  - The colour atlas holds sRGB albedo and coverage.
  - The data atlas holds the normal's xy, the depth toward the view and
    leafiness (leaf or bark).
- The leaves and bark are the very records the leaves tier draws, rested.
  Each leaf's albedo is its tint without the per-tree part. The per-tree
  tint (`vegTreeTone` in scene_vegetation.glsl) is applied at run time, so
  impostors of one specimen differ as its leaves would.
- **Crown shade is baked:** a leaf is darkened by how far it lies behind the
  crown's front near it, `0.55 + 0.45 exp(-behind / (0.08 half))`. The front
  is a separable max over about two leaf lengths, so depth and interior
  darkness come from the crown's real shape. They do not come from a sphere.
- Each specimen also records an upright cylinder (radius across, half
  height) that bounds its leaves. Cards are sized by it.

### The card

One camera-facing card a tree (6 vertices), posed by the same vegPose terms
as every other tier:

- the trunk's bend at each height;
- the crown's limb bend from 0.25 to 0.6 of the height up.

Details:

- **Size and place.** The card is the smaller of the bounding sphere's cone
  and the cylinder seen from the eye. It stands at the sphere's front, so
  it overdraws little.
- **The trace.** Each pixel traces its ray into the three views round its
  direction, at the depth each texel carries. It writes that depth through
  `depth_less` (reversed-Z), so the depth buffer, shadows and fog see the
  leaves' surface and not the card.
- **Light.** The light stays put as the picture sways, so a crown's light
  and shade hold in a gust.
- **Lit as the leaves are:**
  - the leaves' tint, translucency, crown AO and shadows;
  - a 0.6 blend toward the bulk normal;
  - leaf-sized cells of the picture (in specimen space, so they ride with
    the tree), each turned its own way at full strength and toned ±22%,
    fading out as a cell drops under about 2 px;
  - flutter on the cell's phase.
- **Coverage** is sharpened to the pixel (`(c - 0.5) / fwidth(c)`), so an
  edge is a leaf's edge and not a blur.

### The handover

- **Leaves to impostor.** This runs over the same widening as leaves to
  clumps (`clumpFrom`..`clumpTo`).
  - It is held back while the picture would be magnified: the tree's
    pixels against its baked texels, from 1.2 to 1.8 times. So a near tree
    never shows a soft picture; at 1920 wide, from 150 m, the few trees
    under the eye keep their leaves.
  - The bark goes with the leaves, since the picture holds it.
- **The dither.** A white-noise integer hash replaced the gradient noise,
  which hatched diagonally. Under 4x MSAA each sample takes its own share
  of the split (`forestSampleMask`), so a handover blends in quarters
  rather than stippling.
- **Sort.** Impostor entries are sorted nearest-first, both to the eye and,
  per shadow tile, to the light, so early-Z rejects what is behind.

### Shadows

- **What casts.** An impostor casts the picture seen from the light: one
  view, the nearest to the light. Everything constant over the card is set
  up per vertex; a fragment does one ray-plane hit and two reads a mip
  coarser. It writes the leaves' depth plus 0.08 of the radius, through
  `depth_greater`.
- **Gaps.** A gap writes the far plane rather than discarding: a discard
  turned early-Z off for overlapping casters and cost about 2.5 ms.
- **Sparse crowns.** Coverage is compared with a per-texel hashed threshold
  (0.15 to 0.95), so a sparse crown casts a half shade, as its leaves would.
- **Budget.** Impostors cost a card, so they sit outside both vertex
  budgets. The view's budget went from 14 to 18 M with what they saved.

### Cost

From `capture.js trace`, 1280x720, 4x MSAA, RTX 4090, gusty wind. GPU ms a
frame over the flight's 1021 frames:

| Run | Max | p95 | Median |
|---|---|---|---|
| step 6 sprites (budget 14, same harness) | 7.75 | 5.28 | 4.50 |
| impostors, budget 14 | 6.06 | 4.50 | 3.95 |
| impostors, budget 18 (default), run 1 | 6.77 | 6.40 | 4.46 |
| impostors, budget 18 (default), runs 2 and 3 | 6.04 to 6.07 | 5.10 | 4.42 |

Run 1 shared the machine with other GPU work; runs 2 and 3 agree to 0.03 ms.

- **No plateau.** The impostor runs show none of step 6's transient
  plateau of about 8 ms. Their worst second is in among the trunks at
  ground level, where the budget is spent on leaves.
- **Far trees are almost free.** 150 m up is 7.8k impostors and 47 k
  vertices at 3.35 ms. Skimming the crowns is about 290 leaf trees and 4.8k
  impostors.
- **Shadows.** Impostor shadows went from about 5 ms (wide cards, three
  views, discard) to about 1.6 ms as built.
- **The grove is unchanged:** 4.69 / 6.19 / 6.08 ms for the ground / grove
  / meadow shots, against 4.66 / 6.14 / 6.06 at step 6.

### Captures

In broworkshop `demos/forest/captures/`:

- `step7a_<shot>_<sky>_compare.png`: Leaves beside By size, for shot
  above / canopy / skim / glade / trunks and sky noon / golden.
- `step7a_handover_leaves_strip.png` and
  `step7a_handover_impostors_strip.png`: dolly strips through each
  handover.
- `step7a.webm`: the flight.
- `b18_trace.json`: the trace.

**Still open:**

- **Backlit impostors are too bright.** Mid-distance impostors backlit at
  golden hour (the trunks view) read as bright, flat cutouts beside the
  leaves tier. Their trunks are gone, because the bark leaves with the
  leaves while the picture's own bark is mostly hidden by its crown.
- **Golden hour runs dark.** Impostors are about 5% darker than the leaves
  at golden hour (luminance 131 vs 138), with less blue. Noon matches.
  Large-scale contrast runs a little high (sd 35 vs 28), fine detail a
  little low.
- **Thinning shows as flakes.** Under the budget, the leaves tier thins to
  large widened leaves (`handover leaves` strip, 13 to 16 m).
- **Backlit near leaves are still pale grey-white.** This is the leaves
  tier, not the impostors.
- **Specimens still repeat.** 24 specimens still repeat from above. An
  impostor varies only by its tree's tint, turn and scale.
- **Shade stops at the shadow range.** Canopy shade still ends past the
  shadow range.
- **The compare image has a black band.** `capture.js compare` leaves a
  28 px band at the top: the headless frame is taller than innerHeight.

## Step 7a, mid range

The impostors made the mid range (about 15 to 100 m) worse than the step 6
clumps: chunky, posterized flat cutouts, and big flakes where the near
leaves thin. They also cannot work in an infinite procedural world, where
there is no canopy to take a picture of before the tree streams in. So they
are **removed** (not just off by default): the bake cost 1.6 s and about
200 MB of atlases, and nothing may be baked per tree. Every tier past the
near leaves is now drawn at run time from the tree's own records (clumps,
leaf counts, leaf size and placement), cheap per tree on stream-in, so every
tree may be unique.

### Design

Three tiers again, leaves / clumps / crowns (`kTiers` 3), each drawing
leaves rather than a picture of them:

- **Clusters** (the thinned leaves past `cardWidening`, `lod.clusters`,
  default 1). A kept leaf widened k times stands for the k^2 leaves round
  it. Its card is a hexagon in the leaf's plane spanning the widened outline
  and half a leaf more, and the fragment shader places true-size leaves in
  it on a jittered cell walk (2x2 cells), each turned and toned its own way,
  growing in over the outer part of the outline. So a thinned crown shows
  leaves of the right size, not flakes. Clusters have their own pipeline
  (mesh.frag built with `SCENE_FOREST_CLUSTER`). Shadows still cast
  the widened cards.
- **Clumps and crowns** (`scene_forest_mass.glsl`): a camera-facing card a
  lobe (three a clump) or a crown, whose pixels place the leaves of the
  sphere's shell, one per jittered cell, each a prefiltered ellipse
  (`forestLeafCover`), with its own normal, turn and tone. The far half of
  the shell is seen through the near half's gaps. Coverage goes to the MSAA
  sample mask and depth is written through conservative `depth_less`, so
  the depth buffer, fog and shadows see leaves, not a card.
  - **Mean once a pixel holds several leaves.** From fp/cell 0.7 to 1.4 the
    exact layer blends to its analytic mean; the far half is always the
    mean. The mean carries a GRAIN: the wander a pixel's few leaves have
    from the mean (count, facing, how many lie toward the light), about one
    over the root of their count, on the cells' own grid at the two octaves
    either side of the pixel, so it holds still as the crown moves and never
    gets finer than a pixel.
  - **Lit as the leaves are:** the leaf tint, the crown's depth tint and AO
    (`forestCrownTerms`), translucency, the normal blended 0.4 toward the
    bulk normal at 0.6, and the gust sheen.
  - **Self-shadow is analytic.** A sprite's leaves are shadowed by their own
    sphere as `exp(-sigma d)`, d the path out of the sphere toward the light
    and sigma from the leaf count and size (`forestMassDepth`). The leaf
    seen lies about a free path in, which adds `cos(view, light)` of that to
    the light's path: the mean factor is `1 / (1 + c)`, so a crown seen from
    above at noon is not lit as if it were its own surface. Everything else
    comes from one shadow-map tap at the exit point.
  - **Sprite casters** are discs as dense as their leaves:
    `1 - exp(-tau sqrt(1 - r^2))`, dithered on a 9x9 grid a radius. The disc
    stands half a radius past the centre away from the light, so it shades
    the sphere's far side and what lies beyond, not its own lit side (this
    removed most of the glade's mid-range darkness).
  - Thinned leaf casters stand back from the light by 0.35 of the length
    they grew, so a widened caster does not shade leaves above it.
- **Budget.** The view's budget is back to 14 M (it went to 18 only on the
  impostors' savings). Clusters and sprites cost fragments, not vertices.

### Numbers

`capture.js metrics`, Leaves against By size per band of distance to the
crowns. Y is mean luminance, drgb the distance between mean colours, flat
the share of flat patches, hard the hard-edge density. Leaves' own value
first, then cfb4ca44 (impostors), then this step:

| View, band | Y: Leaves / 7a / now | drgb: 7a / now | other |
|---|---|---|---|
| canopy noon 20-40 | 142 / 157 / 150 | 28.1 / 16.1 | |
| canopy noon 40-80 | 143 / 160 / 150 | 30.8 / 13.5 | |
| canopy noon 80-160 | 159 / 158 / 152 | 3.5 / 12.4 | hard 10.4 / 10.7 / 4.7 |
| skim noon 20-40 | 156 / 161 / 157 | 9.3 / 2.2 | flat 24.9 / 30.8 / 27.7 |
| glade noon 20-40 | 121 / 122 / 118 | 1.8 / 5.9 | |
| glade golden 10-20 | 142 / 155 / 145 | 22.4 / 5.3 | |
| trunks noon 20-40 | 123 / 133 / 123 | 14.1 / 8.6 | flat 5.2 / 17.8 / 5.2, d1 16.5 / 10.2 / 16.5 |
| trunks golden 20-40 | 135 / 137 / 137 | 11.6 / 10.4 | flat 4.4 / 17.7 / 4.3 |
| trunks golden 40-80 | 109 / 114 / 118 | 11.3 / 15.1 | |
| skim noon 80-160 | 178 / 172 / 167 | 8.6 / 17.7 | |

The flat cutouts are gone: flat and block now match the Leaves mode in
every band, where 7a had three times the flat patches among the trunks. The
mid bands are within about 7 of Leaves' luminance (7a: up to 17 brighter).
Fine contrast (d1, hard) in the mid bands is a little under Leaves'.

Trace (`capture.js trace`, 1280x720, 4x MSAA, RTX 4090, breeze, 1021
frames), GPU ms:

| Run | Max | p95 | Median |
|---|---|---|---|
| cfb4ca44, impostors, budget 18 | 6.04 to 6.07 | 5.10 | 4.42 |
| first clusters + sprites, budget 18 | 18.79 | 10.83 | 6.93 |
| now, budget 14, three runs | 7.65 to 10.67 | 5.25 to 5.78 | 4.08 to 4.09 |

The max and the spread of p95 come from one- to two-second bursts of 6 to
10 ms that land at different times in different runs (7 to 8 s, 18 s, 23 to
24 s), the transient documented at step 6; the run with one burst had p95
5.25. Outside them the worst second is the dive into the canopy (11 to
12 s), about 5.6 ms mean and 5.9 max. The grain costs about 0.15 ms.

The flight video shows no popping: its frame-to-frame change has no spikes
beyond the encoder's every-tenth-frame ones and the frames where the eye
passes through a crown.

### Captures

In broworkshop `demos/forest/captures/`:

- `mid7a_metrics.json`, `mid7a_trace.json`: the numbers above;
  `base7a_metrics.json` is cfb4ca44.
- `mid7a_<shot>_<sky>_compare.png` for canopy / skim / glade / trunks at
  noon and golden; `mid7a_canopy_zoom_canopy_noon_compare.png` is the
  canopy's near crowns blown up twice.
- `mid7a_handover_{cards,clumps,crowns}_strip.png`: dolly strips.
- `m18.webm`: the flight (the same shaders).

### Still open

- **Far crowns are too smooth.** Past about 80 m the sprites' grain gives
  back only part of the leaves' pixel texture (canopy noon hard 4.7 against
  10.4; the impostors had it, being pictures of leaves). The far skim is 10
  darker than the leaves.
- **Near clumps from above** (canopy, 20 to 40 m) read a little minty and
  too bright (+8) beside the leaves, and their leaves look coarser.
- **Backlit mid trees among the trunks** at golden hour are still about 9
  too bright (40 to 80 m), and a crown card hides the trunks behind it more
  than the leaves do.
- **Crown-tier gaps.** A crown is one sphere of leaves; it has no holes or
  clump structure of its own.
- p95 is 5.25 ms against a 5.1 target.

## Step 7a, round 2

The open items above, still with nothing baked per tree: every tier is
derived at run time from the tree's own records, and the only shared data is
tree-independent.

### What changed

- **Crowns trace the tree's own clumps** (`scene_forest_crown.glsl`). A
  crown is still one card per tree, but each pixel walks the specimen's
  clump records along its ray: every clump is the ellipsoid its leaves fill
  (0.9 r up, 0.7 r down, as `clumpLeafFrame` places them), covered by the
  optical depth of its leaves along the chord. The nearest two clumps hit are
  the ones seen, the second through the first's gaps. So a far crown has the
  clumps tier's lobes, gaps, outline and true extent, not one sphere. Each
  hit is lit like a lobe: the analytic self-shadow through *that* clump, and
  one shadow tap where the light leaves it. The fragment stage now reads the
  procedural records (descriptor binding 1 is VERTEX|FRAGMENT).
- **Clusters are lit as their lobe.** A thinned leaf's cluster carries its
  lobe's sphere and density from the vertex shader. Its members face out of
  the lobe, leaning up (`forestClusterLayer`'s lobe direction and tilt),
  instead of copying the kept leaf. Clusters take the lobe sprite's analytic
  self-shadow at half the lobe's density, plus one tap at the exit, like the
  sprite that takes over from them. Before, they were shadowed by casters
  among their own leaves, and the mid range came out darker than both
  neighbours.
- **Sprite casters are calibrated against leaf casters.** A disc stopped
  about 19 more light than the leaf casters on the same receivers, because a
  binary 9x9 cell over a whole lobe fills in the gaps the leaves leave. Its
  depth is now x0.75.
- **The exit tap is soft**, with twice the filter like a leaf's own tap. A
  sharp tap printed the discs' cells and the wider casters' edges as hard
  blotches on the lobes they fall on.
- **The visible-depth term `1 / (1 + cos(view, light))` is gone.** With the
  casters calibrated, it only darkened crowns seen down the light.
- **Sunflecks at a quarter strength.** Lobes go a quarter of the way from
  the mean sun to a pixel's few lit-or-not leaves, and crowns keep the mean.
  At full strength the flecks read as noise at this size.
- **Leaf facing.** A placed leaf's frame is mirrored, so `gl_FrontFacing`
  named the wrong face. The eye saw sunlit tops lit as undersides, and leaves
  against the sun lit on the far side. That was the pale grey-white backlit
  near leaves. The face is now the one the normal points to.
- **Per-tree variation** (`procClumpBare`, `scene_procedural.glsl`). Each
  tree drops up to a fifth of its clumps, a share drawn from the tree's hash.
  Leaves, leaf casters, clusters, lobes, disc casters and the crown trace all
  skip the same clumps, so the gaps hold through every tier and in the
  shadow. Two instances of one specimen no longer read as copies.
- **`lod.barkKeepPixels`** (default 2). Bark at least this wide on screen is
  drawn whatever the widening and budget ask, so a mid tree keeps its trunk
  and scaffold limbs under its crown.

Tried and dropped:

- A disc spread through its chord's depth: darker and noisier.
- Shading lobes at the shell's projection.
- Offsetting cluster members' depth.
- Jittering widened leaf casters along the light.

### Numbers

`capture.js metrics`, Leaves / By size. Leaves' own values moved too: the
facing fix and the dropout reach it. So the useful number is each row's
By size minus Leaves (dY), before (da11d4cf, `r2base_metrics.json`) and
after (`r2_metrics.json`):

| View, band | before L / B (dY) | after L / B (dY) | drgb before / after |
|---|---|---|---|
| canopy noon 20-40 | 142 / 150 (+8.1) | 155 / 150 (-4.5) | 16.1 / 7.6 |
| canopy noon 40-80 | 143 / 150 (+7.0) | 158 / 159 (+0.5) | 13.5 / 0.9 |
| canopy noon 80-160 | 159 / 152 (-7.3) | 167 / 168 (+1.7) | 12.4 / 2.3 |
| canopy golden 20-40 | 137 / 135 (-1.7) | 145 / 136 (-8.9) | 4.9 / 14.1 |
| canopy golden 80-160 | 142 / 135 (-7.4) | 152 / 149 (-2.9) | 17.3 / 5.8 |
| skim noon 40-80 | 165 / 157 (-7.9) | 173 / 173 (+0.4) | 15.0 / 0.6 |
| skim noon 80-160 | 178 / 167 (-10.3) | 179 / 176 (-3.0) | 17.7 / 5.2 |
| skim golden 80-160 | 156 / 152 (-3.9) | 172 / 169 (-3.2) | 13.1 / 6.8 |
| glade noon 10-20 | 144 / 130 (-13.5) | 128 / 132 (+4.4) | 24.0 / 7.8 |
| glade noon 20-40 | 121 / 118 (-2.9) | 117 / 123 (+5.5) | 5.9 / 8.9 |
| glade golden 20-40 | 126 / 128 (+2.1) | 131 / 131 (+0.1) | 4.3 / 0.7 |
| trunks noon 20-40 | 123 / 123 (-0.1) | 116 / 126 (+9.6) | 8.6 / 14.0 |
| trunks noon 40-80 | 90 / 98 (+7.9) | 88 / 96 (+8.1) | 11.7 / 11.1 |
| trunks golden 20-40 | 135 / 137 (+1.6) | 136 / 138 (+2.2) | 10.4 / 3.0 |
| trunks golden 40-80 | 109 / 118 (+8.6) | 108 / 114 (+5.8) | 15.1 / 7.9 |

Far crowns took on the leaves' structure. Canopy noon 80-160 hard edges are
now 8.2 against 8.2 (before, 10.4 against 4.7), and the far skim's contrast
holds. The canopy and skim mid bands are within 1 of Leaves, with the colour
gone (drgb under 1); the minty cast is gone. The mean colour is now within 9
of Leaves in every band but three (14.1, 14.0 and 11.1).

Trace (`capture.js trace`, 1280x720, 4x MSAA, RTX 4090, breeze, 1021
frames), GPU ms, three runs:

| Run | Max | p95 | Median |
|---|---|---|---|
| da11d4cf (step 7a mid range) | 7.65 to 10.67 | 5.25 to 5.78 | 4.08 to 4.09 |
| now | 6.12 to 7.21 | 5.78 to 5.91 | 4.18 to 4.20 |

**The bursts.** `BRO_SCENE_GPU_PASSES` (bro/docs/headless.md) times each
pass and each opaque draw. The 1 to 2 s bursts are a staircase: 17, then
13.8, then 11, then 6.3 ms, each step held 4 to 7 frames. It starts about
8.3 s of wall time after the device comes up, whatever view is drawn. Only
the fragment-heavy forest sprite draws slow down (clumps 3x, crowns 10x);
shadow, bark and static draws do not.

It is not caused by any of these:

- pipeline creation (all pipelines exist at boot);
- a CPU stall;
- grass;
- the `gl_FragDepth` write (the staircase is there without it);
- clocks (2670 MHz throughout).

da11d4cf shows the same staircase, and it disappears once the GPU has been
kept busy past that point. It is on the driver or OS side, not in the
frame. The trace now warms until 10 s of wall time have passed before it
times anything, so the max is the flight's own.

**What is left over budget.** The dive into the canopy, from 10 to 13 s,
runs about 5.5 to 6.4 ms:

| Draw | GPU ms |
|---|---|
| clump sprites | 2.4 |
| cluster leaves | 1.0 to 1.8 |
| near leaves | 0.6 |
| shadow | 0.65 |
| bark | 0.5 |

The flight's p95 is set by that one second, and the clusters and crown
traces cost about 0.1 ms of median. Nothing was given up for the budget.

The flight video shows no popping. ffmpeg's scene score finds no frame over
0.04 beyond the first frames and the last. The handover strips change
smoothly through all three tiers.

### Captures

In broworkshop `demos/forest/captures/`:

- `r2_metrics.json`, `r2_trace.json`; `r2base_metrics.json` is da11d4cf.
- `r2_<shot>_<sky>_compare.png` for canopy, skim, glade and trunks, at noon
  and at golden.
- `r2_handover_{cards,clumps,crowns}_strip.png`.
- `r2.webm`: the flight.

### Still open

- **Trunks, mid range.** Past 20 m the mid tiers are still about 8 brighter
  than the leaves (trunks noon 20-40 and 40-80). The backlit trees behind
  the trunks read as a paler, denser wall than the leaves draw. At golden
  hour, 40 to 80 m is better (+5.8, was +8.6) but not closed. Lighter mid
  shadows help the canopy top and hurt this view, and the two pull against
  each other.
- **Canopy golden 20-40** is 9 darker than the leaves (the near clumps from
  above, sun low).
- **p95 5.8 ms against 5.1.** It needs the clump sprites cheaper in the
  canopy dive (2.4 ms): fewer lobes per pixel there, or an earlier mean.

## Order

1. Motion: the wind field, hierarchical sway, bulk normals and translucency in
   bro.scene; broflora emits the sway data; the lab shows it.
2. Look: crown clumps, kinked bark, interior depth, leaf underside.
3. Grass: the grass node on the same wind, parted by movers.
4. Wood and leaves: cantilever terms from the geometry, damped-oscillator
   response, the leaf layer, softer leaf light, MSAA, seamless tubes.
5. Lush grass: tufts of fine blades an instance, four rings at a level
   cover, view-culled tiles, darker soil.
6. Forest scale: specimens planted many times over, leaves / clumps / crowns
   chosen by size on screen under a vertex budget, one pose for every tier,
   grass skipped under closed canopy and scaled to the eye's height.
7. Refinement. 7a, the trees: far trees as octahedral impostors of their own
   specimens (removed: nothing may be baked per tree), then the mid range
   as leaves placed at run time in clusters, lobes and crowns. 7b: the
   ground and the bark.

Each step is judged by motion strips and short videos next to the reference
galleries, not by stills.

## Next: step 7, refinement (user review of demos/forest)

- **Far trees as good as the "Leaves" mode, without the leaves.** "Leaves"
  looks far better from above only because it has no sharp contrast at the
  handover. Clumps look good and should stay, but match leaf mode's colour,
  value and texture so zooming in never shows a step.
- **Clump shapes make trees read too small** from above. Crown-scale shapes
  must match the real crown's extent and silhouette.
- **No large shade shifts while trees move.** Big patches of shading change as
  trees sway, which is unnatural. Lighting of a crown's mass should stay
  stable; only fine detail flickers.
- **No popping.** Nothing may blink in or out during motion or flight:
  handover dither stipple, leaves thinning, tier swaps.
- **The ground.** Lush grass must reach much further out. From canopy height
  the ground is bare and ugly, and from above it is a blurry smear. Far turf
  needs real texture and colour, plus forest-floor litter and understory
  under trees.
- **Bark.** Trunks are completely barren and look like clay. They need real
  bark: procedural ridges and furrows that follow the stem, colour and
  roughness variation, moss or lichen on the shaded side, root flare at the
  base, and branch collars.
- Also from the step 6 review:
  - backlit near leaves turn pale grey-white;
  - 24 specimens repeat visibly;
  - canopy shade stops past the shadow range;
  - one transient plateau of about 8 ms per flight.
