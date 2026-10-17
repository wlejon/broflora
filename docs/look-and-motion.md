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
