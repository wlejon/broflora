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

## Motion oracle

A reviewer who reads text and stills cannot watch a video. The motion
oracle turns a run's motion into sheets and a short text that point at
the faults. It lives in broworkshop: `lib/kit/motion.js` (the image half,
for any demo) and `lib/kit/sway.js` (the ground truth: the wind field and
`vegPose` / `vegLeafTurn` ported to JS, evaluated as the GPU does). The
scenarios are `demos/grove/motion.js` and `demos/forest/motion.js`. Run
them from broworkshop:

```
bro-headless --width 1280 --height 720 demos/grove demos/grove/motion.js -- calibrate + truth + plant fir + mover
bro-headless --width 1280 --height 720 demos/forest demos/forest/motion.js -- static + interior + dolly + flight
```

The grove commands:

- `calibrate`: synthetic balls (smooth, pops, flicker, jitter, lockstep,
  travelling). It prints PASS/FAIL for each injected fault, and the
  thresholds (`THRESHOLDS` in motion.js) are set so that it all passes.
- `plant <name>`: a preset, `tree`, an herb preset or `meadow`, alone and
  across the wind. It takes `wind=`, `dist=`, `seconds=`, and `clock=`
  (start the wind clock there).
- `mover [through=grass,bracken,shrub]`: a walker capsule at 1.4 m/s. It
  is a grass mover, undrawn unless `body=shown`.
- `truth`: the grove trees' parts tracked by the port.
- `floor=contrast` on any command draws the ground one flat pale sand
  (grove.js `contrastFloor`, a capture option, not the look), so a grass
  or herb run's blocks and slit-scans see blades rather than turf; its
  reports carry `_contrast`. `capture.js grassfloor` takes the grass stills
  on it (0.3 to 20 m, side and 45° down, noon and golden).

The forest commands:

- `static [dists=5,20,60,150]`: the handover subject, across the wind.
- `interior [shot=trunks]`.
- `flight`: the camera moves, so read the events and flicker.
- `dolly`: a log dolly out through every handover in still air.

Every image run writes `captures/motion/<name>_motion.txt` and `.json`,
plus `<name>_motion.jpg` (sheets with events also get `_events.jpg` and
`_slits.jpg`). The image half tracks 16 px blocks frame to frame. For each
block:

- **V**: its motion.
- **P**: its brightness change along that motion.
- **D**: what neither its own motion, a neighbour's nor stillness
  explains.

Read the text summary per region:

| Field | Meaning |
|---|---|
| motion | rms px/frame |
| wiggle | Effective frequency: 1-2 Hz is sway, 3-7 Hz flutter, above 7 Hz noise |
| jitter | Positional noise in px: rms of the second difference of V over sqrt 20 |
| flicker | Brightness noise in levels, the same way, on P |
| coherence | Correlation of block pairs by distance band. Lag is the travel time of a gust across the view. |

The flags are:

- `NOISE` / `JITTER`: the blocks shake.
- `FLICKER`: the shading flickers.
- `EVENTS`: pops (D spikes), flashes (P kicks) and kicks (jumps in V). Each
  is ranked with its frame, wind clock and pixel box.
- `LOCKSTEP`: far blocks move as one.
- `INCOHERENT`: near blocks do not move together.

On the sheet:

- Top left: the events map, with flicker in magenta.
- Top right: motion by wiggle (green sway, cyan, amber flutter, red
  noise).
- Bottom left: the timelines.
- Bottom right: slit-scans through the worst events. Time runs down; a
  pop is a horizontal tear, flicker is striping.
- `_events.jpg`: each top event as frames t-1, t, t+1.

Truth (`truth_<wind>.txt`, `_tracks.jpg`) gives the following for each
part (trunk top, limb, twigs, a leaf):

- Its rms, dominant Hz, largest step and jerk spikes, each attributed to a
  gust band edge or a kick kink.
- The share of its variance that is the drag's mean bend.
- Its coherence with the other parts.
- The leaf flutter's instantaneous frequency over a long wind clock.
- The stand's trunk tops: correlation and lag against the gust's travel.
- A check of the port against the rendered wood.

Mover (`mover_<through>_<wind>_response.txt` / `.jpg`) gives the
following, by distance from the path and aligned on the walker's passage:

- Change from rest: onset, peak, time to 50% and 10%, what is left at
  the end.
- The tracked speed and displacement, with overshoot.
- For the shrub: when the walker is inside the crown, and how much the
  shrub moved then.

Dolly (`forest_dolly_<part>_<wind>_pops.txt` / `.jpg`) gives each handover
distance, the pops in the subject's picture beyond the dolly's own change,
and the mean-luma step at each handover.

With a moving camera (the flight, the dolly) every block is "lost": the
view's own motion is not plant motion. Read only the events there. Only
large ones count (D over about 30, tens of blocks); their crops show what
jumped.

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

## Step 7b as built: the ground and the bark

Everything here comes from data that is local to the point being drawn:
the species' bark fields, the wood's place on its plant, the grass field's
cells, and the canopy cover (a function of the planted trees). On top of
that, everything uses shared value noise and jittered cells. Nothing is
baked for any one place, so a world that streams in draws the same way.

### Bark

- **The data.** broflora's species carry `barkFurrow`, `barkScale`,
  `barkMoss`, `rootFlare` and `branchCollar` (plant.h; emit-only, world
  format 2).
  - `packBarkWord` packs the first three into one word, in record 2 of
    every bark vegetation record. The word goes on forest tubes and
    `emitBranchTubes` alike.
  - Record 2 also carries the wood's path from the root, so the pattern
    runs on through joints.
- **Root flare and collars** are geometry, made in `tubeSegments`.
  - A trunk's root segment is split at 0.35, 1 and 2.2 radii and swells as
    r(1 + F e^-h).
  - A side shoot swells into a collar where it leaves its parent.
- **The vertex stage** (bro `scene_bark.glsl`, in `mesh_tube.vert` and
  `mesh_forest_tube.vert`) places each point on the stem: around it, along
  the path, the radius, and the height over the root. It also splits a
  trunk's foot into five buttresses.
- **The fragment stage** (the `SCENE_BARK` build of mesh.frag) cuts plates
  parted by ragged furrows.
  - The plates are a jittered 3x3x3 cell pattern (F2 - F1), stretched six
    times along the stem.
  - Their spacing is the girth's (0.01 + 0.13 r) times `barkScale`.
  - The furrows' width and depth come from `barkFurrow`.
- **The normal** bends by the pattern's analytic gradient, so there are no
  derivative taps.
- **The colour.**
  - Furrows are dark and warm; plate faces are greyed.
  - Each plate gets its own tone.
  - Blotches and streaks are sized to outlast the plates with distance.
  - Moss grows on the side away from the sun, a limb's top and the foot,
    in patches, scaled by `barkMoss`. Lichen crusts the plates.
- **Distance.** Each pattern fades to its mean once its period nears a
  pixel or two, and is not computed at all once faded. The bark draw is
  vertex-bound, so its fragment cost does not show.

### The ground

- **Forest floor and understory.** `createGrass({ floor })` takes a second
  field: `bro.flora.forestCover(...).canopy` (`ForestCover::canopyAt`), 0
  in the open and 1 under closed crowns.
  - Where the floor closes, some tufts in the near tuft rings become low
    leafy plants: six broad leaves in a rosette.
  - They grow in patches, whether grass grows there or not.
  - A block of the field is drawn when either grass or floor grows in it.
- **Turf.** `turf: mesh | [meshes]`, or `grass.shadeGround(mesh)`, draws a
  ground mesh through the `SCENE_GROUND` build of mesh.frag
  (`scene_ground.glsl`). Set 4 there is the grass's own field.
  - The sward is the blades' mean colour, dryness and clumps. Near the eye
    it shows the roots and soil between the blades; toward the range it
    shows the blade tops, so the turf meets the blades without a step.
  - Soil shows where nothing grows.
  - Under a closed floor: overlapping fallen leaves (two ovals a cell,
    shadow rims, midribs), twigs, damp soil and moss.
  - Off the field the same data comes from the mesh's vertex colour
    (density, dryness, floor), which is how the far ground carries it.
  - Every octave fades as its period nears two pixels, so the ground keeps
    texture from standing height to far above.
- **Range.** The forest's blades now reach 150 m (were 100 m), and the
  turf continues past them.

### Cost

- **Leaf shadows.** A leaf in a sphere (lobes, a crown's clumps, a
  cluster's lobe) now takes its exit shadow as four hardware-filtered taps
  spanning the nine-tap grid (`sampleShadow` with a negative spread),
  instead of the nine.
  - That is 0.4 ms at the canopy dive (hold t = 12: 5.89 to 5.49 ms).
  - The canopy bands hold against the nine taps. At canopy noon, 80 to
    160 m: drgb 2.3 against 2.4, hard edges 8.0/8.5 against 8.1/8.1. At
    golden hour: drgb 5.7 against 6.0.
- **Tried and dropped: an earlier hand-over to the lobes' mean**
  (`forestLobeMeanShare` 0.45..0.9 instead of 0.7..1.4).
  - It saves another 0.3 ms.
  - It costs the far canopy band: noon hard edges 11.8 against 8.1; golden
    drgb 9.8 against 6.0.
- **Ground and bark cost little.**
  - At t = 24 (6 m up among the trunks), the grass draws total about
    0.3 ms and the turf about 0.07 ms.
  - Grove frame times hold: grove 6.26 to 6.30 ms, trunk 4.90 to 4.66, the
    bark close-up 3.87 to 3.91. The bark draw is 0.35 to 0.38 ms.
- **The canopy dive still sets the frame.** At hold t = 11.5 (5.57 ms):
  - forest sprites 2.31 + 0.36 ms;
  - forest leaves 0.41 + 1.14;
  - shadow 0.65;
  - tubes 0.32 + 0.16;
  - grass and ground under 0.15.

### Numbers

The flight trace is at 1280x720 with 4x MSAA. "Before" is step 7a round
2's head; the target is this step's.

|        | before | 7b   | target |
|--------|--------|------|--------|
| max    | 6.97   | 6.12 | 6.0    |
| p95    | 5.93   | 5.45 | 5.1    |
| median | 4.21   | 4.19 | 4.4    |

By second of the flight, the frame stays under 4.5 ms except at two
points:

- t = 10 to 13 s (50 to 32 m up, the canopy dive): 5.0 to 5.6 ms on
  average, 6.2 ms at worst.
- t = 24 s (6 m up, among the trunks): 5.1 ms.

**Canopy, Leaves against By size** (the metrics mode), with four taps
against nine:

- At 80 to 160 m, noon: drgb 2.3 against 2.4, hard edges 8.0/8.5 against
  8.1/8.1. Golden: drgb 5.7 against 6.0.
- **The full `s7b_metrics.json` against round 2**, all four shots at both
  skies, with this step's ground and grass behind the crowns too:
  - drgb per band moves both ways, mostly within ±3, and averages -0.1;
  - skim golden 20 to 40 m improves from 13.7 to 6.2;
  - canopy noon 80 to 160 m goes from 2.4 to 1.3;
  - the largest rises are skim noon 80 to 160 m (5.4 to 8.4) and canopy
    golden 80 to 160 m (7.1 to 9.7).

**The ground** (By size frame, per grass ring):

- **Glade, noon.** d9 (texture at the crown-mass scale) is 5.6, 11.4 and
  13.7 for 7 to 20, 20 to 55 and 55 to 150 m. The turf keeps its texture
  with distance rather than smearing.
- **Seams at noon**, step at the edge against its controls:
  - glade, 20 m: drgb 18.8 against 8.7;
  - trunks, 7 m: 2.6 against 5.7.
- **Seams with the trees hidden, golden:**
  - glade, 20 m: 17.7 against 7.3;
  - trunks, 7 m: 22.0 against 9.4;
  - trunks, 20 m: 28.6 against 16.5.
- These steps are the glade giving way to the forest floor (the cover, and
  so the turf, darkens past the glade's edge). The stills show no band at
  any ring.

**Bark** (`tests/flora/test_bark_options.js`, tube bark close up):

- `barkFurrow` 0 to 1 doubles the trunk's local contrast, 0.092 to 0.187.
- `barkMoss` 0 to 1 raises its green over red from 0.876 to 0.984.

### Captures

In broworkshop `demos/forest/captures/`:

- `s7b_<shot>.png` (noon) and `s7bg_<shot>.png` (golden), for above,
  canopy, skim, glade and trunks.
  - Before: `r2_<shot>_<sky>_compare.png` (step 7a round 2) and
    `bk1_{glade,trunks}.png`.
- `sh_{trunks,glade}.png`: golden hour with the trees hidden (`hide=node`),
  the ground alone.
- Bark: `bk6_trunks.png` / `bk7_trunks.png`, and `bk6g_trunks.png` /
  `bk7g_trunks.png` at golden hour.
- Ground: `g1`..`g6` stills, including the views
  `at-26_1.6_12_-21_0_15`, `at60_40_40_0_0_18` (from 40 m up) and
  `at-24_0.5_10_-23_0_11.5`.
- `s7b_trace.json` and `s7b_metrics.json`. The metrics now carry each
  shot's `ground`.

In `demos/grove/captures/`: `bk3`..`bk7` trunk and bark close-ups.

### Still open

- **The flight trace misses its max and p95 targets** (6.0 / 5.1). The
  rest is the clump sprites in the canopy dive, t = 10 to 13 s.
  - An earlier mean hand-over would close most of it, at the far band's
    look.
  - What is left is to make the exact lobe loop itself cheaper.
- **Overhead turf** from 40 m up reads as texture, but it is still softer
  than the near grass.
- **The ground seam measure** cannot tell a ring's edge from the view's
  own change at that distance. The edges show steps of about twice their
  controls, darker beyond, but the stills show no band. The steps are the
  glade's edge and the forest floor's cover.
- **Canopy shade past the shadow range** was not attempted.

## Step 8a as built: conifers and shrubs

Conifers and shrubs are species presets any app gets by name
(`bro.flora.speciesPresets()`, `world.plantSpecies(name, {origin})`).
They grow through the same simulation as the broadleaves and are drawn by
the same forest node. Nothing is baked per plant.

### Habits

The habit fields (`Species`: apicalDominance, maxBranchOrder, crownSlope,
crownBase, crownRadiusMax, prototype and lateral groups, lateralElevation,
lateralOrthotropy) give the leader its own modules and the laterals theirs.

- **Monopodial leader with whorled tiers.** fir, spruce, pine, cedar and
  redwood keep a strong leader (apicalDominance) that grows a whorl of
  boughs at each tier. The boughs branch twice (bough, then spray) inside
  a cone (crownSlope).
- **Drooping laterals.** spruce and redwood set a negative
  lateralElevation; pine's boughs rise and fir's stay level.
- **Columnar.** cypress turns every bough up against the leader
  (lateralElevation 1.2, crownRadiusMax 0.9).
- **Multi-stemmed shrubs.** boxwood, holly, hydrangea, hibiscus, lavender
  and juniper grow from a basal clump of stems.
- **Fixed in the step:** a newborn module now gets its world position at
  spawn (`spawning.cpp`). Before, a few segments sat at the world origin
  until the next develop pass.

### Foliage

- **Leaf form** (`Species::leafForm`): broad, needle or scale. A needle or
  scale node's leaf is a whole spray's card. The needles or scale leaves
  are cut out of it per pixel into the sample mask
  (`scene_forest_spray.glsl`), and the cut fades to the spray's mean cover
  once a needle is narrower than a pixel. The shadow caster is the card
  narrowed by that fill.
- **Conifer foliage settings:** small clumps (clumpScale 0.3), twigs 7 mm
  thick, pipeExp 1.75.
- **Emit defaults per form.** An emit filtered to a form fills in that
  form's defaults: needle perArea 50 with clumpSpacing 1, scale perArea
  140 with clumpSpacing 0.8.
- **Colours per specimen.** Each specimen's leaf and bark colour ride in
  `specimenLooks` and tint its leaves and bark in every tier.
- **Shrub leaf size.** A shrub's small leaves come from `Species::leafSize`
  (boxwood 0.3, lavender 0.25, holly 0.55, hibiscus 0.8, hydrangea 1.1).
  Its clumps carry 1/leafSize² as many leaves, so they cover as much. A
  forest given no leaf draws its form's card at the specimens' mean leaf
  size.
- **Bark per species:** barkFurrow, barkScale, barkMoss, rootFlare and
  barkColor. For example pine's red-brown plates, fir's smooth grey bark,
  redwood's deep red fibres and big flare.

### Motion

The form sets how the trees move through vegPose:

| Form   | Leaf flutter | Limb bob | Twig bob |
|--------|--------------|----------|----------|
| broad  | 1            | 0.6      | 0.8      |
| needle | 0.15         | 1.6      | 1.0      |
| scale  | 0.1          | 1.3      | 0.9      |

A conifer's needles hardly flutter while its long boughs bob.
`createForest` takes `flutter`, `limbBob` and `twigBob` to override these.

### Tiers

- Every tier poses and lights from the same records, as for the
  broadleaves.
- Several forest nodes share one budget: the drawer plans them all, fits
  one detail factor and finishes each. A mixed forest drawn as a node per
  form therefore thins as one forest.
- **Crowns trace clumps in blocks.** A conifer has about 280 clumps
  against about 35 for a broadleaf. A crown now traces them in blocks of
  8 and skips any block whose bounding sphere its ray misses
  (`ForestNode::kClumpBlock`).
- **Earlier crown hand-over.** A needle or scale node hands its clumps
  over to crowns at 8 .. 5 px rather than 5 .. 3, because its crown is
  many small clumps.

### Planting

- **demos/forest** plants a mixed forest as four parts, each its own
  forest node under the shared budget:
  - broadleaf;
  - needle conifers (fir, spruce, pine);
  - scale-leaved (cypress);
  - shrubs (boxwood, holly, hydrangea).
- **How the mix is chosen.** `bro.flora.plantForest([...], {mix})` picks
  the part for each cell: conifers by elevation and in patches. Shrubs
  are planted at their own spacing on the glade rim and in understorey
  patches, and are kept off the trail.
- **demos/grove** stands fir, spruce, pine, cypress, juniper, boxwood,
  holly and hydrangea at full size for close-up judging. It has
  `conifers` and `shrubs` shots, and `capture.js species` renders close,
  mid and far panes for each preset at noon and golden hour.

### Numbers

**Trace.** 1280x720, 4x MSAA, the whole mixed forest. Its 12 878 trees
match step 7b's count, plus 3 222 shrubs.

|        | 7b (broadleaf) | 8a mixed (first) | 8a as built | target |
|--------|----------------|------------------|-------------|--------|
| max    | 6.12           | 8.25             | 7.25        | 6.0    |
| p95    | 5.45           | 6.88             | 6.10        | 5.1    |
| median | 4.19           | 5.48             | 5.04        | 4.4    |

The trace misses its targets. Where the time goes:

- **Broadleaf part alone** (the other parts hidden): 4.5 / 4.1 / 3.35.
- **Conifers** cost about 1.5 times as much per tree as the broadleaves.
- **Shrubs** cost about 0.1 ms.
- **By pass.** These are the needle node's draws:
  - Canopy dive: crowns 0.9 ms, down from 1.3 with the blocks.
  - At the trunks: leaves 1.1 to 2.2 ms, bark 0.4 to 0.6 ms.
- **What brought it down from the first mixed run:**
  - needle perArea 80 to 50 and clumpSpacing 0.8 to 1: median 5.48 to
    5.14;
  - crown blocks and the earlier crown hand-over: median 5.04.

**Metrics** (`s8a_metrics.json`). Leaves against By size per band, mixed
forest:

| Shot, sky | Bands | By size against Leaves (luminance) | drgb |
|-----------|-------|------------------------------------|------|
| canopy, noon | all | -1 to -3 units | 2.7 to 4.6 |
| skim, noon | near | -0.5 to +0.1 | 0.5 to 0.6 |
| canopy, golden | all | -9 to -12 | 13.7 to 18.9 |

The golden canopy gap is a regression: step 7b's broadleaf canopy had
drgb 6 to 10 there. The needle part alone shows it:

- **Golden:** By size is 10 to 13 luminance darker than Leaves.
- **Noon:** 4 to 11 darker.
- **Hard edges:** about half of Leaves' (3.6 against 1.4 at 20 to 40 m,
  golden).
- **It is not the lobes' self-shadow:** halving the far tiers' density for
  sprays moved the gap by only 1 to 4 units.
- **It is not the crown hand-over:** with crowns at 5 .. 3 px or forced
  late, the gap is the same.

### Captures

In broworkshop `demos/forest/captures/`:

- **Stills:** `s8a_<shot>.png` (noon) and `s8ag_<shot>.png` (golden) for
  above, canopy, skim, glade and trunks. Also `mixed_<shot>.png`, the
  first mixed forest.
- **Hand-over strips:** `s8a_handover_{clumps,crowns}_{needle,shrub}_strip.png`
  and `s8a_handover_clumps_strip.png`.
- **Flight:** `s8a.webm`.
- **Data:** `s8a_trace.json` and `s8a_metrics.json`.
- **Experiments:** `m_pa40`, `m_cs12`, `m_b50`, `m_flod` (traces) and
  `cs12_*`, `b50_*`, `flod_*` (stills).

In `demos/grove/captures/`:

- `s8a_<preset>_{noon,golden}.png` for all eight presets;
- `species_conifers.png` and `species_shrubs.png`.

### Still open

- **The trace targets.**
  - The needle leaves near the eye cost the most. A spray card is drawn
    whole and its needles are cut per pixel, so the hidden layers of a
    dense conifer crown are all shaded.
  - Conifer bark is many thin twigs.
- **Golden-hour value of conifer clumps and crowns:** 10 to 13 luminance
  darker than their leaves (see Numbers).
- **Conifer upper crowns are narrow and sparse.** The leader's top tiers
  grow little foliage in the sim's profile. The fir and spruce read as
  conifers; cypress is pale against the sky and thin at the top.
- **No weeping broadleaf preset** (willow or birch). Drooping laterals are
  covered by spruce and redwood only.
- **Shrub blooms are not drawn by the forest node.**
- **Clump ellipsoids are the same shape for every form.** A flatter clump
  for a fir's level sprays was not tried.

## Step 8a, round 2: each species its own look, blooms, the light across the tiers

Round 2 of step 8a. Every species should read as itself by default, with
nothing baked: each tier still comes from the plant's own sim at run time.
The round also draws the shrubs' blooms, closes the golden-hour value gap
between the tiers, and brings the trace to its targets.

Commits:
- broflora: `802d506`, `49d4b37`, `143bb77`, `82ed063`, `ee81bee`,
  `947ed26`;
- bro: `b7d8235d`, `6e5ed9e9`, `cfc0c217`, `e1c7adfe`, `1149bd9f`,
  `b4125f68` (docs);
- broworkshop: `2348969`, `48542ab`, `7e6eb9d`.

### What changed

**Habits** (broflora Species, read by the sim or the clump placer):

- **`clumpFlat`:** a clump's height over its width. A fir's sprays lie in
  flat pads (about 0.4) that stack close up and down.
- **`clumpReach`:** a clump is no wider than its bough reaches from the
  axis, so a conifer's short top boughs carry small clumps and the crown
  narrows to a spire.
- **Evergreen young shoots carry foliage** (the maturity floor), so the
  leader and the top boughs are clothed.
- **`crownLift`:** a self-pruned trunk. Boughs leaving the leader below
  that share of the height are shed. The leader's short shoots along the
  bare trunk take no clumps: before, they made a fuzz up a pine's trunk.
- **`heightMax`:** the leader stops and the upper boughs fill in under it,
  giving a pine its flat top.
- **`clumpInner`:** a shoot that has branched on keeps a tip's foliage in
  half light, and the clumps pack up to 40% closer. A shrub becomes one
  mound leaved inside and to the ground, its stems hidden. (0.25 closer
  left gaps between the clumps in the boxwood and hydrangea panes.)

**Presets.** The look target for each species, and what moved:

| Preset | Look target | What changed |
|--------|-------------|--------------|
| fir | a regular cone | flat pads |
| spruce | drooping tiers | flat pads |
| pine | an open crown widest near the flat top, on a bare reddish trunk | crownSlope 0.6, heightMax 16, redder bark, crownLift |
| redwood | a column on a clear, massive trunk | apical dominance 0.8, near-level short boughs, crownRadiusMax 1.6, crownLift 0.45, pipe exponent 1.2 |
| juniper | a bushy cone filled to the core | leaders ahead of rising boughs, clumpInner 1 |
| shrubs | dense mounds to the ground | clumpInner 1 |
| lavender | a grey-green cushion | finer stems |

- **Birch is new:** slender (pipe exponent 2.4) and narrow, a light open
  crown of small bright leaves on hanging twigs, white bark.
- **Willow was tried and dropped:** see Still open.

**Blooms.**

- broflora's emitForest emits each specimen's blooms (`blooms`, 8 floats
  each: position, size, outward direction, the tube piece it sways with)
  and their look (`specimenBlooms`).
- Anchors that hang down, sit deep in shade, or are too small bear none.
- bro's forest node draws them (`setForestBlooms`), each posed by its twig
  and shaped per pixel with no textures:
  - hibiscus: an open five-petal flower;
  - hydrangea: a traced head of four-petal florets;
  - lavender: a spike of tapering whorls.
- Below 1.5 px a bloom is a dithered dot, and below 0.2 px it is not drawn.

**Conifer tiers** (bro):

- **An opaque core** inside each run of a spray node's clumps, drawn first:
  the spray cards behind it fail the depth test.
- **Thinned leaves stay sprays**, widened, instead of clusters of broad
  leaves. Those clusters were the heaviest draw: 3 ms at the flight's
  trunks, now 1.2.
- **A tree whose leaves the camera draws casts no coarser than clumps.**
- **A crown's clumps are traced in spatial groups**, in the specimen's
  space: needle crowns cost 0.75 ms in the canopy dive, down from 1.35.
- **The needle spray card is larger** (0.14 x 0.26), so perArea 45 covers
  what 80 did.

**Light across the tiers** (bro):

- **A broadleaf crown is lit as its clumps are.**
  - Its caster disc sits past the crown's middle, which left the crown's
    near half unshaded, so crowns were 12 to 21 units brighter than their
    clumps and leaves.
  - The crown is now shaded through its own mean density toward the light
    (`crownSigma`), with the shadow map read past its sphere.
  - Its clumps are traced at 0.8 of their radius (`FOREST_CROWN_FILL`):
    their lobes leave the rim open, so far crowns covered a fifth more.
  - The clump seen is picked by its share.
  - The leaf-count wander no longer fades with distance: far crowns were
    nearly solid.
  - Spray nodes keep per-clump shading and a fill of 1: both changes made
    needle crowns dark.
- **Clumps hand over to crowns at 7 .. 4.5 px** (was 5 .. 3), within 2
  units of the leaves from 90 to 120 m. This made the canopy dive 0.4 ms
  cheaper.
- **A sprite caster stops as much sun as its leaves turn to it.** The
  leaves lean up (the emit's upBias), so they stop less of a low sun:
  depth x ((1 - b) + 2b |L.y|), b = 1.4 upBias (Ross's G). As one depth
  for every sun, the far casters shaded a golden canopy 8 to 12 units
  under its leaves.
- **A near broadleaf leaf is its own face.** It is folded along its midrib,
  turned its own way more than the crown's bulk (fading out from 6 to
  25 m), and given its own lightness around its tree's. At golden hour the
  near leaves were flat bright paper.

### Numbers

**Trace.** 1280x720, 4x MSAA, the mixed forest (16 100 plants).

| | 8a as built | after conifer tiers (`r4d`) | shrubs fill + blooms (`r5a`) | crown light (`r5b`) | as built (`r5c`) | target |
|---|---|---|---|---|---|---|
| max | 7.25 | 6.15 | 8.69 | 7.60 | 6.01 | 6.0 |
| p95 | 6.10 | 5.48 | 5.61 | 5.42 | 5.32 | 5.1 |
| median | 5.04 | 4.64 | 4.65 | 4.50 | 4.47 | 4.4 |

Notes on the trace:

- The max is at its target (6.01 against 6.0).
- p95 and the median are 0.2 and 0.07 ms over.
- `r5a`'s max was a spike at t = 0.
- The remaining hotspots:
  - t = 13: broadleaf LeafCard about 1.0 ms and clumps 1.0 ms;
  - t = 24: needle near leaves 0.88 ms;
  - shrub leaves about 0.44 ms. This cost is clumpInner's fill: the
    mounds are leaved inside now.
- Tried and reverted:
  - a second sun trace through the crown: +0.7 ms, and needle crowns too
    dark;
  - crown stride and grow changes;
  - drawing a broadleaf core.

**Metrics.** By size against forced Leaves per band: luminance difference
dY, with hard edges given as Leaves / By size.

| Shot, sky | Band | 8a dY | Round 2 dY (`r5c_metrics.json`) | Hard edges, round 2 |
|-----------|------|-------|---------------------------------|---------------------|
| canopy, noon | 20-40 | -1.9 | -0.3 | 22.6 / 18.4 |
| canopy, noon | 40-80 | -2.6 | +0.2 | 18.2 / 13.6 |
| canopy, noon | 80-160 | -1.1 | +3.1 | 17.6 / 20.2 |
| canopy, golden | 20-40 | -9.0 | -1.9 | 21.3 / 16.6 |
| canopy, golden | 40-80 | -11.9 | -4.2 | 17.5 / 13.9 |
| canopy, golden | 80-160 | -10.8 | -6.4 | 16.7 / 18.2 |
| glade, noon | 10-20 | -6.1 | -0.7 | 21.8 / 18.7 |
| glade, golden | 10-20 | -6.7 | +0.9 | 16.1 / 14.0 |
| glade, golden | 20-40 | -4.8 | +0.5 | 13.5 / 10.7 |

- The golden canopy gap is down from 9 to 12 units to 2 to 6.
- Hard edges are within a fifth of Leaves' everywhere (8a: half).

**Hand-overs.** The strips are smooth for broadleaf crowns and for needle
cards, clumps and crowns.

**Flight.** The webm's frame-to-frame scene scores:

- p50 0.002, p95 0.026.
- Every 10th frame spikes: these are the codec's keyframes.
- The one peak, 0.047 near 30 s, is the camera passing through a near
  conifer's sprays, not a tier pop.

**Tests:**

- bro flora 7/7, scene 75/75, mesh 11/11, with validation;
- broflora ctest 4/4, bromesh ctest 3/3;
- broworkshop `validate.sh demos/forest demos/grove`: 4/4.

### Captures

In broworkshop `demos/forest/captures/`:

- **Data:** `r5c_trace.json` and `r5c_metrics.json`. Also `r4d`, `r5a` and
  `r5b` for the steps.
- **Tiers:** `r5t_tiers_{broadleaf,needle}.png`, one tree alone at each
  distance, forced Leaves / By size / Clumps / Crowns.
- **Hand-over strips:** `r5_handover_crowns_strip.png` and
  `r5_handover_{cards,clumps,crowns}_needle_strip.png`.
- **Stills:** `r5g_skim.png` and `r5g_skim_golden_compare.png`.
- **Flight:** `r5.webm`.

In `demos/grove/captures/`:

- `r6_<preset>_{noon,golden,sil}.png` for all thirteen presets: 5 / 20 /
  60 / 150 m, shrubs 2.5 / 6 / 20 / 60;
- `r5f_{hydrangea,boxwood}_golden.png`: the near leaf fold;
- `r5s_*`: the clump spacing test.

### Still open

- **The trace:** p95 is 0.2 ms over and the median 0.07 over. The rest is
  in the broadleaf cards and clumps at the flight's t = 13 and the needle
  near leaves.
- **The golden far canopy (80-160 m)** is still 6 units under its leaves.
- **Pine reads close to a broadleaf at 20 to 60 m.** Its clumps are round
  tufts. A pine's crown is open clusters of upturned tufts at bough tips,
  which the clump placer does not shape.
- **No willow.** The growth model gives no weeping habit: negative
  lateralElevation droops the boughs, but the long hanging twigs that make
  a willow are not grown.
- **Birch twigs are drawn in its white bark colour.** A birch's fine twigs
  are dark.
- **Hibiscus flowers are small at mid range** (bloomSize 0.14 m, true to
  life).
- **Blooms cast no shadow.**
- **Shrub cost:** about 0.44 ms of leaves with the filled mounds.

## Step 8a, round 3: species reading as themselves, motion by stiffness, the frame under target

Everything stays derived per plant at run time. Nothing is baked per tree;
the only shared pieces are tree-independent shader functions.

### What changed

**Motion follows stiffness** (broflora `sway.cpp`). Before, a conifer's
lowest boughs swung metres while its leader barely moved.

- Each cantilever (trunk, limb, twig) now takes the wind it actually
  catches:
  - a piece's drag is sheltered by the crown above and around it,
    `exp(-2 * 2.5 * depth)`, normalised at 0.75 of the height;
  - a twig's drag is sheltered by the share of its limb that is open.
- Its spring is the larger of two:
  - bending stiffness, 3EI/L³, where I grows as r⁴;
  - the stiffness that holds its own weight to a sag of 8% of its length.
- Its deflection is capped at a working strain of 0.2% (a thick limb
  cannot swing further than its wood bends).
- The trunk chain runs out at a tenth of the base radius (was 0.35), so the
  whole stem sways from the base.

Static tip deflection at the 10 m/s reference wind (`preset_report`
REPORT_SWAY):

| Preset | Lowest bough, before | Lowest bough, after | Upper limbs / top, after |
|--------|---------------------|---------------------|--------------------------|
| fir | 2.53 m | 0.046 m | 0.13 m |
| spruce | 2.84 m | 0.040 m | 0.12 m, top 0.65 m |
| cedar | 4.9 m | 0.074 m | 0.36 to 0.76 m |
| pine (trunk top) | 4.3 m | | 1.1 m |
| oak (limb) | 0.251 m | | 0.178 m |
| birch (limb) | 0.69 m | | 0.127 m |

Rendered: the grove's `swayprofile` (gusty wind, side on, RMS of the
silhouette's left and right edges, in cm, by eighths of the height from low
to high):

| Preset | Before | After (`r7_swayprofile.json`) |
|--------|--------|-------------------------------|
| fir | 97, 50, 41, 37, 23, 12, 10.5, 10 | 18, 4, 6, 12, 11, 6, 6, 10 |
| spruce | 232, 150, 120, 53, 22, 18, 18, 16 | 10, 10, 5, 9, 22, 27, 22, 27 |
| cedar | 216, 98, 108, 154, 152, 114, 117, 156 | 19, 12, 36, 21, 24, 32, 28, 62 |
| pine | 0.9, 3.6, 8.8, 105, 120, 113, 104, 100 | 0.4, 1.4, 2.7, 4.9, 21, 29, 38, 54 |
| birch | 0.2, 0.5, 13.6, 11.1, 9, 10, 9.3, 38 | 0.0, 0.4, 8.4, 6.0, 5.3, 3.1, 4.3, 22 |

Notes on the rendered profile:

- With no wind every band reads 0.
- The fir's lowest band is lifted by its flat boughs' vertical bob, which
  makes the silhouette's edge jump. It is not a swing.

**Pine** (Scots pine):

- A straight, stout stem: kink 0.1, pipe exponent 1.55, about 0.45 m
  across at 17 m.
- The crown sits on the top 45% (crownLift 0.55).
- The needles are only on the outermost shoots (clumpFill 0), so the limbs
  inside the crown are bare.
- The clumps stand apart as tufts: `clumpGap`, a new Species field (world
  format 8), is the spacing along a shoot times the emit's. Pine's is 1.3.
- Bark of strong colour keeps it on its plates' faces (`scene_bark.glsl`).
  Before, every bark's plates were greyed, which turned the pine's
  red-orange dark grey.
- The pipe model keeps wood once grown, so a shed limb no longer thins the
  stem under it.

**Cypress:**

- Small packed clumps (clumpScale 0.16, clumpInner 0.8) make a smooth flame
  rather than a lumpy column.
- Scale sprays are narrower and closer, with deeper scallops.
- A spray's mean cover thins across its needles' ragged tips rather than
  ending at a clean outline.

**Shrubs:**

- **The near leaf.**
  - A broad form's own card is cut to its outline per pixel:
    `MESH_FOREST_LEAF_CUT`, widest a little below its middle, rounded, drawn
    to a point.
  - The card is three rows wide enough to hold the outline: 9 vertices,
    where the old card was its own outline in 12 and showed its corners.
  - The halves curve away from a level midrib (tilt as the square of the
    distance out) rather than folding into two flat facets.
  - `Look::leafCut` marks the form's own card, so a leaf an app gives is
    drawn as it is.
- **Leaf colours:**
  - hydrangea and hibiscus mid green, boxwood darker, holly near
    black-green;
  - the presets were about twice a real leaf's albedo.
- **Hydrangea heads:**
  - borne only on the upper shell (past 0.6 of its radius, above 0.4 of
    the height, not on its underside);
  - held proud of the leaves, turned out from the mound;
  - 0.24 m across.
- **The smeared brown backdrop** was bare soil. The grove's presets stood
  where its own ground cover had no grass. The grove now grows each preset
  world's cover at its spot.

**Sky:**

- **The posterised bands are the image viewer's.** A downscaled 1920 wide
  composite bands; full-resolution crops of the same sky are smooth (the
  targets are RGBA16F).
- **The dark navy noon sky was real.** The kit's noon preset dimmed the
  environment to 0.2 to set off a strong sun, and the environment is both
  the sky seen and the light in the shade.
- **The fix keeps one light:** noon's environment is 0.45. The sky is a pale
  noon blue, and shade is no longer black: a pine's trunk under its crown
  was near black before.

**Leaf tone:**

- The forest's and the grove's leaf colour is a leaf's, (0.07, 0.13, 0.04)
  linear. It was (0.2, 0.36, 0.11), an acid yellow-green.
- Birch's leaves were darkened the same way.

**Birch twigs:** a pale bark comes with age. Wood under a couple of
centimetres across is dark red-brown.

**Frame time**, at the same leaf density:

- **Cheaper cards.** The broad leaf card is 9 vertices (was 12) and a
  spray's is 4 (was 6, losing its bend: the sprays are posed per leaf
  anyway).
- **A lower budget.** The shared vertex budget is 12.5 M (was 14). It still
  draws more leaves than before at the views where it binds:

  | View | Leaves, before | Leaves, after | Detail, before | Detail, after |
  |------|----------------|---------------|----------------|---------------|
  | skim | 779 k | 801 k | 2.02 | 1.98 |
  | trunks | 1.15 M | 1.20 M | 2.18 | 2.09 |

- **A cheaper leaf shadow.** A leaf's shadow is four hardware-filtered taps
  as wide as the nine point taps it took, as a lobe's leaves already were
  (about 0.35 ms).
- **A spray's cover:** the sharp needles or their mean are each worked out
  only where they count.
- **Tried and reverted:**
  - dithering the cascade blend for leaves: no measurable gain;
  - a split-sum fit with an irradiance lookup for a leaf's ambient
    specular: no measurable gain.

### Numbers

**Trace.** 1280x720, 4x MSAA, 16 100 plants.

| | Round 2 (`r5c`) | Round 3 (`r7t1`) | Round 3 (`r7t2`) | Target |
|---|---|---|---|---|
| max | 6.01 | 6.04 | 6.41 | 6.0 |
| p95 | 5.32 | 4.88 | 4.88 | 5.1 |
| median | 4.47 | 4.12 | 4.14 | 4.4 |

- The max frames are lone spikes in seconds whose mean is ordinary: t =
  32 s in `r7t1` (mean 5.00, usually 4.1) and t = 29 and 31 s in `r7t2`.
  The other GPU was running another session's job at 33 to 60%
  utilisation.
- The steady peak is the canopy dive at t = 13 s: 5.55 / 5.66 ms.
- Leaf lighting is about 40 to 60% of a leaf draw's cost, measured by
  stubbing it. Of that, the shadow was about a third; the rest is ambient
  and the sun's specular and translucency.
- With the leaf vertex budget pinned, the remaining structural cost is the
  leaf frame (clumpLeafFrame, sway). It is worked out per vertex, 4 to 9
  times a leaf.

**Metrics.** By size minus forced Leaves, luminance per band
(`r7_metrics.json`). The levels are lower than round 2's because the leaves
are darker.

| Shot, sky | Band | Round 2 dY | Round 3 dY |
|-----------|------|------------|------------|
| canopy, noon | 20-40 | -0.3 | +1.4 |
| canopy, noon | 40-80 | +0.2 | +2.2 |
| canopy, noon | 80-160 | +3.1 | +4.9 |
| canopy, golden | 20-40 | -1.9 | -1.0 |
| canopy, golden | 40-80 | -4.2 | -2.9 |
| canopy, golden | 80-160 | -6.4 | -6.3 |
| glade, noon | 10-20 | -0.7 | -3.1 |
| glade, golden | 10-20 | +0.9 | -4.2 |
| glade, golden | 20-40 | +0.5 | +1.8 |
| skim, noon | 10-20 | +1.2 (`r5`) | +3.0 |
| trunks, noon | 10-20 | +1.4 (`r5`) | +2.0 |

- The golden canopy holds: 1.0 / 2.9 / 6.3, against 1.9 / 4.2 / 6.4.
- The noon far canopy moved from +3.1 to +4.9 (crowns brighter than their
  leaves). Of that:
  - about 0.7 is the brighter noon environment (old leaf colour with the
    new sky: +3.8);
  - about 1.1 is the darker leaf albedo.
- The leaf shadow filter is not the cause: with the nine taps back, it
  reads +4.6.
- Leaf translucency is not the cause either: with it off, the gap is +5.3.

**Tests:**

- bro: scene 79/79 (the native Vulkan scene test included), flora 7/7,
  mesh 15/15;
- broflora ctest 4/4;
- broworkshop `validate.sh demos/forest demos/grove`: 4/4.

### Captures

In broworkshop `demos/forest/captures/`:

- **Data:** `r7t1_trace.json`, `r7t2_trace.json` and `r7_metrics.json`.
  `r6*` are the steps.
- **Hand-over strips:** `r7_handover_crowns_strip.png` and
  `r7_needle_handover_{cards,clumps,crowns}_needle_strip.png`.
- **Stills:** `r6_skim.png` and `r6_canopy.png` (the leaf tone), plus
  `r6k_*` and `r6o_*` (the leaf-count checks).
- **Flight:** `r7.webm`.

In `demos/grove/captures/`:

- `r7_<preset>_{noon,golden,sil}.png` for all thirteen presets;
- `r7_swayprofile.json`;
- `s45_*` / `s46_*` / `s47_*` / `s48_*` / `s49_*`, the steps:
  - `s46_pine_noon` and `s46_cypress_noon`;
  - `s48_hydrangea_noon` and `s48_holly_noon` (the leaf cut);
  - `s48_birch_noon` (the twigs).

### Still open

- **The noon far canopy gap**, +4.9 at 80 to 160 m. The crowns' ambient
  reads brighter than their leaves'.
- **Pine's crown** is open with bare limbs and stands on a bare trunk, and
  its umbrella silhouette parts from a broadleaf's at 60 and 150 m. But
  at 20 m its tufts are still soft clumps rather than upswept brushes of
  needles.
- **Holly is dark but neither spiny nor glossy.** A leaf's outline and
  roughness are the node's, not the species', so a holly needs a
  per-species leaf margin and gloss in the look records.
- **Blooms cast no shadow.**
- **The max frame:** only machine-load spikes are over 6.0. The canopy
  dive's 5.6 ms is the steady peak.

## Step 8a, round 4: the sky's bands, leaves of their own species, the pine

A short round from a full-size review of round 3's grove panes.

### What changed

- **The sky's bands had two causes.**
  - **The image viewer.** It re-encodes a PNG over about 3.75 MB (its 5 MB
    base64 limit), and that re-encode posterised the smooth sky into the
    wide bands seen in `r7_cypress_noon` and `r7_pine_noon`. The grove's
    species panes are now written as JPEG q95 (`capture.js` `saveView`),
    which the viewer shows as rendered.
  - **The scene's 8-bit target.** The LDR target is RGBA8, and an
    undithered sky stepped one level every 8 to 12 rows. Under a contrast
    gain those steps showed as faint bands. `tonemap.frag` now adds
    triangular (TPDF) dither of one level per channel after the grade,
    scaled down near black and white so they stay exact.
    `SceneGraph.setDither(false)` turns it off; two pixel-exact tests do.
- **The leaf cut never ran on a forest.** `ForestNode::setLook` returned
  early when nothing but `leafCut` had changed, so round 3's cut outline
  and curved halves were drawn only by chance. It now compares `leafCut`.
- **Each species its own leaf.** Species has six new fields:
  - `leafWidth`, `leafTip`, `leafTeeth`, `leafToothDepth` and `leafGloss`
    set a broad leaf's outline, margin and sheen;
  - `sprayBrush` sets how a needle spray is held.

  `emitForest` carries them as `specimenLeaves` (8 floats a specimen), and
  the forest node holds them as a ninth specimen record. One node then
  draws several species' leaves:
  - **The outline** (`forestLeafCut`, `scene_forest_mass.glsl`) is widest a
    little below the middle and either drawn to a point or rounded. It has
    serrations, or deeper spines between concave scallops whose margin
    waves up and down. The card's three rows are set out to the species'
    width (`mesh_forest_leaf.vert`).
  - **Veins:** a tapering midrib and curving pinnate laterals, faded out as
    they near a pixel.
  - **Gloss** lowers roughness (to 0.26 at 1) and halves translucency.
  - **Presets:**
    - holly: spiny (3 a side, depth 0.32), glossy (0.9);
    - boxwood: small, rounded (tip 1), a little glossy (0.35);
    - hydrangea: broad and finely serrated (12 a side, depth 0.06);
    - hibiscus: toothed, glossy;
    - birch: finely serrated.
  - **Far off,** a leaf whose half-card is under 2 to 4 pixels hands over
    to its whole card (`fwidthCoarse`, so a far quad skips the cut). That is
    the area the far tiers stand in for. Fully cut at every distance, the
    Leaves tier lost a sixth of its cover and By size minus Leaves rose to
    +6.8 / +6.9 / +13.3 at noon.
- **The pine's brushes.** With `sprayBrush` set (pine 1), each spray turns
  up and out along its twig and gathers toward its clump's end
  (`forestSprayBrush`, in the leaf and caster vertex shaders alike). At
  20 m the tufts read as upswept brushes.
- **The pine's trunk.** Its shaded side had only the sky's lower
  hemisphere, which is dark in the pure-sky HDR.
  - `SceneGraph.setGroundBounce(albedo)` adds a level ground's bounce to
    the ambient of every surface's lower hemisphere, lit by the sun and the
    sky. It is worked out once a frame
    (`SceneEnvironment::fillLighting`); the sky's share is integrated from
    the panorama at bake, so the shader adds one term and makes no fetch.
    The kit's `daylight({ ground })` sets it (forest
    `[0.09, 0.12, 0.05]`, grove `[0.11, 0.15, 0.06]`).
  - The pine's bark is a stronger red-orange (0.50, 0.21, 0.085) with
    shallower furrows (0.6). At 5 m it reads red-brown and plated at noon,
    and orange at golden.

### Numbers

**Trace.** 1280x720, 4x MSAA, 16 100 plants. Run-to-run spread is about
0.1 ms on p95.

| | Round 3 (`r7t1`) | Round 4 (`r8t8`) | Round 4 (`r8t9`) | Target |
|---|---|---|---|---|
| max | 6.04 | 5.57 | 5.36 | 6.0 |
| p95 | 4.88 | 4.98 | 4.98 | 5.1 |
| median | 4.12 | 4.20 | 4.15 | 4.4 |

- The ground bounce first cost about 0.1 ms as a per-pixel cube fetch.
  It is now a uniform.
- Skipping the far leaves' cut whole saved about another 0.1 ms on p95.

**Metrics.** By size minus forced Leaves, luminance per band
(`r8_metrics.json`).

| Shot, sky | Band | Round 3 dY | Round 4 dY |
|-----------|------|------------|------------|
| canopy, noon | 20-40 | +1.4 | +0.7 |
| canopy, noon | 40-80 | +2.2 | +1.4 |
| canopy, noon | 80-160 | +4.9 | +3.6 |
| canopy, golden | 20-40 | -1.0 | -1.3 |
| canopy, golden | 40-80 | -2.9 | -3.1 |
| canopy, golden | 80-160 | -6.3 | -7.9 |
| glade, noon | 10-20 | -3.1 | -4.9 |
| glade, golden | 10-20 | -4.2 | -5.6 |
| skim, noon | 10-20 | +3.0 | +3.0 |
| trunks, noon | 10-20 | +2.0 | +1.6 |

- **The noon far canopy and the golden one pull against each other.**
  The far crowns are brighter than their leaves under a high sun and
  darker under a low one. Their sum stays at about 11 whatever the leaves'
  far cover:
  - with a far cover of 0.84: noon +6.3, golden -5.6;
  - with 1: noon +3.6, golden -7.9.

  Reaching both targets (3 and 6.3) needs the gap's dependence on the
  sun's height fixed.
- **Some levers were ruled out:**
  - the crown's self-shade (`FOREST_CROWN_SHADE` 0.5 to 0.3) moved nothing;
  - the disc casters' cover (0.47 to 0.30) brightened the near bands as
    much as the far ones;
  - translucency off brightened the leaves at both skies.
- **The ground bounce** narrowed noon by 1.2 and widened golden by 0.9:
  - without it: noon +4.7, golden -7.0;
  - with the sun's share only: +4.0 / -7.2.

**Sky** (`r7_cypress_noon` against `r8_cypress_noon`, column x = 1680).
Before, the sky stepped one level every 8 to 12 rows, and the viewer's
re-encode made bands of those steps. After, the row means run smoothly and
a 12x contrast crop shows no steps.

**Tests:**

- bro: scene 75/75, flora 7/7, mesh 11/11, `bro_vulkan_scene_test` and
  `bro_vulkan_scene_passes_test`;
- broflora ctest 4/4. `test_forest_blooms` now allows a hydrangea head to
  stand proud of its twig's tip, as round 3 placed it;
- broworkshop `validate.sh demos/forest demos/grove`: 4/4.

### Captures

In broworkshop `demos/grove/captures/`:

- `r8_<preset>_{noon,golden}.jpg` for all thirteen presets;
- `r8a`..`r8f_*` are the steps:
  - `r8a`: the dither;
  - `r8d` and `r8e`: the leaf cut running, then the brush and the bounce;
  - `r8f_pine_*`: the bark.

In `demos/forest/captures/`:

- `r8_metrics.json`;
- `r8t1`..`r8t9_trace.json` (`r8t8` and `r8t9` are the final build);
- `x1`..`x8_metrics.json`, the experiments above.

### Still open

- **The far canopy's sun-height gap:** noon 80-160 at +3.6 (target 3),
  golden at -7.9 (round 3 -6.3). See Numbers.
- **The glade's near band** (10-20 m) went from -3.1 to -4.9 at noon: the
  leaves there are brighter than By size. The bounce lifts their
  undersides.
- **The pine's 5 m trunk at noon** is red-brown under the crown's dapple
  rather than bright orange. At golden it is orange.
- **Blooms cast no shadow.**
- **The forest's compare PNGs** (about 6 MB) are over the viewer's
  re-encode limit, as the grove's were. They could go to JPEG as the
  grove's have.

## Step 8b: herbs, the low plants

Ferns, wildflowers and forbs from 1 m to the horizon, in place of the
forest's floor sprouts. Commits: broflora `4e39972`, bro `9ccf0f5c`,
broworkshop `2470bb1`.

### What changed

- **Species, nothing grown** (`herbs.h`). Eleven presets:
  - ferns: bracken, lady fern;
  - wildflowers: daisy, poppy, foxglove, bluebell;
  - forbs and ground cover: yarrow, nettle, plantain, clover, wood sorrel.

  Each preset has a habit (rosette, leafy stems or carpet), a leaf form, a
  bloom (ray, cup, head, spike, umbel or bell) and where it grows (shade
  preference, colony period and cover, abundance). Its stem and leaf
  cantilevers are worked out from its build (`herbSway`).
  `herbRecords` packs up to 12 species into the 48-float table the grass
  node reads.
- **Built where they stand** (bro: `scene_herb_mix.glsl`, `scene_herb.glsl`,
  `scene_herb_surface.glsl`, `mesh_herb.vert`).
  - The grass node has a grid of 0.35 m cells, each holding at most one
    plant. The plant is picked from each species' abundance, the light the
    floor's canopy leaves it and its colonies (noise of its own period),
    times the floor's herb density (a second floor channel).
  - Two rings reach to `range`:
    - near: 14 cards of 5 segments;
    - far: 7 cards of 2 segments. A flowering plant there keeps three
      leaves, dropping stems for them. Widening is capped at 1.6.
  - The cards are cut per pixel: fronds with lobed pinnae, toothed and
    lobed leaves, trifoliates, rays, cups, heads, spikes, umbels and bells.
  - They sway by the trees' rule (`vegLevel` cantilevers) with a
    saturation so a tall stem never hooks over. Shelter is 1 − 0.8 ×
    canopy. Leaves flutter only in wind.
  - Herbs cast no shadows.
- **The hand-over**, through the last quarter of `range`:
  - Blades stand in, as many as the herbs cover × 0.6:
    - a leaf blade stands at its species' leaf top (a rosette's lies low,
      under its flowers), blunt and as broad as 0.8 × its height, so a
      stand of bracken is a mass rather than spikes;
    - a bloom is a short dab held at the flowers' height.
  - The turf takes the herbs' mean colour. It comes from a per-node table
    by canopy, averaged over every combination of the species' colonies
    (`GrassNode::herbMean`). Weighing every species per pixel cost the turf
    pass 0.1 to 0.15 ms even where no herbs grow, before any branch was
    taken.
  - The blades' herb mix now runs after their off-screen cull, not before.
- **Forest:** all eleven presets, each finding its own light. The plants
  reach 24 m: 30 m cost about 0.05 ms more at the trace's low views.
  **Grove:** the meadow's six wildflowers.

### Numbers

**Trace.** 1280x720, 4x MSAA. This session the machine ran noisier than in
round 4: the same build varies by about 0.1 ms on p95 between runs, and
herbs-off runs reached 5.25.

| | Round 4 | Herbs off (`h8off`) | Herbs (`h8t2`, `h8t3`, `h8t5`) | Target |
|---|---|---|---|---|
| max | 5.36 | 5.63 | 5.95 / 6.28 / 6.25 | 6.0 |
| p95 | 4.98 | 4.98 | 5.12 / 5.10 / 5.08 | 5.1 |
| median | 4.15 | 4.20 | 4.31 / 4.29 / 4.29 | 4.4 |

- **Max.** The max sits on plateaus of 6 to 8 consecutive frames at about
  6.2 ms (frames 950 to 960, 758 to 763). These plateaus appear in
  herbs-off runs too.
- **Herbs' cost.** Measured frame by frame against herbs-off runs, with the
  runs interleaved: herbs cost 0.05 to 0.13 ms over the low part of the
  flight (frames 600 to 1021) and nothing above the canopy.

**Hand-over** (`forest herbs`, `h8_herbmetrics.json`). The plants drawn to
200 m against handed over at 24 m, per band of ground distance:

| Shot, sky | 2-12 | 12-24 | 24-40 | 40-80 |
|---|---|---|---|---|
| glade noon, dY / drgb | -0.1 / 0.5 | -0.5 / 1.0 | +2.4 / 5.8 | +10.2 / 15.6 |
| glade golden, dY / drgb | +0.3 / 0.5 | +0.1 / 0.4 | -0.4 / 1.6 | +2.9 / 6.5 |
| trunks noon, dY / drgb | -0.1 / 0.1 | +0.1 / 0.3 | | |
| trunks golden, dY / drgb | +0.3 / 0.4 | +0.9 / 1.3 | | |

The noon glade's 40-80 m band is only 1232 px, the glade's far edge.
Darkening the turf's and the blades' herb tint moved it by under 2.

**Tests:**

- bro: scene 79/79 (`test_grass_floor_turf` now tests herbs, not the floor
  sprouts) and flora 7/7;
- broflora ctest 4/4;
- broworkshop `validate.sh demos/forest demos/grove`: 4/4.

### Captures

In broworkshop `demos/grove/captures/`:

- `h8_<preset>_{noon,golden}.jpg`: each preset alone at 1, 3, 10 and 30 m;
- `h8_<preset>_sil.jpg`: their silhouettes;
- `h8_meadow_{noon,golden}.jpg`: the meadow's mix at 0.5, 3, 10 and 60 m.
  The grove's ground ends before 150 m.

In `demos/forest/captures/`:

- `forest_glade.png`, `forest_trunks.png`, `forest_skim.png`;
- `h8_herbmetrics.json`;
- `h8t1`..`h8t5_trace.json`, `h8off_trace.json`.

### Still open

- **The budget.** p95 is at the 5.1 limit, not under it, and max is over
  6.0 on the plateaus.
- **Far fern stands.** From 30 to 100 m the stand-in blades read as
  separate pale plates rather than a continuous mass, and they are paler
  than the plants they replace. The glade's 40-80 m band is +10 at noon.
- **Bluebell's bells** read as a comb of trapezoids at 1 m.
- **The meadow at 150 m** needs the forest's glade. The grove's ground ends
  before then.
- **Carried over from round 4:**
  - the far canopy's sun-height gap: noon 80-160 m at +3.6 (target 3),
    golden at -7.9 (target -6.3). Not worked on in this step;
  - the forest compare captures are now JPEG.

## Step 8b, round 2: far stands, near forms, the meadow

Everything below is still worked out per plant at run time from its
species record and its cell's hash. Nothing is stored per plant or place.

### What changed

**Far stands.** The blade stand-ins are gone: their pale triangular plates
were the "Christmas trees".

- **The far ring.** Past `range` the same plants go on to `far` (default
  3 x range) as a third herb ring. Each plant has three cards of one quad.
  The far ring keeps fewer plants with distance (keep = range / d) and
  widens the ones it keeps to hold their cover: leaves get wider more than
  longer, blooms get bigger. A fern stand at 30 to 70 m is the fronds
  themselves.
- **Seen from above.** Once the eye's lift is over a third of the distance
  across the ground, the far ring is left to the turf (`herbFarSteep`). The
  CPU skips the ring entirely once the lift is past half of `far`.
- **The turf past them.** The turf now covers by viewing angle:
  cover = P·(1 − e^(−density·(top + side/tanθ))). The top and side cover and
  the blooms' share of the side come from each species' build: broflora
  `herbSideCover`, record slots 49 and 50. Seen across, a meadow therefore
  reads as its flowers and leaves; seen from above, it shows the ground
  they leave bare.
- **Stand self-shading.** Each herb vertex carries its stand's leaf area
  index and the share of the stand's leaves above it (`HerbVertex.stand`,
  leaf top in slot 51). The sun is attenuated along its slant path through
  the stand, and ambient light by exp(−0.35·L). A bracken stand is darker
  within and lit at its top.
- **Grass gives the herbs room.** Where the floor's herb density is h, the
  grass is baked into the field at (1 − 0.5h) density and (1 − 0.45h)
  height (`GrassNode::uploadField`). This first ran per blade vertex; the
  extra texture read cost about 0.05 ms.
- **`spacing`.** A new HerbSpecies field: how far apart a species' plants
  stand inside a colony (0 means 1.6 x spread). Bracken uses 0.45 m and is
  now 1.2 m tall with 1.5 m fronds, so a colony is a continuous stand above
  the grass.

**Near forms** (`scene_herb_surface.glsl`, `scene_herb.glsl`):

- **Cups (poppy).** The card faces the eye with the cup's axis up it. The
  fragment draws the rim's ellipse at the angle it is seen and the bowl
  below it. Looking in, you see the petals' inner faces round the black eye
  and its blotches. From the side, you see the round outside with petal
  seams, greener at the base.
- **Racemes.**
  - Bluebell: narrow bells with curled-back lobes hang nearly straight down
    off the arched tip, all on one side.
  - Foxglove: fat tubes droop out and down on one side, rounded where they
    are held, with a pale, spotted throat. The top bells are buds.
  - The card stands to the bells' side, so they are no longer clipped.
  - Higher bells overlap the lower ones.
- **Spikes (plantain).** The core is a slim cylinder (about 7 mm) with a
  band in flower: filaments tipped by pale anthers, fading to a haze under
  a pixel.
- **Daisy and yarrow heads.** These turn out from the plant as well as up:
  a daisy by up to about 1 rad, a yarrow umbel only a little.
- **Presets.**
  - Daisy has 5 stems.
  - Plantain's leaves lift off the ground (rise 0.85, arch 0.45).
  - Foxglove has 17 bells over 0.5 m.
  - Poppy's stems nod more (0.25).
  - The meadow colonies are larger: daisy patchCover 0.55, poppy 0.45 and
    abundance 0.85, yarrow 0.45 and abundance 0.8.

**Near ground.** Between the blades near the eye there is now thatch (pale
straws lying every way, at two scales), crumbs of soil and the green of the
bases. All of it fades by pixel footprint.

**Budget.**

- **Draw order.** A forest's clumps and crowns draw after the rest of the
  opaque scene, so early-Z rejects them behind near trees and ground
  (`MeshDraw::order`). This applies only while the eye is under the canopy
  top within 40 m. From over the canopy the crowns are what hides the
  floor, and drawing them last cost about 0.3 ms at the descent (t = 13 s).
- **Timer marks.** The scene GPU timer now has 190 marks, up from 62. A
  forest's grass draws had used up the marks, so the far tiers were landing
  in "rest".
- **Tried and dropped.** A single shadow tap for leaves in the last cascade
  made the trace slower (p95 5.27 to 5.32). The likely cause is register
  pressure from a dynamic `spread`.

**API.**

- `setHerbs({far})`.
- `grass.herbPlants(x, z, radius)` returns the plants nearest a point,
  computed on the CPU as the shaders pick them.
- `grassStats().herbInstances` has three rings.
- HerbSpecies has `spacing`.
- The herb record is 52 floats.
- The grove capture frames the nearest plant (the nearest flowering one for
  the meadow) in its 1 m pane, and the red-and-white roller is hidden in
  herb captures.

### Numbers

**Flight** (`forest trace`, 1280x720, 4x MSAA). The desktop GPU was in use
during these runs, which added run-to-run noise.

| | 8b | Draw order alone (`r9ord`) | Herbs off (`r9q`) | Final (`r9t1`..`r9t4`) | Limit |
|---|---|---|---|---|---|
| max | 5.62 | 5.65 | 5.42 / 5.32 | 6.11 / 5.70 / 5.48 / 5.38 | 6.0 |
| p95 | 5.14 | 5.03 | 4.85 / 4.99 | 5.05 / 4.96 / 5.11 / 5.11 | 5.1 |
| median | 4.30 | 4.22 | 4.10 / 4.11 | 4.23 / 4.22 / 4.21 / 4.20 | 4.4 |

- **Herbs' cost.** Herbs cost about 0.1 ms median. Before the steep-view
  skip, the far ring cost about 0.2 ms from 36 m up.
- **The max.** In one run of four, the max sits on a plateau at t = 13 s
  (eye 36 m), whose time moves from run to run.

**Herb cost in the grove** (`herbs <name> perf`, range 30): 0.07 to 0.38 ms
per view.

**Hand-over** (`forest herbs`, `r9_herbmetrics.json`): drgb against plants
drawn to 200 m, per band.

| Shot, sky | 2-12 | 12-24 | 24-40 | 40-80 |
|---|---|---|---|---|
| glade noon | 0.4 | 0.9 | 2.4 (8b: 5.8) | 5.5 (8b: 15.6) |
| glade golden | 0.3 | 0.5 | 2.4 (8b: 1.6) | 3.3 (8b: 6.5) |
| trunks noon | 0.2 | 0.2 | | |
| trunks golden | 0.4 | 1.4 | | |

**Tests.**

- bro: scene 79/79, flora 7/7, grass 2/2.
- broflora ctest: 4/4.
- broworkshop `validate.sh demos/forest demos/grove`: 4/4.

### Captures

In broworkshop `demos/grove/captures/`:

- `r9_<name>_{noon,golden}.jpg`: bracken, daisy, poppy, yarrow, plantain,
  foxglove, bluebell and the meadow at 1, 3, 10 and 30 m. The 1 m pane
  frames a plant.
- `r9far_meadow_{noon,golden}.jpg`: the meadow at 0.5, 3, 30 and 60 m.

In `demos/forest/captures/`:

- `r9_herbmetrics.json`;
- `r9t1`..`r9t4_trace.json`.

### Still open

- **The budget has no clear headroom.** p95 is 4.96 to 5.11 against 5.1,
  and max goes over 6.0 in one run of four. The clump and crown sprites
  (about 1.8 ms) and the leaves (about 2.0 ms) are most of the heaviest
  frames. A real saving has to come from there.
- **The meadow at 150 m.** From the grove, the hills' haze hides it. Making
  the ground 200 m and the eye higher did not help. It needs a large open
  scene, for example the forest's glade made bigger.
- **Near forms that still read wrong.**
  - The foxglove's bells at 1 m still stack into a rope.
  - The poppy's stems are thin and hidden in the grass, and not hairy.
  - The yarrow's umbels are flat white discs, with no florets above a pixel.
  - The plantain's leaves are plain.
- **The golden glade's 24-40 m band** is 2.4, up from 1.6.
- **Carried over:** the far canopy's sun-height gap (noon +3.6, golden −7.9).

## Step 8c: the frame, the far canopy's sun, the meadow at true scale, near forms

### What changed

- **Leaves posed once a leaf** (bro ccf3fc07). A compute pass
  (`forest_leaf_pose.comp`) places, widens and sways each colour leaf into a
  per-frame pose buffer, 64 bytes a leaf. Each vertex reads its point from
  that pose. A group past the buffer's 4 M leaves poses vertex by vertex
  (`FOREST_LEAF_INLINE`). The per-band metrics are unchanged.
  `BRO_VK_PIPELINE_STATS=1` logs each scene pipeline's register counts:
  sprite FS 96, leaf 72, cluster 80.
- **The far canopy follows its leaves at every sun height** (bro 93311593).
  - A far tier is lit through one normal a pixel, but its leaves face every
    way about it. Its wrapped diffuse is now the wrap's mean over cosines
    spread 0.6 either side (`SceneSurface::leafSpread`,
    `FOREST_MASS_SPREAD`).
  - A crown's one-disc shadow caster stops 0.4 of its leaves' even depth
    (`FOREST_CROWN_CLUMPING`, forestry's clumping index). An even disc
    shaded a golden far canopy through the low sun's long shadows.
  - What showed it: in the direct light alone, the gap was +5.1 at noon and
    −5.9 at golden. With shadows off it was +7.9 / +1.9. So the low sun's
    crown shadows were too strong, and the narrow normal spread lit noon
    too fully.
- **A meadow at true scale in the forest** (broworkshop 79f7b76, 16ec510).
  - `MEADOW` is an 85 m clearing in a shallow hollow, with a ragged, shrubby
    forest edge. Inside it the trees are cut and the ground is damper.
  - The `meadow` shot runs from the edge across it to 150 m. The
    `understory` shot is a forest interior.
  - `capture.js meadow` writes full-frame JPEGs at 0.5, 3, 30, 60 and 150 m,
    and the understory view, for each sky.
- **Near forms** (bro 1f862e6a, broflora 90d3b7c).
  - **Yarrow.** Its head is a dome of tiny five-lobed florets with cream
    eyes, on six stalks, its flat underside seen from below. Its leaves are
    narrow fronds of many finely cut pairs.
  - **Foxglove.** Its bells hang on short pedicels, mostly to one side and
    the odd one to the other. They are shorter and a little apart, each
    shaded at its shoulder.
  - **Plantain.** Raised pale ribs run base to tip and meet at both ends,
    and the blade puckers between them.
  - **Poppy.** `HerbSpecies::hairs` (record slot 43) gives a stem a wider
    card with fine pale hairs spreading up and out. The poppy's stems are
    pale grey-green and stand clear of the grass.
- **The turf close by** (`groundBlades`, scene_ground.glsl).
  - It lays tapering short live blades and fewer dead ones, each its own
    tone, over the sward's dark foot. Soil shows only in fine crumbs where
    the grass is thin.
  - The grove's ground is now turf (`turf: true`). It had been a flat
    vertex-coloured soil mesh, which was the flat dark grey-green at 0.5 m.

### Numbers

**Far canopy, By size − Leaves dY** (`p1` → `g6_metrics.json`):

| Shot, sky | 40-80 | 80-160 |
|---|---|---|
| canopy noon | +1.9 → +2.5 | +3.6 → **+2.9** |
| canopy golden | −2.6 → −1.5 | −7.7 → **−5.3** |
| skim noon | +9.6 → +8.6 | −4.7 → −5.5 |
| skim golden | −5.6 → −6.0 | −6.3 → −5.3 |

**Flight** (1280x720, 4x MSAA). The GPU was shared with other bro-headless
sessions through this step, and a contended run spikes to a p95 of 8.7.

| | ms |
|---|---|
| p95 | 4.62 to 5.0 |
| median | 3.9 to 4.2 |
| max | about 5.4 to 6.1 |

**Tried without gain, all reverted:**

- dithered cascade selection for leaves;
- nearest-first lobe order (reversing it costs +0.17 ms, so outermost-first
  stays);
- a separate crown fragment build for the lobes (clumps:broad stayed at
  0.635 ms);
- sorting shadow casters by light depth;
- cores for broad nodes.

In clumps:broad's 0.65 ms, lighting is about 0.26 ms, and the direct light
is about 70% of that.

**Herb hand-over in the forest meadow** (`herbs @-69.3/12/-54.6/-151.4/0/-128
h3`, an elevated view; full vs handover):

| Sky | 24-40 drgb | 40-80 drgb | 80-150 |
|---|---|---|---|
| noon | 1.7 | 5.1 | Y 169.0 vs 179.5, drgb 19.7 |
| golden | 0.7 | 2.9 | drgb 9.7 |

From the ground the bands up to 80 m stay at drgb ≤ 2.4. Past the far ring
(72 m) the turf is brighter and flat, and the flowers are gone. A far of
150 m took `opaque:grass` from 0.25 to 0.47 ms, and 96 m took it to 0.32;
both were reverted.

**Tests.**

- bro: scene 79/79, flora 7/7, grass 2/2.
- broworkshop `validate.sh demos/forest demos/grove`: 4/4.

### Captures

In broworkshop `demos/forest/captures/`:

- `m3_meadow_{noon,golden}_{0.5,3,30,60,150}m.jpg`;
- `m3_understory_{noon,golden}.jpg`;
- `meadow_meadow_*` and `meadow_understory_*`: the same shots after the
  near-turf change;
- `g6_metrics.json`, `p1_metrics.json`: the far canopy, after and before.

In `demos/grove/captures/`:

- `n3_foxglove_noon.jpg`;
- `n4_poppy_noon.jpg`, `n4_plantain_noon.jpg`;
- `n1/n2_yarrow_noon.jpg`;
- `n10_meadow_{noon,golden}.jpg`: the near ground at 0.5, 1, 3 and 10 m.

### Still open

- **p95 is not robustly under 4.7.** The crown trace (0.45 to 0.57 ms) is
  the next structural lever: a two-level bound over a crown's lobes. After
  that come posing the shadow casters in compute, as the colour leaves now
  are, and the sprite fragment shader's register pressure.
- **Past the far herb ring** the turf reads brighter and flat, and the
  flowers vanish at 72 m. The turf needs the flowers' colour in its mean at
  that distance, or a cheaper far ring that can reach 150 m.
- **The skim view's 80-160 m band at noon** is −5.5, down from −4.7.
- **Yarrow's heads** are still a little uniform from plant to plant.
- **The near turf at 0.5 m** still shows faint soil blotches.

## Step 9: motion

Every still frame looked right; between frames the motion often did not.
This step fixes the motion's continuity and its response, each claim
against the motion oracle, the same reports before and after (the
"before" reports are kept in `captures/motion_before/` of each demo).

### What changed

- **A clock for oscillators** (bro `scene_camera.glsl`, `wind_field.h`,
  `scene_renderer.h`). The wind time is a double on the CPU, and the camera
  block carries it reduced modulo 256 s (`windParams.w`). Every oscillator
  takes its phase from `sceneWindCycles(hz)`: its frequency quantised to
  1/256 Hz, so its phase is exact and wraps without a seam. No sinusoid
  multiplies a wrapped phase, and no varying frequency multiplies the
  clock.
- **Leaf flutter** (`vegLeafTurn`). Each leaf flutters at its own fixed
  frequency, 2 to 4.5 Hz (`VEG_LEAF_FLUTTER_HZ`), its amplitude following
  the drag where its limb is, in bursts at a leaf's own 0.45-0.75 Hz. The
  old flutter's frequency followed the gusts times the absolute clock, so
  its instantaneous frequency grew with play time and aliased into noise.
  Grass blades, herb leaves and conifer sprays take the same clock.
- **A smooth gust field.** The gust noise fades quintically
  (`sceneWindNoise5`) and its threshold is a hyperbola, not `max(g - 0.45,
  0)` (`sceneWindSoftRamp`), so the drag is C2 in time; a level's kick is
  a smooth |change| (`vegSmoothAbs`). Both had put jerk spikes at every
  gust edge and wherever two delayed gust samples crossed.
- **Each level feels the wind where it is, when it does** (`vegPose`).
  - The trunk reads the drag at its root a quarter of its period back; a
    limb and its twigs read it at the limb's own rest point a quarter of
    the limb's period back. A gust now sweeps through a crown from its
    upwind side.
  - Limbs and twigs ring with the crown's turbulence (about 40% of the
    drag, `VEG_CROWN_TURBULENCE`: a canopy's turbulence intensity is 30-50%
    of its wind, of its drag about twice that), the trunk and herbs with
    the open's 15%. At 30%, three quarters of every part's motion was still
    the one bend the whole crown shares; at 50% the lockstep fell further
    (correlation at lag 0, median 0.58 against 0.66) but a crown 20 m off,
    its leaves under a pixel, flickered 3.07 levels (p90) against 2.56,
    and the grove tree's pops rose by a third.
  - The ring is a narrow band, two sinusoids at f(1 - ζ) and f(1 + ζ), and
    a third across the wind, so no two parts keep step.
- **Stiffness** (broflora `sway.h`). `kSwayTwigGain` 1 → 4: a leafy spray's
  base gives in its fork as a limb's does; as a rigid footing it rang at 6
  to 8 Hz. `kSwayLimbGain` 4 → 2.5: a limb rang well under its trunk, one
  slow sway with it. A limb rings at most 0.8 of its sprays' frequency
  (`sway.cpp`): a shrub's short stiff stems are held at their forks by
  their sprays as much as by their wood, and rang at 4.6 Hz (boxwood) to
  6.5 Hz under 5.4 Hz sprays, which together read as an 8 Hz buzz. Only
  the frequency moves; the bend stays the stiffness's, so the conifers'
  heavy low boughs stay as still. Boxwood, holly, hydrangea limbs now 3.0
  to 3.3 Hz under 4.3 Hz sprays; conifers' 0.5-1.6 Hz under 3.1 Hz.
- **Movers part shrubs and low foliage** (bro `plant_movers.h`,
  `vegMoverPush`). A grass node's movers also push the plants: each keeps a
  trail sampled every 0.1 s on the wind clock (20 samples, in the camera
  block), and a plant part is pushed away from the body through a damped
  spring's impulse response (1 Hz, ζ 0.4) to each sample, up to the
  mover's `height` (addMover's new option, 1.8 m by default). It parts as
  the body arrives and swings back once after.
- **Grass springs back** (bro `scene_grass.glsl`, `grass_node`). A bend
  releases as a damped spring (0.9 Hz, ζ 0.3), a trample critically damped
  over 0.7 s, with 6% of it relaxing over 4 s. The old release was an
  exponential, so nothing swung back and half of a trample stayed after 8
  s.
- **Herbs compose their bends as angles** (`herbTangent`). The rest droop
  and the wind's tilt add as tilts from upright, the wind's saturating near
  level. As one vector sum, normalised, a nodding head pushed against its
  droop passed through nothing and turned over in a frame (the poppies'
  pops as the walker passed). A leaf's card is square to its own outward
  way, not to the vertical, so a frond pushed through upright no longer
  flips face. The cup's inside/outside is blended across its rim. A
  flower's outward way (its face's turn out from the plant, a raceme's
  side) is its stem's at rest, not its tip's from the root, which a stem
  pushed over its root swung half a turn in a frame or two.
- **The eye passing through a crown** (`VEG_NEAR_SHRINK`). A colour leaf,
  forest or scattered, shrinks to its middle within a metre of the eye and
  is gone by 0.25 m, rather than filling the view for a frame.
- **The forest's handovers** (bro `scene_mesh_drawer_forest.cpp`,
  `forest_node`).
  - Near leaves hand over to cards (clusters) through ±6% of
    `cardWidening`, the two splitting the kept run (a shuffled prefix, so
    each a random share of the crown; `TreeLod::cardShare`); the switch
    was whole. (Both forms over the whole run, dithered apart, doubled the
    leaves' vertex work through the band: 8 ms flight frames.)
  - Shadow casters draw every tier at its share: the leaves keep that share
    of their casters, a sprite stops that share of the light
    (`shadow_forest.frag`). A caster drew only its heaviest tier, so the
    shadow switched whole where two shares crossed a half. The camera's
    leaves hand the caster's crowns to clumps over a band, not at a half.
  - The budget's detail factor moves only so fast (`settleDetail`): a
    coarser fit 6% a frame, a finer one 2% a frame once it is 4% finer, a
    jump of the eye taking it whole. Each frame's fit, quantised and moving
    with the camera, re-thinned the crowns a little every frame.
  - Bark thinner than the entry's cut thins to nothing over 1.6 times it
    (`scene_forest_tube.glsl`, the entry's `widening` slot carrying the
    cut radius), so a twig fades out as the eye backs off rather than
    blinking out.
- **The oracle.** `sway_check.js` runs on `sway.js`. The truth names
  lockstep (correlation at lag 0, peak lags, the root drag's share, the
  upwind → downwind twig), attributes each jerk spike to a gust edge or a
  kick crossing, and only counts a spike that stands over its neighbours.
  The contrast floor (above) for grass and herb runs. A demo's grove test
  checks each spray against its own tree's trunk (half again its
  frequency), not the slowest spray against the quickest trunk.

### Numbers

Before is `captures/motion_before/`, now `captures/motion/` of each demo
(1280x720; the grove from broworkshop's `motion.js` commands as named).

Truth (`truth`, sway.js, the grove's broadleaf, gusty, 20 s):

| | before | now |
|---|---|---|
| jerk spikes, trunk top / limb middle | 4 / 32 | 0 / 0 |
| correlation at lag 0, 27 part pairs, median | (0.86-0.90 a pair) | 0.66 |
| share that is the one bend at the root | 76-81% | 56-82%, median 72% |
| upwind → downwind twig | | corr 0.58, 30 frames on |
| trunk / limb dominant Hz | 0.62 / 0.49 | 0.32 / 0.67 |
| twig high rms / p-p / max cm a frame | 17.1 / 72 / 3.4 | 18.9 / 102 / 8.5 |
| flutter aliasing, clock 0 to 3600 s | 45-100% | 0% (4.2 Hz) |

`sway_check.js`: ok at every level (twig rms 2.47 against the
oscillator's 3.18, speed 31.3 / 33.8 cm/s).

Plants (flicker p90 levels, pops; top / middle / bottom):

| run | before | now |
|---|---|---|
| tree 12 m | 6.84 / 2.60 / 2.69; 577 / 86 / 100 | 2.50 / 2.40 / 2.61; 78 / 61 / 65 |
| tree, clock 600 | 8.18 / 4.01 / 3.89; 482 / 291 / 337; LOCKSTEP all three (0.92-0.98) | 4.34 / 3.84 / 3.97; 286 / 273 / 270; LOCKSTEP top (0.93 at lag 1) |
| tree, clock 3600 | | 2.29 / 2.32 / 2.30; 72 / 67 / 79 |
| tree 7 m (in the crown) | 3.21 / 2.52 / 1.80; 332 / 238 / 61; NOISE all three, wiggle 6.2-7.5 Hz | 2.27 / 1.90 / 1.32; 175 / 118 / 25; NOISE bottom, wiggle 4.0-6.3 Hz |
| fir 8 m | 0.71 / 0.72 / 0.73; 0 / 0 / 1; LOCKSTEP bottom (0.90) | 0.89 / 0.63 / 0.72; 15 / 11 / 0 |
| boxwood | wiggle 7.1 / 7.3 / 7.9 Hz | wiggle 6.1 / 5.0 / 7.5 Hz; NOISE bottom (7 of 38 blocks) |
| bracken (far / mid / near) | 1.79 / 1.52 / 2.40; 70 / 208 / 220 | 1.69 / 1.54 / 2.98; 52 / 125 / 144 |
| meadow | 0.43 / 0.62 / 0.72; 0 / 8 / 0 | 0.37 / 0.49 / 0.63; 13 / 0 / 0 |

Movers (still air; change from rest at 0-0.3 m from the path):

| through | before | now |
|---|---|---|
| bracken | 50% back by 5.60 s, 40% left at 8 s | 3.37 s, 19% left |
| grass | 50% back by 7.27 s, 47% left; 84 pops | 5.10 s, 44% left; 99 pops |
| grass, contrast floor | | 4.23 s, 36% left; 66 pops |
| boxwood (walker inside its crown 2 s) | its speed x9.3 while inside, change 0.97 levels | x85.5, 9.18 levels; 1 s after 1.80, at the end 0.44 |

Forest (flicker p90, pops):

| run | before | now |
|---|---|---|
| broadleaf 5 m | 4.01; 1155 pops, 122 kicks | 2.81; 598 pops, 10 kicks |
| 20 m (subject / view) | 1.94 / 2.58; 111 / 113 | 2.52 / 3.45; 158 / 151 |
| 60 m | 0.90 / 0.94; 0 / 0 | 1.08 / 1.15; 0 / 0 |
| 150 m | 0.80 / 0.77 | 0.83 / 0.86 |
| trunks (upper / middle / ground) | 1.25 / 0.81 / 0.78; 8 / 0 / 0 | 1.56 / 0.99 / 1.03; 10 / 1 / 1 |
| flight, gusty (upper / middle / lower) | 8.37 / 4.93 / 5.05 | 9.13 / 4.99 / 5.17 (still air the same: the view's own) |

Dolly hand-overs (mean-luma step, times the usual): card at 28.6 m
-0.57 (x3.4) → -0.60 (x3.6); leaves → clumps at 67 m -0.69 (x5.3) →
-0.39 (x3.0); clumps → crowns at 264 m -0.61 (x2.8) → -0.65 (x3.3).

Still frames: `capture.js metrics` (s9) against g6, |Leaves - By size| Y
grows by at most 1.7 levels in any band (trunks golden 10-20 m), and
falls by 2.0 at skim noon 80-160 m. Flight (`trace`, five runs): p95
4.85-4.91 ms, median 4.09-4.12 (HEAD: 4.61 / 3.91); max 7.2-7.9 ms in
one second near t = 26-28 s under the canopy (HEAD 6.02-6.26).

### Captures

- Motion reports: `broworkshop/demos/grove/captures/motion/`,
  `broworkshop/demos/forest/captures/motion/` (before:
  `captures/motion_before/`).
- The grass on the contrast floor (`capture.js grassfloor`):
  `broworkshop/demos/grove/captures/grassfloor_{noon,golden}_{side,down}_{0.3,1,3,8,20}m.png`.
- Forest still metrics: `broworkshop/demos/forest/captures/s9_metrics.json`.

### Still open

- The flight's max frame: 7.2-7.9 ms for a second under the canopy
  (p95 and median within 5.1 / 4.4). Casters drawn at every tier's share
  add 2 M shadow vertices (3.4 → 5.4 M); with only the heaviest tier, or
  with no card band, a run still reached 7.4-8.0 ms, so neither alone is
  it.
- A crown 20 m off flickers more (2.52 against 1.94): its limbs and
  twigs ring more (the turbulence that broke the lockstep), its leaves
  under a pixel. The clock-600 tree keeps a LOCKSTEP flag at its top.
- The boxwood's image still wiggles at 5-7.5 Hz (NOISE on 7 of 38 bottom
  blocks), though its limbs ring at 3.3 Hz and sprays at 4.3 Hz now; the
  bottom third holds grass, and the flag's blocks move under a pixel.
  Lavender's flower stalks still ring at 6.5 Hz (bare, light, 1% sway).
- Movers: the grass keeps 36-44% of the change at 0-0.3 m 8 s on, and
  a slow drift (2-3 levels over the run, starting before the walker)
  shows 1-2.5 m off the path on the meadow's ground but not on the
  contrast floor. The boxwood run has 72 pops as it parts.
- The card hand-over at 28.6 m (x3.6) and crowns at 264 m (x3.3) still
  step; the dolly's mean luma steps near 190 m where the last shadow
  cascade ends without a fade. Bark's sides switch 5 ↔ 10 at once.
- Item 8 (the bracken pinnae at 2.5 m) was not separately measured.

## Step 10: grass

The grass node's blades (bro scene_grass.glsl), judged on the grove's
contrast floor and its own ground. Everything is still derived per blade
from its cell's hash, the field and shared noise: nothing is stored per
plant or per place.

### What changed

- **A blade's form.** A blade is a sum of tilt vectors (its lean at the
  root, its rest curvature, the wind and the movers' bend), saturated at
  2.3 rad and integrated along it with Simpson's rule, so it arches and
  droops without a flip whatever the ways combine. Leaves taper to a fine
  point (blunter once about a pixel wide, so the tip does not flicker),
  fold along the midrib near the eye (a shallow V in the shading normal to
  12 m) and twist a little along their length. Older leaves are broader.
- **Posture.** Age (a hash) sets each leaf's length (0.45-1.3 x the tuft),
  lean (0.1-0.75 rad out) and droop (more with length, more where the
  field is dry: sun-dried leaves splay and flop). 4% lie lodged. Dead
  leaves are short, lie out at 0.75-1.3 rad and are straw to dun.
- **Tufts.** Tillers rise from a small crown and lean out from it; cells
  gather toward tussocks (one a 0.42 m square, anywhere in it, each
  gathering its own share), the bunching set by `tufts` (default 0.35) about
  a shared noise of a few metres: a sward in places, bunch grass in others.
- **Flowering culms** (`flowering`, default 0.08; the grove's meadow 0.25):
  a bare stem 1.35-1.75 x the tuft carrying a head in its top quarter, a
  loose panicle (branches rising to one side, spikelets along their outer
  part) or a dense spike, by a noise of tens of metres; fresh green flushed
  purple with pale anthers, ripening to straw where dry. The card is cut
  per pixel (scene_grass_surface.glsl, under 4x MSAA its share of the
  samples); the spikelets fade to their mean once a row is under about two
  pixels, and a card widened to a pixel covers only its true width. Culms
  are the same share of the blades in every ring, so a far culm stands for
  as many culms as its leaves do leaves. Their stems give 0.45 as much to a
  push and are flattened half as much.
- **Light.** Grass draws with its own build of mesh.frag (SCENE_GRASS), so
  the forest's leaves pay nothing for it. The light through a blade follows
  its own face toward the eye (SceneSurface::thinNormal), not the ground
  normal it is shaded by, so blades glow against a low sun; live leaves
  have a cuticle sheen near (roughness 0.42 to 6 m), dead leaves and heads
  are matt.
- **Ring 2's cover.** Rings 0 and 1 cover 5.6 and 5.0 m of blade width a
  square metre; ring 2 (20-55 m) had 2.2, so more turf showed and it drew
  4-7% darker. It now has 6 blades at 19 x the width (5.0): luma steps at
  20 m are gone. A ring's blade tone spreads by its coarseness^0.12.
- **The movers.** A trample rises critically damped over 0.35 s (was 0.6)
  with 1% over 2 s (was 2% over 2.5 s); a bend and a trample build up over
  about a quarter second rather than in a frame or two; under the mover's
  body the bend lies along its path (at the disc's edge out to the side),
  so a flattened blade or herb no longer swings across as it passes over.
- **Yarrow** (item 7): the umbel's florets sit in staggered rows with
  jittered size and fade to their mean by a quarter of a cell a pixel: the
  grey gridded balls at 8 m are white floret domes.
- **The turf** (scene_ground.glsl) mixes in the dead leaves' and heads'
  colours by the same shares, so the far meadow keeps the near one's cast.
- Measures: `capture.js grassfloor ... floor=normal` (the same stills over
  the grove's ground) and `capture.js grassbands` (below).

### Numbers

**Ring hand-overs** (`grassbands`: knee and eye height, four ways, each
view drawn at the node's range and at 1.3 x it; between an edge and 1.3 x
it the two differ only in the ring, so the terrain's patches drop out;
control: the band inside, both the same ring):

| Edge | HEAD noon / golden luma | Now noon / golden luma | Now local contrast | Control |
|---|---|---|---|---|
| 7 m | -1.3% / +1.3% | +0.2% / +3.6% | -10% / -7% | +0.3 / +1.6% |
| 20 m | -4.0% / -6.6% | -0.8% / -1.0% | -15% / -16% | +0.1 / +0.4% |
| 55 m | -0.9% / -1.1% | +0.2% / +0.3% | -4% / +0.4% | -0.4 / -0.7% |

**Motion oracle** (`motion.js`, before = bro 989babdb):

| Run | Before | After |
|---|---|---|
| mover grass, contrast: 0-0.3 m to 50% / left at +8 s | 4.50 s / 39% | 1.67 s / 10% |
| mover grass, contrast: 0.3-0.6 m | 2.73 s / 25% | 1.60 s / 5% |
| mover grass, ground: 0-0.3 m | 5.20 s / 44% | 1.90 s / 12% |
| mover grass, ground: 0.3-0.6 m | 2.77 s / 28% | 1.60 s / 6% |
| mover grass pops, contrast / ground | 74 / 95 | 52 / 92 |
| meadow gusty, ground: events; near coherence | 49; INCOHERENT (0.13) | 43; no flag |
| meadow gusty, ground: wiggle median far / mid / near | 3.7 / 3.5 / 3.6 Hz | 3.1 / 3.1 / 3.2 Hz |
| meadow gusty, contrast: events | 57 | 76 (flashes near 24 -> 28, middle 2 -> 12) |

**Cost** (RTX 4090, 1280x720, 4x MSAA): grass in the grove's `perf` shots,
HEAD's shaders against these on one binary, three runs each (+-0.3 ms):
meadow 1.18 / 1.15, ground 1.40 / 0.89, grove 0.93 / 1.10 ms. Forest flight
on bro da708766 + this: p95 5.05-5.09, median 4.13-4.17 ms (HEAD's grass
shaders the same binary: 5.01-5.15 / 4.21-4.23; without the grass 4.48 /
3.78).

### Captures

demos/grove/captures/: `s10k_{noon,golden}_{side,down}_{0.3,1,3,8,20}m.png`
(contrast floor), `s10k_normal_...` (the grove's ground),
`s10j_{noon,golden}_{knee,eye}0.0.png` (the band views), motion reports
in `motion/` and the before set in `motion_s10_before/`.
demos/forest/captures/: `s10_meadow_{noon,golden}_{0.5,3,30,60,150}m.jpg`,
`s10_understory_{noon,golden}.jpg`.

### Still open

- The outer rings' local contrast falls 10-16% at 7 and 20 m (fewer, wider
  blades are a smoother grain); luma no longer steps. Golden light at 7 m
  is +3.6%.
- Pops in the mover runs are mostly the herbs (poppy heads) parting and
  swinging in a gust front; the meadow's contrast-floor run has more flashes
  than before (the near sheen or the culms' heads; not split out).
- The near sward still shows floor between tufts on the contrast floor
  (about 1400 blades a square metre near the eye); over the turf it reads
  closed.
- Gust waves: the sheen of the blades a gust bends is as before; whether a
  wave reads travelling across a field was not measured separately.
- Boxwood (5-7.5 Hz) and lavender (6.5 Hz) wiggles and the meadow drift
  1-2.5 m from the path were not worked on: baseline numbers are in
  `motion_s10_before/` (boxwood bottom NOISE 8/26 blocks at 9.1 Hz;
  lavender FLICKER 6/22 middle).
- The forest's meadow at 30-150 m was judged by eye (it keeps its colour
  and grain); the band measure only reaches 70 m in the grove.

## Step 11: leaves on twigs

Better motion showed what stills had hidden: a broadleaf's leaves were
scattered through their clumps' lobes (clumpLeafFrame), each turned its
own way, so they floated rather than grew. Now every broad leaf hangs from
a node of a shoot, the shoots from sprays and the sprays from stems, all of
it derived per clump at run time; near trees draw the twigs.

### What changed

- **The shoot model** (bro `scene_forest_shoot.glsl`, the forest node's
  leaf form 0; conifers and the scatter node's clump mode are unchanged).
  A clump of n leaves carries S = max(1, (n+4)/8) shoots on
  NB = max(3, (S+2)/5) sprays (S < 3: one each). Leaf j (its key less the
  clump's key base) is node j/S of shoot j%S; shoot s is on spray s%NB
  (s/NB = 0 its terminal shoot, then laterals left and right at 1 rad).
  Sprays sit on a Fibonacci lattice over each of the clump's three lobes,
  their axes and shoots along great circles a little under the lobe's
  surface (the shell), flattened as the clump is: the foliage lies in
  layers on the outer shell. Each lobe has a stem from the clump's base
  out past its centre (a quadratic curve); each spray's twig leaves it
  along its length. Spacing in leaf lengths: 0.6 between a shoot's nodes
  (closer toward the tip), 1.1 between a spray's shoots.
- **The leaf at its node.** Its azimuth by phyllotaxis: alternate (two
  ranks), opposite (decussate pairs) or a 2/5 spiral, ±0.25 rad. The blade
  leaves the shoot at 0.95 ± 0.17 rad, is turned 60% into the spray's
  plane (facing out of the crown and up by the scatter's up bias), droops
  0.12, keeps the scatter's tilt jitter; larger toward the shoot's middle
  (0.8 → 1.15, 1.03 at the tip). The node lies on the twig as drawn (the
  chord of its third of the shoot), not the arc 3-8 mm off it.
- **The petiole.** broflora `Species::phyllotaxis` (0/1/2) and `petiole`
  (of the blade's length, 0..0.6; default alternate, 0.2) go through
  `ForestSpecimen` to `specimenLeaves` [6], [7] and the specimen record
  8.yz. The cut card keeps its stalk row at the node and moves the blade's
  rows out by the petiole; `forestLeafCut` draws the stalk box-filtered,
  0.03 of the card wide. It shrinks away as the leaf falls from 16 to 8 px
  (with it the far tiers' cover rose, a contrast regression in every
  band). Presets: birch 0.3; boxwood opposite 0.05; holly spiral 0.1;
  hydrangea opposite 0.25; hibiscus spiral 0.35; oak, elm, beech
  alternate 0.2. World format 10.
- **Twigs** (`mesh_forest_tube.vert`'s twig build, the drawer's Twigs
  group). A clump has 6 + 5NB + 3S pieces (two a lobe stem, five a spray,
  three a shoot), 64 an entry, 6 vertices a piece: camera-facing ribbons
  at least a pixel wide, cover their width under it, crown-shaded,
  shoots tinted green. Stems and sprays draw while 2·0.045·leaf length is
  0.15 px or more, shoots while 2·0.014·leaf length is; they fade over a
  widening of `cardWidening` x 0.95 → 1.3 (the natural widening, so a
  forced Leaves tier draws the same twigs as By size). A piece is culled
  whole by its radius at the clump's distance (culling a vertex at a
  time made streaks). They count toward the vertex budget and cast no
  shadow.
- **Motion follows the hierarchy.** Each end of a twig and each leaf's
  node take the wood's pose of their clump (vegPose) and then the same
  spray and shoot bending: a spray's axis bends by 0.08·sqrt(drag) at
  1.4-2.2 Hz, a shoot by 0.12·sqrt(drag) at 2.2-3.2 Hz (quadratic along
  each, `sceneWindCycles`); the leaf's pose adds that offset and turn, and
  its flutter turns it about its node.
- **The key base.** Each clump's record carries its first placement key
  (r2.z, `ForestNode::gpuData`), so a leaf finds its index in the clump
  with no table.
- **broworkshop.** `lib/kit/sway.js` ports the shoot model step for step;
  grove's default path is now the forest node (`gpu`, the scatter node,
  is kept for comparisons and for `truth`'s bark-only render check);
  `motion.js twigs` measures the leaves against their twigs; both
  `capture.js foliage` commands take the views below.

### Numbers

- Twig oracle (`motion.js twigs`, the grove broadleaf, 62 leaves over
  36 clumps, leaf 18 cm, petiole median 37 mm): node to drawn twig at
  rest median 0, max 5.2 mm. Gusty, the largest over 10 s: median 7.4 mm,
  p95 21.3 mm, max 24.4 mm, p95 0.63 and max 0.77 of the petiole. Breeze:
  p95 10.4 mm, max 0.38 of the petiole. The tip moves about its node by
  7.6 cm (median; 15.7 max) gusty, 4.8 cm breeze.
- Forest flight (1280x720): before p95 5.37, median 4.29 ms; with the
  twigs, on a quiet GPU, p95 5.01, median 4.19 ms. Later runs shared the
  GPU with another bro-headless (p95 5.17-5.48, median 4.18-4.26; one
  13.6 / 8.2). The twig pass in frames over 4.6 ms: median 0.07,
  p95 0.19 ms. The budget's detail factor rose with them (skim 1.96 →
  2.71, trunks 2.09 → 2.54, canopy 1.00 → 1.06).
- Leaves vs By size per band (mean Y, L/B), before → after: canopy noon
  20-40 112.7/113.2 → 113.2/114.6, 40-80 119.2/120.5 → 118.3/122.2,
  80-160 111.1/113.6 → 110.7/113.8; skim noon 10-20 126.7/129.6 →
  125.8/130.6, 20-40 148.9/150.7 → 146.8/150.2; glade noon 20-40
  98.7/102.0 → 98.7/102.9. Golden canopy 82.0/79.2, 87.9/86.3, 90.1/84.7
  → 82.5/80.0, 88.2/87.1, 89.8/84.8: the same or closer.
- Handover dollies (forest `handover leaves|cards`): no step at the twig
  fade or the petiole's.

### Captures

- Grove (forest path), 1, 3 and 8 m and inside, noon and golden:
  `broworkshop/demos/grove/captures/{before,after}_{broadleaf,birch,hydrangea,holly}_{noon,golden}_{inside,1m,3m,8m}.png`.
- Forest glade edge:
  `broworkshop/demos/forest/captures/{before,after}_{broadleaf,shrub}_{noon,golden}_{inside,1m,3m,8m}.png`.
- Motion: `broworkshop/demos/grove/captures/motion/twigs_{gusty,breeze}.txt`,
  `plant_{tree,birch,hydrangea,holly}_gusty_motion*`; handovers
  `broworkshop/demos/forest/captures/forest_handover_{leaves,cards}_strip.png`.
- Metrics: `broworkshop/demos/forest/captures/forest_metrics.json`.

### Still open

- Canopy noon 40-80 m: Leaves vs By size 3.9 levels apart (1.3 before),
  the one band past ~2; By size brightened 1.7 there.
- At 1 m the spray twigs and lobe stems read as long straight rods
  through a crown's front: they should be shorter and more crooked, and
  darker inside.
- The holly flags 15 single-block pops in its top third (gusty): its
  glossy leaves' glints at the silhouette. There is no before run to
  compare.
- bromesh has no CPU twin of the shoot model (sway.js is the only port),
  and the scatter node's clump mode (grove's `gpu` path) still scatters.
- The twigs cast no shadow; the caster's leaves carry no shoot bend.
- The flight numbers need a quiet GPU to confirm.

## Step 11b: twigs as wood

Step 11's twigs read well from 3 m out, but inside a shrub and up to about
a metre they were a cage of thick, near-black, dead-straight rods, many
crossing the view and the sky with no leaf on them. Now they are crooked,
tapering wood at a real radius, bark-shaded, branching from the clump's
base through limbs to leafy shoots. Still all derived per clump at run
time (bro `scene_forest_shoot.glsl`); nothing is baked per plant.

### What changed

- **A structure, not spokes.** Each lobe's stem is a crooked polyline (4
  pieces, a quadratic from the clump's base B, which is the parent
  segment's end, out past the lobe's centre). A lobe of three or more
  sprays grows (L+2)/3 limbs (3 pieces each, curved and crooked) off the
  stem, on a Fibonacci lattice of directions, each leaving the stem at
  0.4-0.94 of its length by its direction. A spray's twig leaves the limb
  (or the stem, for small lobes) where it passes nearest the spray's start,
  a cubic along the wood it leaves and into its axis (3 pieces): short
  twigs off a branch, not one spoke a spray from the clump's middle.
- **Crooked and zigzag.** Every joint steps aside by up to 0.2 of its
  piece (`forestCrook`); a spray's axis (4 pieces) and a shoot (4 pieces)
  zigzag between nodes, 0.1 leaf length for alternate phyllotaxis, 0.045
  otherwise, alternating sides. A shoot is moved to leave its axis as
  drawn, and a leaf's node lies on its shoot as drawn. Shorter: 0.35 leaf
  lengths between a shoot's nodes (was 0.6), 0.75 between a spray's shoots
  (was 1.1), so the bare spans are short and inside the crown.
- **Real radii.** The pipe model: a shoot 0.011 → 0.005 leaf lengths base
  to tip, wood carrying k shoots 0.011·sqrt(k); capped at 7 mm
  (`FOREST_TWIG_MAX_RADIUS`). A pixel wide at the least only where that
  is under a pixel (far); near, it is its own width, and it thins away
  from 0.9 m to 0.2 m from the eye with its cover (as the leaves shrink
  away), so no rod crosses the eye inside a crown.
- **Bark-shaded.** Twigs go through mesh.frag's `barkSurface` with the
  specimen's bark word (inUV.y = -2 - word, cover in inUV.x), lit as the
  tubes are, darker toward the ground and as deep in the crown as the
  leaves where they are; this year's shoots tinted green toward the tip.
  They were lit as leaves with no colour, which is why they were black.
- A clump draws 12 + 9·limbs + 7·NB + 4·S pieces (`forestTwigPieces`,
  `ForestNode::twigPieces`).
- broworkshop `lib/kit/sway.js` ports all of it step for step.

### Numbers

- Twig oracle (`motion.js twigs`, grove broadleaf, 62 leaves, petiole
  median 37 mm): at rest 0 mm. Gusty median 1.11, p95 6.65, max 11.63 mm,
  0.15 / 0.34 of the petiole (step 11: 7.4, 21.3, 24.4 mm; 0.63 / 0.77).
  Breeze p95 3.37, max 5.71 mm, 0.075 / 0.17 (step 11: 10.4 mm, 0.38).
- Gusty plant run (grove `plant tree wind=gusty`), step 11 → now: top
  flicker 273 → 232 blocks, events 41.6 → 36.6/s; middle flicker 460 →
  340, 79.2 → 64.2/s; bottom flicker 367 → 302, 66.6 → 50.2/s, and the
  bottom's jitter flag gone. Both still flag FLICKER and EVENTS in every
  region.
- Leaves vs By size per band, unchanged within 0.6 (twigs are near only):
  canopy noon 20-40 113.0/114.3, 40-80 118.6/122.8 (4.1; was 3.9),
  80-160 110.6/113.9; golden canopy 82.2/80.2, 88.4/87.5, 89.8/84.9.
- Flight (1280x720), never on a quiet GPU: another bro-headless ran
  throughout. Back to back, step 11's shaders → these: median 4.37, 4.31
  → 4.44, 4.59 ms; p95 6.07, 6.31 → 5.38, 5.75 (contended, not the
  budget's numbers).
- `validate.sh demos/forest demos/grove`, bro `tests/flora`, broflora's
  tests pass.

### Captures

- Before (step 11): `broworkshop/demos/{forest,grove}/captures/s12pre_*`.
- After: `broworkshop/demos/forest/captures/after12_{broadleaf,shrub}_{noon,golden}_{inside,1m,3m}.png`,
  `broworkshop/demos/grove/captures/after12_{broadleaf,birch,hydrangea,holly}_{noon,golden}_{inside,1m,3m}.png`.
- Motion: `broworkshop/demos/grove/captures/motion/s12{before,after}_plant_tree_gusty_motion*`.
- Metrics: `broworkshop/demos/forest/captures/after12_metrics.json`.

### Still open

- Canopy noon 40-80 m stays 4.1 apart (from step 11's petiole and cover,
  not the twigs).
- The flight on a quiet GPU.
- Some spray twigs in the large grove broadleaf clumps (r 1.6-2.7 m) are
  still long and straight at 1 m.
- Twigs cast no shadow, and the casters' leaves carry no spray or shoot
  bend.

## Step 11c: twigs at their size, matte leaves, far tiers from the shell

After 11b, inside a crown the twigs faded away within 0.9 m of the eye, so
the leaves near the eye hung from nothing; the near leaves shrank away
over the last metre, small ones floating among full-size ones; the broad
leaves carried a white salt of single pixels and a glare; the big grove
broadleaf clumps still showed long straight twigs at 1 m; and the far
tiers were 4 units brighter than the leaves at noon 40-80 m. All of it is
still derived per clump at run time; nothing is baked.

### What changed

- **Twigs at their true size.** The twig fade (`FOREST_TWIG_NEAR`,
  mesh_forest_tube.vert) is now 0.03-0.1 m, was 0.2-0.9 m: a twig a few
  mm thick stays that thick at any distance and only gives way when the
  eye all but touches it. The ribbon is across the curve's tangent at
  each joint, which both pieces share, so the joints are seamless. Twigs
  take half the leaves' crown occlusion (all of it left the wood inside a
  crown near black), and young pale-bark twigs are red-brown
  (0.15, 0.095, 0.065), not near-black.
- **No floating leaves.** A forest leaf shrinks to its middle only within
  0.1-0.4 m of the eye (`FOREST_NEAR_SHRINK`, was scene_vegetation's
  0.25-1 m).
- **Matte leaves, no salt.** mesh_forest_leaf.vert packs teeth plus depth
  and petiole (64ths) plus gloss into one float each. Interpolated across
  a triangle, a whole number came back a hair under itself and `floor()`
  read it one less with a fraction near 1: deep spines tilting the normal
  and full gloss on single pixels. `forestUnpack` nudges by 0.004 before
  the floor (bro a49ac610). Per band unchanged.
- **Shorter, more crooked limbs.** A limb and a spray's twig to it take 5
  pieces each (was 3) and a limb carries 2 sprays (was 3), so the twig
  from a limb to a spray is short and a large lobe's limbs bend every
  third of a metre or less. A clump draws 12 + 15·limbs + 9·NB + 4·S
  pieces (`ForestNode::twigPieces`, broworkshop `lib/kit/sway.js`).
- **Leaf casters bend with their sprays.** shadow_forest_leaf.vert poses
  a broad leaf as scene_forest_leaf_pose.glsl does, its spray's and
  shoot's bending (`forestLeafShootSway`) included, so a leaf's shadow on
  the leaves under it stays with it in a gust. Twigs cast no shadow: a
  few mm across, they are under a shadow texel at every tile (2 cm at the
  nearest), and drawn there they would alias into crawling lines.
- **Far tiers from the shell.** A broad lobe's leaves lie in its shell
  (shoots at 1 - shell·(0.2..0.8) of its radius), not through its ball.
  A clump caster's disc (shadow_forest_sprite.vert) now stands at the
  lobe's centre, not half a radius past it, since everything but the lit
  cap lies behind the near shell's leaves; its cover is the chord through
  the shell (shadow_forest.frag), as many leaves. A cluster's light
  through its lobe crosses the hollow free (`forestShellPath`,
  scene_forest_mass.glsl; `inLobeHollow` from mesh_forest_leaf.vert).
  Before, the sides of the shell and the inner face of its far half went
  unshaded by the clump casters that By size uses from about 25 m.

### Numbers

Baseline `s13base2`: bro 8da81a6c shaders with 11c's piece counts (the
binary's), run with the current forest `capture.js`. (Its framing changed
since step 11b's runs: compare runs of the same day only.)

- One broadleaf, noon, `tiers` Leaves vs By size: 30 m 126.2 / 131.3 →
  127.4 (with the disc at the centre), 50 m 125.8 / 130.2 → 126.8, 70 m
  125.3 / 129.6 → 127.1 (clusters, density through the shell). Without
  shadows both read 134.3: the gap was the casters.
- Leaves / By size per band, before → after (`s13base2` → `s13w`):

| shot, sky | band | before | after |
| --- | --- | --- | --- |
| canopy, noon | 20-40 | 112.2/110.5 (-1.7) | 112.2/108.8 (-3.4) |
| canopy, noon | 40-80 | 115.5/118.4 (+2.9) | 115.6/117.4 (+1.8) |
| canopy, noon | 80-160 | 109.3/114.3 (+5.0) | 109.4/114.1 (+4.7) |
| skim, noon | 10-20 | 123.8/129.0 (+5.3) | 124.6/128.1 (+3.4) |
| skim, noon | 20-40 | 145.9/149.6 (+3.7) | 146.3/149.2 (+2.9) |
| glade, noon | 20-40 | 97.6/101.3 (+3.7) | 97.6/100.5 (+2.9) |
| glade, noon | 40-80 | 122.8/121.6 (-1.2) | 122.8/121.2 (-1.6) |
| trunks, noon | 10-20 | 70.0/77.8 (+7.8) | 70.5/77.5 (+7.0) |
| canopy, golden | 20-40 | 82.2/79.4 (-2.8) | 82.3/78.6 (-3.8) |
| canopy, golden | 40-80 | 87.8/87.4 (-0.4) | 87.8/86.7 (-1.2) |
| canopy, golden | 80-160 | 89.5/87.5 (-2.0) | 89.5/87.3 (-2.3) |
| skim, golden | 10-20 | 106.0/107.3 (+1.3) | 106.7/106.7 (0.0) |
| trunks, golden | 10-20 | 58.2/70.8 (+12.6) | 58.8/70.0 (+11.2) |

  The other bands are within 0.5. Canopy 20-40 m is the one band further
  apart: there By size keeps most leaves but casts partly as clumps, and
  the centred disc shades them as the shell's back. Run to run the
  Leaves column moves up to 0.7 (golden canopy 20-40).
  A disc a quarter radius on (`s13I`) gave canopy noon 20-40 -2.9 and
  40-80 +2.2; clusters at the lobe's full density through the shell
  (`s13b`) gave 40-80 +0.8 but 20-40 -5.6.
- Twig oracle (`motion.js twigs`): unchanged, gusty p95 6.65 mm, max
  11.63 (0.15 / 0.34 of the petiole); breeze p95 3.37, max 5.71.
- Gusty plant run (grove `plant tree wind=gusty`), before → after: top
  flicker 225 → 178 blocks, events 37.2 → 36.2/s; middle 349 → 285,
  73.2 → 41.0/s; bottom 290 → 271, 49.8 → 41.6/s (the casters' leaves now
  move with the leaves they shade).
- Flight (1280x720), back to back, base → mine, twice; another
  bro-headless ran throughout (not a quiet GPU): p95 6.57 → 5.66, 10.90 →
  5.98 ms; median 4.59 → 4.59, 4.80 → 4.70; max 13.0 → 11.6, 14.1 →
  10.0. Mine is no slower than the baseline in either pair.
- `validate.sh demos/forest demos/grove`, bro `tests/flora`, broflora's
  tests pass.

### Captures

- Before (8da81a6c shaders) and after, full frame, inside, 1 m and 3 m,
  noon and golden:
  `broworkshop/demos/forest/captures/{before13,after13}_{broadleaf,shrub}_{noon,golden}_{inside,1m,3m}.png`,
  `broworkshop/demos/grove/captures/{before13,after13}_{broadleaf,birch,hydrangea,holly}_{noon,golden}_{inside,1m,3m}.png`.
- Motion: `broworkshop/demos/grove/captures/motion/s13{base2,work}_twigs_{gusty,breeze}.txt`,
  `s13{base2,work}_plant_tree_gusty_motion*`.
- Metrics: `broworkshop/demos/forest/captures/{s13base2,s13w}_metrics.json`;
  traces `{s13fb,s13fw,s13fb2,s13fw2}_trace.json`.

### Still open

- Inside a crown the twigs a few centimetres from the eye are as thick
  as they are, so they cross the view as dark rods against the sky; their
  polylines read as straight pieces with kinks at that range.
- One long limb still crosses the grove broadleaf's 1 m view.
- Canopy noon 80-160 m is +4.7 (crowns, not clumps), canopy 20-40 m -3.4.
- The flight on a quiet GPU.

## Step 12: far meadow

From about 30 m to the horizon the meadow was pale yellow-green, washed
out and leafy (clover-like), with dark soil-like mottles, and its flowers
thinned to dots and vanished past the herb plants (72 m). Now it keeps the
near look's tone out to 150 m, its far leaves read as grass, the wind's
sheen and gusts travel across it, and the flowers go on to the horizon as
specks of their colour, patch by patch. Everything is derived at run time
from the herb records and shared noise (bro `scene_grass.glsl`,
`scene_grass_surface.glsl`, `scene_ground.glsl`, `scene_herb_mix.glsl`).

### What changed

- **Lit as a sward.** Far off (2 to 14 m in) a blade's shading normal
  becomes the sward's mean face: the way to the eye from its root bent up
  toward the ground (`grassCanopyNormal`). The turf takes the same normal
  where it stands for the blades. Lit as level ground, a far meadow under
  a high sun was a quarter brighter than its blades' faces.
- **As dark as the near blades on average.** A far blade takes the mean
  of its root-to-tip shade (`grassMeanShade`), not full brightness, and
  the turf's sward colour takes the same. Far blades keep a quarter of a
  near blade's glow (`b.far` 0.25), and the turf as many (0.3 x cover), so
  golden light is not lost with distance. Far blades are rough
  (0.9 past 40 m), so the eye-facing normal does not mirror a sun behind
  the eye.
- **Fans, not broad leaves.** A non-tufted live leaf in the outer rings is
  a fan card cut per pixel into four tapering strands
  (`grassFanSurface`, box-filtered), so 20-150 m reads as fine grass,
  not clover. Ring 3 has more cover than its 9 x 10 cm: 5 blades in a
  0.7 m cell, x50 wide (10 x 20 cm). The 55 m step is now under 1%; it
  was -3 to -7% once the gaps read right.
- **The gaps are grass.** Between drawn blades past the near ground the
  turf is the sward seen deeper in (0.75 of its tops, `GRASS_TURF_DEPTH`),
  mottled at 0.35 and not sun-bleached. It was soil and pale patches, which
  is what read as dark mottles and as a sun-dried ground. Far thin clumps
  fill in (`cover` toward density x 1.4).
- **Wind at distance.** A comb (streaks along the wind, tens of metres by
  a few) and each travelling gust tip the far blades' and the turf's
  normal downwind (`grassCombLean`; 0.3 and 0.6). A gust also lifts the
  turf's sheen as it does a blade's. Both are scaled by the steady wind
  (`grassGustStir`: none in still air, full from half the reference
  speed).
- **Flowers past the herb ring.**
  - `herbBloomLocal` weighs each blooming species by its colonies at the
    point (the same noise `herbPick` uses for the plants) against its mean
    over them. A stretch of poppies reads red, one of daisies white, and
    over the land the mix is `herbMixMean`'s.
  - Past the plants, a far fan stands for a herb as often as the herbs'
    cover seen from there has them: as a bloom of the species picked by
    hash among the local weights, or as the leaves' mean. The turf between
    and past the fans does the same.
  - Most herb leaves sit below the grass top, and some blooms do, so only
    0.1 of the leaves and 0.5 of the blooms show (`GRASS_HERB_LEAF_SEEN`,
    `GRASS_HERB_BLOOM_SEEN`). These are set so the stand-ins match what the
    drawn plants show at the 72 m hand-over.
  - `GrassNode::computeHerbMean` counts a species' side silhouette only as
    far as its leaf top and bloom height rise over the grass (0.7 x
    height).
- **Seed-head haze.** Once a head's spikelets are under a pixel it takes
  their mean with the anthers. Seen across (`graze`, from the view's
  elevation) it shifts toward a pale pinkish-buff (`grassHeadFarColor`).
  The turf's flowering does the same. With `flowering` set, a meadow seen
  across has a buff haze in its flowering patches.
- broworkshop `demos/forest/meadow.js` holds the measures `meadow`,
  `meadowbands` and `meadowtone`; `motion.js meadow` holds the far-meadow
  motion run with its travel measure.

### Numbers

**Tone by distance, fixed 8° view, herbs off (`meadowtone`).** Luma against
3 m at 10/30/60/100/150 m:

| Sky | Before luma | After luma | Before sat | After sat |
|---|---|---|---|---|
| Noon | +11/+17/+20/+21/+22% | +5.5/+2.1/+0.5/+2.0/+5.2% | -9 to -23% | -8.6/+0.9/+5.8/-1.2/-11.6% |
| Golden | +6 to +2% | +4.6/+1.5/-1.9/-2.3/+1.3% | about -20% | +1.4 to +7.3, -6.8 at 150 m |

**Standing eye at 1.7 m, herbs on (`meadowbands`).** Against 2-4 m, for the
32-64, 64-100 and 100-150 m bands:

| Sky | Measure | Before | After |
|---|---|---|---|
| Noon | Luma | +16/+24/+27% | +4.5/-1.3/+1.8% |
| Noon | Sat | +8/-3/-5% | +28/+36/+33% |
| Golden | Luma | +28/+36/+39% | +38/+42/+45% |

At noon the luma now matches. The saturation rises because, seen across, the
grass hides the soil. The golden luma is worse; see Still open.

**Ring and hand-over steps (luma / sat), with range x1.3 over the same
ground:**

| Edge | Noon before | Noon after | Golden before | Golden after |
|---|---|---|---|---|
| 7 m | -0.5/+0.5 | -0.4/+0.4 | +0.1/-0.1 | +0.5/-0.1 |
| 20 m | -0.2/+0.9 | -0.6/+1.4 | -0.2/+0.3 | -0.7/+0.7 |
| 55 m | +1.0/-2.7, contrast -20% | -0.2/-0.9, contrast about -10% | +0.1/-1.6 | -0.5/-0.9 |
| 72 m (herbs) | +4.8/-5.6, contrast -40% | +0.9/-1.9, contrast -15% | +1.6/-6.5, contrast -33% | +1.1/-1.1, contrast -11% |
| 150 m | +0.6/-9.5 | +4.1/-8.6 | +0.8/-6.5 | -0.2/-5.3 |

**Motion, gusty far meadow (`motion.js meadow`).** The eye is 8 m up, looking
across the wind. Change in time and travel along each slit:

| Slit | Change before (levels rms) | Change after (levels rms) | Travel before | Travel after |
|---|---|---|---|---|
| 30 m | 6.70 | 9.08 | -9.7 m/s | -9.7 m/s |
| 60 m | 5.75 | 8.59 | -9.1 m/s | -6.5 m/s |
| 100 m | 3.56 | 8.89 | -6.5 m/s | -6.5 m/s |
| 150 m | 1.11 | 2.82 | 0 (nothing travelled) | -3.2 m/s |

- Gusts now travel as waves out to 150 m.
- Best-match correlation 0.80 to 0.91.
- Flicker p90 at most 0.37 levels.
- No noise-like, jittery or flickering blocks.
- Two single-block flashes in the near region.

**Flight trace (1280x720, no other bro-headless running, interleaved runs).**
On this machine HEAD (bro 35b32359, shaders and ring spec) runs median 4.43
and 4.42 ms, p95 5.26 and 5.22. That is over the step 11 budget; the
machine is about 0.25 ms slower than when step 11 measured 4.19 / 5.01.
With step 12 it runs median 4.51 and 4.54, p95 5.40 and 5.42:

- median +0.10 ms;
- p95 +0.17 ms;
- ring 3's extra cover is most of the added cost.

**Tests.** `validate.sh demos/forest demos/grove`, bro `tests/run_tests.sh
grass` and `flora`, and `bro_vulkan_scene_test` pass.

### Captures

All under `broworkshop/demos/forest/captures/`:

- Before: `s12a_meadow_{noon,golden}_{0.5m,3m,30m,60m,150m}.jpg`,
  `s12a_{noon,golden}_{3,10,30,60,100,150}m.jpg` (the tone views),
  `s12a_meadowbands.json`, `s12a_h_meadowbands.json`,
  `s12a_meadowtone.json`.
- After: `s12b_meadow_{noon,golden}_{0.5m,3m,30m,60m,150m,skim60,skim150}.jpg`,
  `s12b_{noon,golden}_*m.jpg`, `s12b_meadowtone.json`,
  `s12c_meadowbands.json`. The haze is in
  `s12b_flowering_meadow_golden_*.jpg` (flowering 0.3).
- Motion: `motion/s12_before/forest_meadow_gusty_motion*` and
  `motion/forest_meadow_gusty_motion*`.

### Still open

- **Golden from a standing eye.** The ground brightens with distance (+38 to
  +45% over the dark 2-4 m band, against +28 to +39% before). At a fixed
  angle it matches, so this is the view angle: the far glow and grazing
  faces. It wants a grazing-angle reference rather than a lower glow.
- **The 150 m edge.** The turf past the last ring is 4% brighter and 9% less
  saturated at noon than the fans there. Darkening the turf's tops moved it
  little, so haze and lighting dominate there, not the turf's colour.
- **White blooms far off.** They show as short dashes (a fan strand's
  shape), not dots.
- **The flight budget.** Step 12 adds 0.1 ms median. The budget numbers need
  a re-baseline on this machine, or ring 3 needs a cheaper path.

## Step 12b: the far meadow lit as a sward

Past 30 m the meadow read as a flat yellow-green carpet, speckled pixel by
pixel, and at golden hour it went acid-yellow and brightened with distance
(+38 to +45% from a standing eye). The cause was the grazing-angle
response: a far blade and the turf were lit as one face turned to the eye,
which a low sun lights fully, plus a near blade's art translucency (about
four times a blade's front reflection). Now the far sward takes the light
as its blades do on average (bro `scene_lighting.glsl` `sceneSwardLight`,
`scene_grass.glsl`, `scene_ground.glsl`, `scene_grass_surface.glsl`,
`mesh.frag`), all derived at run time.

### What changed

- **The sun on a sward is its blades' mean** (`sceneSwardLight`,
  `SceneSurface::sward`: the way the blades stand and the weight of this
  lighting). A blade takes it from 2 to 12 m, the turf where it stands for
  blades, and a herb's leaves and stems from 8 to 30 m (its blooms keep
  their own light).
  - Eight blades, at two tilts (0.35 and 1.0 rad from upright) and four
    ways round each, weighted by how much of each the eye sees.
  - A face turned to the light reflects by Lambert. A face turned away lets
    through the light on its other side: a thin leaf transmits about as
    much as it reflects in the green (PROSPECT's R ~ T), less blue
    (`SWARD_TRANSMIT` 0.95/1.0/0.45).
  - Times the share of the seen blades the light reaches: a turbid canopy
    (G 0.6) along both lines, with Kuusk's hot spot (width 0.12). Seen
    across, the eye meets blade sides lit at a slant and the undersides of
    those leaning away, so a far meadow under a high sun is darker than
    level ground; under a low sun behind the eye it sees lit faces, looking
    toward the sun shaded sides with a glow through them.
  - Plus each blade's cuticle sheen (Blinn 40, Schlick's Fresnel).
  - The sky's light reaches the sward through its opening: irradiance
    about the blades' way, not the eye-facing facet (which faced the
    bright horizon and lit the far golden meadow by itself).
- **The wind combs the sward one way.** The lay is only downwind (0.5 to
  1.5 of `GRASS_COMB` 0.6 along the streaks, plus `GRASS_GUST_LEAN` 2.4
  times the gust) and gathers the blades' leans that way (`SWARD_COMB`
  0.7), so a gust running across the view turns the faces the eye sees
  between those lit and those in shade. It starts where the sward's light
  takes over (2-12 m), not at 10-40 m.
- **Tussocks, not pixels.** Once a blade is under a few pixels wide its own
  tone and hue fade into its tussock's (0.42 m cells): many blades share a
  pixel and their own values average out.
- **Blooms as dots.** A far herb's bloom is a round head over its fan's top
  (`grassFanBloom`, at least about a pixel across, cut per pixel), its
  colour packed in one varying (`grassPackColor`), on proportionally more
  fans (`GRASS_FAN_BLOOM_AREA` 0.8). The turf's blooms are 0.6 of before to
  match.
- **The turf matches the far fans.** Its sward colour takes the blades'
  mean hue spread and sheaths (`GRASS_BLADE_MEAN_TINT` 1.03/1.025/0.88) and
  its tops 0.85 (`GRASS_TURF_TOPS`).
- **Ring 3** is 4 blades a 0.7 m cell at 62 x the width (was 5 at 50): the
  same cover, a fifth fewer vertices.
- **Test.** bro `tests/scene/test_grass_floor_turf.js` lights its scene at
  8 (it sat at 6-10 of 255) and lets the litter be up to 1.6 x the sward
  seen from above, which now shows its blades' shaded depths.

### Numbers

**Standing eye at 1.7 m, herbs on (`meadowbands`).** Against 2-4 m, for the
32-64, 64-100 and 100-150 m bands:

| Sky | Measure | Before (step 12) | After |
|---|---|---|---|
| Noon | Luma | +4.5/-1.3/+1.8% | -3.5/-1.1/+0.6% |
| Noon | Sat | +28/+36/+33% | +39/+40/+36% |
| Golden | Luma | +38/+42/+45% | +16/+13/+15% |
| Golden | Sun behind | +44/+44/+43% | +26/+24/+24% |
| Golden | Sun ahead | +30/+38/+44% | +2/+8/+10% |

The 4-32 m bands at golden sit at +12 to +18% (the 2-4 m band looks down
into the sward), so past 32 m the golden meadow is within a few percent of
the nearer ground. With the sun behind the eye it is brighter, which is
the hot spot.

**Ring and hand-over steps (luma / sat), range x1.3 over the same ground:**

| Edge | Noon before | Noon after | Golden before | Golden after |
|---|---|---|---|---|
| 55 m | -0.2/-0.9 | +0.6/-0.2 | -0.5/-0.9 | +0.2/+0.1 |
| 72 m (herbs) | +0.9/-1.9 | +2.1/-1.9 | +1.1/-1.1 | +0.3/-0.7 |
| 150 m | +4.1/-8.6 | +1.0/-6.1 | -0.2/-5.3 | +1.3/-4.7 |

**Tone by distance, fixed 8° view, herbs on (`meadowtone`).** Luma against
3 m at 10/30/60/100/150 m:

| Sky | Before | After |
|---|---|---|
| Noon | +4.0/+0.6/+1.9/+5.1/+9.1% | -1.9/-8.5/-7.0/-4.7/-1.8% |
| Golden | +3.9/+0.9/-0.2/+1.0/+6.4% | -9.2/-19.2/-18.4/-16.2/-10.4% |

At one angle the far sward is now darker than the near blades. The near
blades (step 10) keep their art translucency, about four times a blade's
front reflection; the far ones take the physical mean. The standing eye
shows no step (the hand-over runs from 2 to 12 m), but see Still open.

**Motion, gusty far meadow (`motion.js meadow`).** Change in time (levels
rms), travel and best-match correlation per slit:

| Slit | Before | After |
|---|---|---|
| 30 m | 9.08, -9.7 m/s, 0.86 | 6.07, -1.3 m/s, 0.69 |
| 60 m | 8.59, -6.5 m/s, 0.80 | 7.23, -3.9 m/s, 0.72 |
| 100 m | 8.89, -6.5 m/s, 0.91 | 6.22, -4.3 m/s, 0.85 |
| 150 m | 2.82, -3.2 m/s, 0.84 | 2.86, -3.2 m/s, 0.78 |

Waves still travel at every slit (the slit image shows diagonal bands at
30-100 m), but the waves are weaker, and at 30 m travel is borderline
(between -0.6 and -7 m/s from run to run as the comb was tuned). No
noise-like, jittery or flickering blocks; flicker p90 0.27 levels; no
flashes.

**Flight trace (1280x720, no other bro-headless, base and new back to
back).** HEAD is now bro 5957cbc6 (with step 11c). It runs median 4.48 to
4.67 ms, p95 5.37 to 5.50, max 6.1 to 8.5. That is over the budget
(median 4.4, p95 5.1, max 6.0) before any grass change. Per pass, median
frame:

| | Grass | Ground | Forest | Shadow |
|---|---|---|---|---|
| Base | 0.37 | 0.15 | 3.33 | 0.45 |
| New | 0.33 | 0.19 | 3.14 (noise) | 0.45 |

Ring 3 at 4 blades saves 0.04 ms. The turf's sward light costs 0.04 ms.
Grass plus ground is unchanged at 0.52 ms. With the earlier 5-blade ring 3
the whole flight ran +0.02 to +0.15 ms median.

**The max frame** (7.5-8.5 ms at t≈26-28 s, under the canopy) is not
grass. Grass is under 0.2 ms of it. It is the forest's near leaves:
`forest:leafNear:needle` 1.7 ms, `forest:leafCard:needle` 1.3 ms,
`forest:leafNear:broad` 0.5 ms, clumps 0.7 ms (`BRO_SCENE_GPU_PASSES=6`).

**Tests.** `validate.sh demos/forest demos/grove`, bro `tests/run_tests.sh
grass` and `bro_vulkan_scene_test` pass. In `flora`, `test_forest.js`
("close up: every leaf, full bark") fails on HEAD 5957cbc6 with HEAD's
shaders as well; it is not this step.

### Captures

All under `broworkshop/demos/forest/captures/`:

- Before: `s12b_meadow_{noon,golden}_{0.5m,3m,30m,60m,150m,skim60,skim150}.jpg`,
  `s12b_understory_*.jpg`, `s12e0_{noon,golden}_*.jpg` (tone views and
  standing views), `s12e0_meadowbands.json`, `s12e0_meadowtone.json`.
- After: `s12f_meadow_{noon,golden}_{0.5m,3m,30m,60m,150m,skim60,skim150}.jpg`,
  `s12f_understory_{noon,golden}.jpg`, `s12f_{noon,golden}_view{0,2}.jpg`
  (standing eye, the woods' edge), `s12f_meadowbands.json`,
  `s12f_meadowtone.json`. The golden s12f stills and the after motion run
  were drawn with HEAD's ring 3 (5 blades), the rest with 4.
- Motion: `motion/s12e_before/` and `motion/s12f_after/`
  (`forest_meadow_gusty_motion*`, the slit image shows the waves).

### Still open

- **Near against far at one angle.** At a fixed 8° the far sward is 7-9%
  darker at noon and 16-19% at golden than the 3 m blades. The near
  blades' translucency is an art value about four times physical. Bring
  the near blades onto the same light (sward weight below 1 with their own
  normals) and recalibrate the near look against it.
- **Gust waves at 30 m.** They are weaker than step 12's facet tilt gave.
  The sward's mean barely changes when blades tilt across the view under a
  high sun; the comb's gathering carries most of it. A stronger cue may be
  the blades' paler undersides as they bend.
- **The 150 m edge.** The turf past the last ring is still 5-6% less
  saturated, with +3 to +4 levels of blue. The turf's colour tint barely
  moves it, so it is in the turf's sky light or roots.
- **Golden, sun behind.** It is +24% past 32 m, the hot spot. Real fields
  do this, but its width (0.12) is a guess.
- **The budget** needs the forest's near leaves, not grass.

## Step 13: flight in budget, round near wood, foxglove bells

After 11c the flight at 1280x720 (4x MSAA) on a quiet GPU missed its
budget (max ≤ 6.0, p95 ≤ 5.1, median ≤ 4.4 ms): max 7.3-8.4, p95
5.3-5.5, median 4.4-4.5. Inside a crown the branches within 1-2 m showed
faceted sides, flat-shaded bands and offsets at their joints, and twigs a
few centimetres from the eye crossed the sky as thick dark rods. The
foxglove's bells were plain pink cones. All of it is still derived at run
time; nothing is baked.

### What changed

- **Wood drawn indexed.** Bark and twig draws are `vkCmdDrawIndexed` over
  a fixed index pattern per side count (`woodIndices`,
  scene_mesh_drawer_forest.cpp; ForestTube in scene_mesh_drawer.cpp). A
  bark segment shades its two rings' 2(N+1) points, was 6N, and a twig
  piece its 4 corners, was 6 (`forestTubeVertexMorph`,
  mesh_forest_tube.vert decode the indexed numbering).
- **Round near bark, seamless joints.** A tree whose trunk is wider than
  `barkFineSides` (2.5, a new Lod field) times `barkSidesPixels` draws
  20 sides (`forest:bark20` groups), else 10 or 5 as before. Each ring
  geomorphs by its own width on screen: 5 → 10 over 1-2 x
  barkSidesPixels, 10 → 20 over 2.5-5 x, a point on the coarser level's
  chord (`forestTubeMorph`, scene_forest_tube.glsl). So a ring of a given
  width has one outline whichever side count its tree draws, and neither
  the tree's switch nor the narrowing toward the twigs shows a step.
  `barkSurface` renormalises its ring coordinates, so the bark pattern is
  round, not banded per facet.
- **Branch ends narrow.** ForestNode's segment records end each segment
  at the thickest radius going on from its end, or 0.3 x itself at a tip
  (`kTipTaper`), so a limb steps down into its children rather than
  ending in a flat cut wider than what leaves it.
- **Twigs as small cylinders.** The twig ribbon sends only its normal's
  sideways part; mesh.frag rounds it out toward the eye into a cylinder's
  normal once the twig is a few pixels wide. Thinner, it keeps the
  ribbon's two faces: lit toward the eye, a twig of a pixel or two stood
  out against the leaves and the gusty plant run's middle region flickered
  more (295 blocks against 280). The bark coordinates run on along a chain
  of pieces (`alongM`, an angle per clump), so the bark does not jump at a
  joint. Within 5-18 cm of the eye a twig slims to 0.45 of its radius
  (`FOREST_TWIG_SLIM_AT`), and it gives way within 3-8 cm
  (`FOREST_TWIG_NEAR`, was 3-10). The rod a few centimetres from the eye
  is now a thin round twig.
- **Foxglove bells** (`herbFoxgloveBell`, scene_herb_surface.glsl), cut per
  pixel on the raceme card:
  - Each bell is a lipped tube that flares to a longer lower lip, round
    shaded from above with faint ridges and green sepals at its base.
  - The throat is a paler cream with dark spots ringed white, under a dark
    hollow beneath the upper lip.
  - Most bells hang to one side (14% to the other), opening down and out
    and foreshortened as they turn.
  - They grade smaller up the spike into paler, then green, buds.
  - The raceme card is wider for a foxglove (4.2 x the bell, shifted 0.15,
    scene_herb.glsl).
  - The bluebell (secund) path is unchanged.

### Numbers

- Flight, quiet GPU (no other bro-headless at any point), back to back,
  base (`binbase`, 5957cbc6 shaders, `barkFineSides` 0) against step 13:

  | run | max | p95 | median |
  | --- | --- | --- | --- |
  | base `qb5` | 7.31 | 5.32 | 4.44 |
  | step 13 `qm5` | 5.96 | 5.00 | 3.98 |
  | base `qb6` | 8.40 | 5.45 | 4.51 |
  | step 13 `qm6` | 5.86 | 4.92 | 3.98 |

  The step 13 runs predate one change: limiting the twigs' rounding to
  twigs a few pixels wide, a fragment-only change. All three are within
  budget in both runs. Base's slow frames are the
  0.3-0.7 s runs at t ≈ 26-29 s (eye 2-6 m among the leaves). In the
  step 13 runs these stay under 6.0 ms. Before this, two runs with
  another agent's bro-headless running read p95 11.8 and max 17.7 ms: an
  unquiet GPU's numbers are not the flight's.
- Per band (`capture.js metrics`, `s14base` → `s14w`): every band within
  0.2 (for example, canopy noon 20-40 m 112.2/111.0 → 112.1/111.0).
- Handover strips, base against step 13, PSNR: leaves 43.5, cards 42.4,
  clumps 54.8, crowns 76.6 dB. The steps at 28.6 m and 264 m are as
  they were.
- Twig oracle (`motion.js twigs`): unchanged, gusty p95 6.65 mm, max
  11.63; breeze p95 3.37, max 5.71.
- Gusty plant run (grove `plant tree wind=gusty`), flickering blocks and
  events (pop + flash + kick) per region, top / middle / bottom:

  | run | flickering blocks | events |
  | --- | --- | --- |
  | base (two runs) | 171-177 / 280-284 / 256-257 | 167-169 / 187-188 / 207-218 |
  | step 13 | 172 / 276 / 266 | 161 / 183 / 180 |

  Events are down in every region and flicker p90 levels are within 0.06.
  With the twigs rounded at every width the middle region read 295-299.
- `validate.sh demos/forest demos/grove` and bro `tests/flora` pass
  (test_forest expects 20 sides close up and checks that the sides step
  20, 10, 5 by width).

### Captures

- Before (5957cbc6 shaders, the 11c binary) and after, full frame, inside,
  1 m and 3 m, noon and golden:
  `broworkshop/demos/forest/captures/{before14,after14}_{broadleaf,shrub}_{noon,golden}_{inside,1m,3m}.png`,
  `broworkshop/demos/grove/captures/{before14,after14}_{broadleaf,hydrangea}_{noon,golden}_{inside,1m,3m}.png`.
- Foxglove close-up (0.6 / 1.5 / 4 m):
  `broworkshop/demos/grove/captures/{before14,after14}_foxglove_{noon,golden}.jpg`.
- Metrics: `broworkshop/demos/forest/captures/{s14base,s14w}_metrics.json`.
  Handover strips: `{s14base,s14w}_handover_{leaves,cards,clumps,crowns}_strip.png`.
  Traces: `{qb5,qm5,qb6,qm6}_trace.json`.
- Motion: `broworkshop/demos/grove/captures/motion/{s14base,s14base2,s14w,s14var7}_plant_tree_gusty_motion*`,
  `{s14base,s14w}_twigs_{gusty,breeze}.txt`.

### Still open

- Twig joints still read as polyline kinks at about 1 m: the wood is now
  round, but the path is straight between nodes.
- Thin twigs at 3-10 m have a stipple of single pixels along them, from
  bark detail under a pixel across.
- Canopy By size against Leaves: 20-40 m -1.1 (noon), 80-160 m +3.1.
- The shadow cascade's end at about 190 m has no fade. That is
  scene_lighting.glsl, which step 12b was editing; it is left for a pass
  of its own.
- The p95 margin is 0.1-0.2 ms. The near leaves (needle leaves and cards
  at t ≈ 26-28 s) are the next place to look.

## Step 14: near wood as curves, the cascade hand-over

Bro 05dadd4f, 1f0e2469; broflora d1d3699; broworkshop df790b9, eccc38a,
37b5e6d. Everything is still derived at run time.

### Near twigs and wood (bro 05dadd4f)

- **Curves through the joints.** `forestHermite` (scene_forest.glsl, pull
  0.8 of the chord; 0.6 still kinked, 1.0 hooked) carries the bark tubes
  and the shoot model. A bark segment of a tree's thickest wood (radius
  6 px or more, the `Bark20Curved` / `Bark10Curved` prefix) draws 3 or 2
  rows on the curve through its ends along the bark's way there; the rest
  stays one straight row. A twig clump of radius 120 px or more draws its
  pieces in 3 rows (`TwigsFine`).
- **Leaving the parent.** A side branch, and a shoot off its axis, leaves
  along the bisector of the parent's way and its own (broflora
  `forest.cpp` `tangentFrom`, sway.js the same). A shoot's base is a collar
  0.5 of its width over 3 radii. The zigzag is capped at 0.12 of a piece:
  wider, it put kinks back.
- **Ends.** A tip's end radius is stored negative (`ForestNode::gpuData`),
  and the tube closes to a rounded point (`sqrt(1 - f^4)`). Twig chains end
  at 0.25 of their radius. Thick tips now read a little "pencil"-like at
  1 m.
- **Stipple at 3-10 m.** The cause was the twig's two-faced ribbon normal,
  which lit and darkened single pixels along it. A twig's normal is now
  round toward the eye at every width. A drawn-width floor and bark cover
  had no measured effect and cost fragments, so they were dropped. Static
  breeze subject flicker (p90 / flickering blocks): 5 m 2.70 / 1671 →
  2.00 / 608, 10 m 3.09 / 1833 → 2.37 / 1224.
- **Twigs oracle** (grove `motion.js twigs`, now modelling 3 drawn rows):

  | wind | p95 / max mm | over the petiole p95 / max |
  |---|---|---|
  | gusty | 12.70 / 24.11 → 2.56 / 3.08 | 0.294 / 0.670 → 0.074 / 0.108 |
  | breeze | 6.50 / 12.45 → 2.41 / 2.51 | 0.150 / 0.343 → 0.071 / 0.095 |

  At rest the median is 1.2 mm (the rows' chords off the curve).
- **Cost.**
  - Trunks shot: 18.3 → 23.5 M vertices. Curving every segment was 52 M,
    over the test's 30 M.
  - Quiet-GPU flight, interleaved against HEAD: median 4.07 → 4.24 ms,
    p95 5.16 → 5.24. Fine twigs +0.13, curved bark +0.09.
  - Draw groups keep their old order ranks, so clumps and crowns still
    draw before static.
- **Grove gusty plant run, HEAD → this** (flicker p90, pops):
  - top: 2.13 → 2.05, 50 → 51 pops;
  - middle: 2.80 → 2.82, 148 → 202 pops;
  - bottom: 2.97 → 2.87, 170 → 147 pops.

  The middle's extra pops are the fine twigs (174 without them). Ablating
  the tip, the zigzag, the bark and the Hermite did not account for the
  rest.
- **Captures.** `demos/forest/captures/after15_{broadleaf,shrub}_{noon,golden}_{inside,1m,3m,5m,8m}.png`.
  These were taken before the 3-row / 120 px settings; recapture for a
  final set.

### The shadow cascade (bro 1f0e2469)

- **No end at 190 m.** With 3 cascades, λ 0.75 and far 2000, the splits
  are at 169 and 388 m. The last cascade reaches past 800 m, so shadows do
  not end in any view. A coverage-coded shader showed no uncovered pixels
  at a 3°, 8° or 45° sun, or at noon.
- **The real step.** Moving the split (`lambda=0.6`) isolates what the
  cascade does. Far crowns take more of each other's shade from finer
  texels: across the meadow at noon they were about 2 levels darker just
  inside 169 m than just past it, a step made within the old 25 m (15%)
  blend. Elsewhere the two hand-overs differ by at most 0.3 levels (noon,
  golden, 8°).
- **The change.** A cascade hands over across the last 45% of its depth
  on a smoothstep, where the next tile's window covers the point. Meadow
  noon crown darkening at 124/132/140/148 m went from 3.28/4.06/3.11/3.45
  to 1.87/2.44/2.01/2.29, running smoothly on to 2.33 at 156 m. That
  matches a single cascade drawing the whole range.
- **Cost.** Flight median +0.04 ms, p95 unchanged.
- **Tooling.**
  - `capture.js shadowbands [shots] [label] [skies] img=1`: darkening by
    distance, crowns and ground apart, plus a darkening image.
  - `sun=<deg>`, `lambda=` and `sunshadow=0` work on any command.
  - Captures: `{c0,b45}_*_shadow.jpg`, `{c0,g0,c0L6,g0L6,b45}_shadowbands.json`,
    and overviews `casc_{c1,n2}_{noon,golden,sun8}_{meadow,canopy,above}.png`.

### Still open

- **Hand-overs at 28.6 m (card) and 264 m (crowns).**
  - Dolly 12-420 m, still: card step -0.50 (x2.6), crowns -0.54 (x3.0),
    and widening > 1.05 at 18.8 m -0.63 (x2.7). These are near the
    measure's noise.
  - The crowns step is a value mismatch ramped over the blend: at 260 m
    `tiers` gives clumps 134.5 and crowns 128.9. Matching the crowns' value
    to the clumps' is the fix, not a wider band.
- **A real pop.** `motion.js dolly shots=` showed that the near-to-card
  switches at 30-31 m are a whole nearby crown going to cards in one
  frame. The forest's `detail` jumped 2.35 → 2.83 → 3.29 in two frames.
  - Cause: `settleDetail` caps its change per call (6% up, 2% down), and a
    headless frame calls it about 6 times, so the cap was 20% a frame.
    Every tree within the ±6% card band switched at once.
  - Fix: tie the rate to the scene clock (`windTime`, 6% / 2% a 30th of a
    second; a paused clock with a moving eye at 60 Hz).
  - That change is in bro's working tree, **uncommitted and unverified**
    (`scene_mesh_drawer.h` `ForestDetail::time`, `settleDetail`). With it,
    the full dolly's 31 m pop is gone and one appears at 59 m (z 10.7, in
    the leaves-to-clumps band). It still needs a trace and the validate
    run.
- **Canopy By size against Leaves at 80-160 m (+3.1).** Not worked this
  step. The shadows are not the cause: the hand-over moved crown
  darkening by about 1 level at 120-150 m, and only in the meadow view.
- **Perf headroom.** p95 is 5.2-5.3 ms after this step (+0.07 for the
  twigs). The slowest baseline frames are `leafNear:needle` at about
  1.4 ms under the canopy at t ≈ 26-28 s. `forestPose` spikes of
  2.4-2.9 ms appear in both base and new.

## Step 15: herbs and shrubs reviewed, their motion, near blades physical

Every herb preset (bracken, lady fern, daisy, poppy, foxglove, bluebell,
yarrow, nettle, plantain, clover, wood sorrel) and shrub (boxwood, holly,
hydrangea, hibiscus, lavender) was looked at in the grove at 0.5, 1.5, 4 and
15 m (the shrubs at 2.5, 6, 20, 60 m), noon and golden. Bracken, lady fern,
poppy, plantain, clover, wood sorrel, holly, hibiscus, boxwood and hydrangea
read right; the foxglove, nettle, bluebell, yarrow, daisy (from behind) and
lavender were fixed, structure first. Everything is still derived per plant
at run time.

### Structure and surface (bro f46459be, broflora 325ff5f)

- **Foxglove spikes without slivers.** A stem, a raceme and a flower turn
  their card to the eye from where each point of them is, not from their
  root: a raceme passing close to the eye, turned by its root's way, showed
  the eye its edge and its bells stretched into long pink spindles and
  floating petals (seen at 1920x1080 in the 0.6 m and 1.5 m panes). A
  raceme's side is settled once at its middle and its "down" leans down the
  stalk as the card lies.
- **Each spike its own.** Every stem stands at its own lean and bows along
  its length (`HerbStalk::bow`, `bow2`). A foxglove has its own bell count
  (0.7-1.25 of the record's) and stage: in bud to its middle, or open near
  its top with the lowest bells gone to green pods; bells spaced wider low,
  each at its own size and side.
- **Stem leaves.** `HerbSpecies::stemLeaves` / `stemLeafSize` put alternate
  stalkless leaves up a rosette's flowering stem (foxglove 4 a stem from 0.7
  of a rosette leaf, smaller upward; yarrow 2). The record is 56 floats (52:
  stemLeaves, stemLeafSize); the mean and side covers count them.
- **Nettle** in clumps: a leafy herb's leaves are drawn a pair a card
  (opposite, decussate), two shoots of six pairs, drooping, hairy.
- **Daisy from behind** shows its rays' backs round a cup of green bracts.
  Bluebells take their own size, place and slant a bell. A yarrow's corymb
  is a dozen small heads with shade between them.
- **Lavender** takes narrow grey felted leaves in stalkless opposite pairs.

### Motion (bro 1c43ffcc, broflora a2772d2, broworkshop d15b6bf)

- **Lavender's bare stalks.** A spray's tip is now a segment with nothing
  of any length beyond it (`sway.cpp`). A shoot's newest module can be a
  zero-length bud, which left every lavender limb without a tip, so without
  a twig cantilever and without the cap at 0.8 of its sprays' frequency: the
  flower stalks rang at 6-10 Hz. Its limbs are now 2.5-3.0 Hz under
  3.1-3.9 Hz sprays. Boxwood, holly, hydrangea and hibiscus are unchanged
  (limbs 2.0-3.3 Hz, sprays 4.3 Hz).
- **Sprays damped by their leaves.** `VEG_TWIG_DAMPING` 0.05 → 0.2 (kit
  `sway.js` mirrors it). A leafy shoot's leaves are most of its face and
  little of its mass; at 0.05 every spray rang at its one frequency, and a
  shrub, whose sprays are a third of its height, flickered with them
  (zeroing the twig level took the boxwood's flicker away; zeroing the leaf
  flutter did not).
- **Shrubs part without jerks.** `vegMoverPush` takes each trail sample as
  the spring's response to a push held over its 0.1 s step (the step
  response at its age less a step earlier), not an impulse: an impulse's
  response starts moving at once, so each new sample kicked the shrub's
  speed. The trail's last half second fades.
- **Herbs brushed aside.** A walker's push on a herb is 0.7 and a trample's
  lean 0.9 (were 1.2, 1.5): at the old values a poppy's head crossed a hand's
  breadth a frame as the walker came by. With the push zeroed the contrast
  floor run had 21 pops (the grass's own), so about half its pops were the
  herbs.
- Grove motion oracle, `motion/s15_before/` → this:

  | run | before | after |
  | --- | --- | --- |
  | shrub walker, shrub pops / flashes | 71 / 74 | 50 / 73 |
  | shrub walker, its grass pops | 32 | 26 |
  | meadow walker, pops | 77 | 64 |
  | meadow walker, contrast floor, pops | 45 | 32 |
  | boxwood 6 m, flickering blocks top / middle / bottom | 4 / 36 / 17 | 0 / 8 / 5 |
  | lavender 6 m, wiggle median top / middle | 7.2 / 8.4 Hz | 4.5 / 5.7 Hz |
  | grove tree (gusty), flicker p90 | 2.30 / 3.03 / 3.14 | 2.13 / 2.82 / 2.96 |
  | grove tree (gusty), pops | 59 / 168 / 172 | 51 / 138 / 160 |

- The boxwood's image wiggle stays 6-7 Hz at 6 m: its blocks move 0.2-0.5
  px a frame, under the tracker's noise, and with its twigs zeroed it still
  read 5-7 Hz; its limbs ring at 3.3 Hz and sprays 4.3 Hz.
- **The grass drift** 1-2.5 m from the path (step 10's open item) is gone
  since steps 12/12b: change left at the end at 1-1.5 m 19% → 2% of peak,
  0.6-1 m 26% → 3%, against `motion_s10_before/`.

### Near blades physical, gust waves (bro ff296f93)

- A thin blade (or the turf standing for blades) takes as much light
  through its face as it reflects, yellower (`SWARD_TRANSMIT`,
  max(-N·L, 0) / π), not a leaf's art glow of about four times that. Near
  and far share `swardGlow`: 0.3 of the light through is even, 0.7 in a
  cos⁴ lobe about the sun's way (as much light in all as an even spread),
  so looking toward a low sun the meadow glows. A near blade's face the eye
  sees is lit by its own slope to the sun where that is more than its
  softened bulk normal's.
- Forest `meadowtone` (the same ground at 8°, against 3 m; `s15base` →
  `s15t3`):

  | | 10 m | 30 m | 60 m | 100 m | 150 m |
  | --- | --- | --- | --- | --- | --- |
  | golden before | -10.2% | -19.4% | -18.3% | -15.7% | -10.0% |
  | golden after | -4.4% | -12.8% | -12.0% | -9.5% | -3.4% |
  | noon before | -2.8% | -8.7% | -7.0% | -4.5% | -1.6% |
  | noon after | -2.1% | -9.0% | -7.4% | -4.8% | -1.8% |

- `meadowbands` (the standing eye, against 2-4 m): noon within -5..+4%
  as before; golden 32-150 m +13..+16% → +20..+23%, because the near band,
  seen steeply, loses its art glow (2-4 m luma 85.9 → 83.1; with the sun
  ahead 87.5 → 80.8, 4-8 m 96.0 → 82.7). With the near transmission at 4×
  physical the near band returned to 88.4 / 97.8, so that is where the old
  glow came from.
- **Gust waves.** The far blades (from 4-20 m) and the turf take a gust's
  lift about the gust field's mean (`grassGustFar`, the field's mean 0.34 of
  its strength measured over 160,000 points), so the waves stand out of the
  sward's mean light and the meadow's tone does not move. Gusty meadow slit
  at 30 m: change in time 6.91 → 8.49 levels rms, best-match correlation
  0.70 → 0.79; 60 m 5.34 → 6.92, 100 m 6.11 → 6.45, 150 m 3.54 → 4.26.
  The bands run diagonally in the slit images, but the 30 m travel figure
  still peaks in place (0 m/s): the gust structure is about as wide as the
  view at 30 m, so a 0.5 s lag barely separates the shifts.

### Captures

- Presets, 0.5 / 1.5 / 4 / 15 m (shrubs 2.5 / 6 / 20 / 60 m), noon and
  golden: `broworkshop/demos/grove/captures/{s15base,s15after}_<preset>_{noon,golden}.jpg`
  (the foxglove's golden base was overwritten; its spindles are in
  `s15fx_foxglove_{noon,golden}.jpg` at 1080p, fixed in
  `s15a_foxglove_{noon,golden}.jpg`).
- Motion: `broworkshop/demos/grove/captures/motion/s15_before/` (before)
  and the same names in `motion/` (after); forest meadow
  `broworkshop/demos/forest/captures/motion/s15_before/forest_meadow_gusty_motion*`
  and `motion/forest_meadow_gusty_motion*`.
- Meadow tone: `broworkshop/demos/forest/captures/{s15base,s15t1,s15t2,s15t3}_meadow{tone,bands}.json`
  and their frames.

### Still open

- The shrub walker's 73 flashes are single-frame darkenings inside the
  boxwood, with the frames either side alike; not the movers' push.
- The near meadow at golden, seen steeply, is darker than before; the far
  meadow seen toward the sun is brighter than it.
- The 30 m gust travel measure, as above.
- The flight trace was not run on this step alone: the forest work in
  progress beside it (step 14) was built into the same binary and drew
  42 M vertices among the trunks.

## Step 16: tree-like wind

The user's report on a tall, slim conifer: the top moved like a spring,
fluid but not tree-like; small trees were springier than their size
allows; they should be pushed by the wind and stay there while it blows,
and the top half of slim trees hardly answered the wind.

Diagnosis (`demos/grove/lean.js`, the ground truth in `lib/kit/sway.js`
under scripted winds: a step to 12 m/s, a 5 -> 12 m/s gust and its
release, a steady 10 m/s):

- Every level rang about its lean as a lightly damped oscillator. Young
  fir (8.6 m): the top overshot a step 1.75x and rang 14 cycles, its low
  bough 1.97x for 18; steady 10 m/s wobbled 0.34-0.41 of the lean. Young
  oak 1.43-1.67x, 13-23 cycles.
- The leader stopped at a conifer's whorls, so the top 2-6 m of a fir or
  spruce was a separate limb (spruce: 6.3 m at 0.65 Hz) springing on its
  own: the "springy top".
- The trunk bent as one stiff cantilever, so a slim stem's top leaned no
  more than a thick one's.

Changes:

- broflora `sway.cpp`: the trunk is the stem strained alike along its
  taper, so a slender stem leans further and its slim top bends most; its
  bend has the shape h^p (p 1..4) carried in the trunk word (freq 9 bits |
  sway 9 | shape 6) and its frequency is Rayleigh's over that bend. The
  leader runs on through whorls while its thickest child is 80% of it or
  1.3x the next.
- bro `scene_vegetation.glsl` (vegTreeLevel, vegDamping): each level
  holds a lean downwind under the drag; damping by size (a trunk rings
  weakly, a bough is near critical, a spray over-damped); the trunk feels
  the wind averaged over its last half period; eddies too small for the
  gust field buffet limbs and sprays slowly about the lean. Herbs keep
  their motion. `lib/kit/sway.js` follows it step for step.

Before -> after (top; `s16base_young.txt`, `s16now.txt` in
`demos/grove/captures/motion/`):

- Young fir: step overshoot 1.75x, 14 cycles -> 1.16x with eddies, 1.00x
  and no ringing without them; gust release overshoot 0.30 (low bough
  0.78) -> 0.09; steady std/mean 0.34-0.41 -> 0.08.
- Young oak: 1.43x, 23 cycles -> 1.17x (1.00x without eddies); steady
  0.10-0.23 -> 0.09.
- Every tree and shrub preset: without eddies a step settles with no
  overshoot (pine 1.07x the most); steady wobble 0.06-0.16 of the lean.
- broflora ctest 4/4, bro `tests/flora` 7/7, `validate.sh demos/forest
  demos/grove` 4/4.

Still open: the flight trace on this step; a strip of the tall conifer
holding its lean in steady strong wind for the user to judge (sent as a
strong-wind grove video; the held lean is 4% of a young fir's height at
10 m/s, which may read as too little).

## Step 17: the far tiers against the leaves, part by part

Each part looked at alone (`compare ... hide=<other parts>`), so a tier's
fault is not lost in the forest's mean, and measured with `tiers` (a shrub
from above: `pitch=-0.6`, from the side its panes were the grass).

- Leaf clusters' shadow where the light leaves their lobe: one tap, not
  four (bro 332e4912). The taps were 1.4 of forest:leafCard:broad's 2.0 ms
  in the slowest flight frame and left the mid range smoother than the
  leaves it stands for. Flight p95 5.47 -> 5.36 ms.
- A spray node's clump closes its gaps within a spray's length of its
  surface (bro 767d77af). Its pads were a pale veil the ground showed
  through (forestLobeCore finds no core for a pine). Pine, forced clumps
  against forced leaves at noon, 15/25/40/60 m: +10.7/+8.7/+3.6/+0.8 ->
  +3.0/+1.3/-3.6/-9.1 levels; golden +6.9/+4.4/+2.6/+2.1 after.
- A broadleaf crown's self-shade is capped at 0.75 of its leaves' optical
  depth (bro 70a30c3e). A dense shrub's crown was near black from afar.
  Shrub noon 10/20/35 m -33/-26/-23 -> -11/-13/-10; broadleaf golden
  40/60/100 m -15/-14/-3 -> +2.0/+2.8/+5.6.
- A tree the camera draws as clumps casts clumps, not its crown (bro
  a7ca946a): crown casters shade only the far half. Pine from above at
  80 m, By size against Leaves: noon +6.7 -> -4.0, golden +11.2 -> -2.2.
  Shadow vertices 5.7 -> 8.5 M on the flight, the shadow passes' GPU time
  unchanged.

Found, not fixed: a broadleaf seen from above at 50-90 m in By size
covers less than its leaves (tiers `diff`, the mean change from bare
ground: 29.7 against 39.9 at 65 m). The far tiers drop bark under 0.7 px
(Lod::barkPixels) and thin it to nothing up to 1.6 times that, and from
above an open crown's fine branches are a grey haze over much of it: with
no cut, pure clusters match the leaves (39.0 against 39.3 at 90 m).
barkPixels 0.35 costs nothing on the flight, but the budget pays for it
in leaves (detail 1.07 -> 1.16) and the side view's 40-80 m hard edges
drop 5.7 -> 5.0. Fixed by sub-pixel bark drawn by its cover (bro
a8e30042): what the round bark cuts or thins, down to half the cut, is a
ribbon a pixel wide covering its width, as the clump twigs are, outside
the budget. It was also the canopy's missing hard edges: 40-80 m noon
5.7 -> 6.5 (Leaves 9.0), 20-40 m 7.2 -> 8.7 (9.7), golden 20-40 m
5.3 -> 8.0 (8.3). Twigs pass 0.35 -> 0.6 ms on the flight. Rejected along
the way: clusters' leaves 15% larger (+2 cover,
softer side view), their outline ramp moved out (+2.6..6.7 cover, side
view 20-40 m hard 7.2 -> 6.3), a stratified leaf order by clump (no
change), a wider cell search (+0.7..2.5).

New species, first try: a weeping willow. Its narrow
toothed leaves read well close up. Its form did not: a lateral aimed
below the horizon (lateralElevation -1.35) fights the spawn's collision
term and the shoots coil into loops (more so with weepingModule as the
lateral module), and round clumps make balls at the limbs' ends. Tall
clumps (clumpFlat over 1, hung below their shoot; clamps to 3 in
broflora's emit and clumps, bro's forest_node and the two leafFlat light
paths) gave columns, not curtains. What a willow needs is a sag at emit:
young shoots bend down under their own load (leaves and wood beyond
them) against their stiffness (E I from their radius, the cantilever
terms sway.cpp already has), from the tip inward, so the whips hang
straight from limbs that rise and arch. The same pass would hang a
birch's twigs and a spruce's boughs' tips by physics instead of set
points.

The sag pass and the willow preset. `Species::sag` (0, off, for every
other preset) turns each joint of the emitted skeleton toward hanging by
M / k times `sag`: M the moment about it of the wood (700 kg/m^3) and
leaves (2 N/m on shoots under three leaf radii) beyond it, k its
segment's E I / L (E 10 GPa), never past straight down, root first so
each segment carries every turn below it. Two gates make it a willow,
not a heap. The leader (the way to the highest tip) stands: without it
the top fell over and the tree stood 3.4 m, not 6.6. Only shoots within
0.15 of the plant's height of their furthest tip sag, fading out by
twice that: at 0.4 the limbs fell too and the tree was a leaf ball on a
bare whip over a tangle of cords; a radius gate hung the thick limbs as
well. The willow (trunk 2.4 m, pipe exponent 2.3, leaf diameter 0.01,
sag 30) stands 6.6 m, a dome of hanging shoots to the ground at 20 m
and a rounded clump at 60 m. Not yet: the far tiers draw it as a round
crown, not a curtain; its hanging foliage is thin close up.

A palm, from the same parts. Its root module is a clear column (5 m,
14 frond terminals in a golden-angle rosette from upright to level), and
each frond is a module with no terminals, so it never branches on. Such
a module now tapers from its base to a third of it at its tip (it had
one radius along it; no other preset has one): with a frond of one
radius the sag turned it most at its base and every frond hung straight
down from the crown; tapered, the stiff stalk rises and the thin end
arches over. The palm stands 8.4 m, a rosette of arching fronds on a
column at 20 m. Not yet: the fronds carry the broad-leaf clumps (bunches
of narrow leaves), not leaflets in two ranks along the rachis, so they
do not read as feathers; that is the shoot shader's to draw.

Pinnate leaves (Phyllotaxis::Pinnate, 3; bro's forestPinnateLeaf): a
clump's leaves line the wood it grows on, a clump's radius either way of
its base, in two ranks either side, leaning 0.45 rad toward the tip and
drooping (a frond's V), facing up; no twigs (ForestNode counts none for
a pinnate specimen's clumps) and no spray or shoot bend, riding the
wood's pose. The palm takes them, and less sag (10, was 100: with
leaflets the fronds' outer halves hung straight down): at 5 m feathered
fronds, at 20 m a radiating crown of them. Not yet: the leaflets favour
each frond's outer half (the clump placer's), and past 60 m the clump
and crown tiers draw the fronds as wisps, then a round blob.

The far willow and palm. Silhouettes (grove capture `species ... sil`,
leaves forced against auto) showed the far tiers matched the willow's
leaves tier: the leaves tier itself was a cloud of round balls, each
clump's leaves on its lobes' spheres. The willow takes pinnate leaves
too, in small clumps (clumpScale 0.35, leafSize 1.2), so they line its
whips: strands of foliage hanging from the arching limbs at 20 and 60
m. And a pinnate clump's sprites (the clump tier) are its three lobes in
a run along its wood, a third of the run each and a leaflet's reach
across, not spheres round it: the willow's clump tier hangs in strands,
and the palm keeps its radiating fronds at 60 m and a palm's outline at
150 m (it was a round blob). Not yet: close up the willow's leaves lie in
two flat ranks like a fern's, and at 60 m in auto the crown tier draws
it as a dome.

A cactus (a saguaro), from the same parts and one new bark. Its root
module is a column with three arms leaving its upper part level and
turning up at an elbow; the leader and arms lengthen only (maxBranchOrder
1, heightMax 5 m), and a leaf diameter of 0.3 m keeps every stem past
the clumps' 5 cm limit, so it carries none. Plated bark, at any scale,
read as snakeskin or speckle; `Species::barkRibs` (world format 12, the
bark word's top bit, its moss now 7 bits) draws ribbed bark instead: 48
/ barkScale ridges round the stem whatever its girth, unbroken along it,
rounded with sharp valleys, a third of their spacing deep times the
furrow, a row of pale areoles on each crest. It stands 6.2 m, ribbed and
elbowed at 5 m, a saguaro's outline at 20 to 150 m. Its arms and top
ended in points: the radius held (0.15 m to the top), but a far tree's
bark is one row of rings a segment, and the bark closes over a tip's
whole last segment (a metre here), so a cone. Now a ribbed plant's tips
are split (bluntTips: the last 0.8 of a radius its own cap segment) and
bro keeps a ribbed tip's girth to its end (not 0.3 of it): blunt domes
at every distance.

A succulent (an agave), the palm's parts at ground level: a stem 6 cm
tall topped by a rosette of 22 leaves, each a module that never branches
and so tapers to its point, the inner near upright, the outer spreading;
pipe exponent 3 (at 1 the 22 leaves summed to a stem a metre across).
Its leaves are its wood, blue-green and smooth, so it carries no clumps:
`clumpScale` 0 now means leafless (it fell back to 1; nothing set it),
since its leaves' tips are thin enough that the placer hung broad leaves
on them. A blue-green rosette 0.8 m tall at 2.5 to 20 m; past that it is
under the bark's cut.

A vine, first as a creeping one (ground ivy): a vine climbs something,
and nothing in a world of plants alone is yet there for it to climb, so
it trails along the ground. Seven runners fanned level from the root,
branching flat (plagiotropic modules, no orthotropy, nothing budding past
0.15 m), flat clumps of small glossy heart-shaped leaves on long stalks.
The first try (heightMax 0.25, clumpFlat 0.35, leafSize 0.55) heaped into
a 0.7 m mound; the preset is a mat 0.4 m tall with its runners showing,
from 2.5 to 20 m.

Ivy, the climber (Species::climb, Plant::support): a plant of a climbing
species standing where a plant that does not climb stands (their origins
one across) takes that plant's way up, its root-to-highest-tip joints
while its wood is at least 4 cm thick, refreshed every step (the
simulation grows the climber as a column; the emit winds it). Its emitted
wood goes to the support at its height along it, out by the bark's radius
and its stem, at its angle about its origin plus `climb` turns a metre,
side shoots standing off a quarter of how far out they grew, at most
12 cm. Each joint is moved by where it is alone, so nothing parts. It
sways as the host's stem at its height (the host's height and trunk
cantilever ride with the support; no limb or twig mode of its own): in
the grove's leanstrip under a strong wind it held to the oak's trunk and
limb in every frame, where with its own sway (a 6 m column of thin wood)
its mass swung off the bark. Nothing is baked: a forest draws the pair as
two specimens under one instance. Not yet: a climber on rock.

Lobe self-shade against the leaves tier (bro a05ef3fc): the leaves tier's
shade among a lobe's own leaves is the shadow map's, whose lookup is
pushed off along the leaf's normal and biased in depth by a few texels,
so no leaf is shaded by the leaves that near it toward the light; the
analytic path through a placed leaf's sphere now skips its first 3 shadow
texels too (at most 0.15 m), and the lobe sprite and cluster take 0.8 of
the lobe's depth (0.5 before the skip). Clumps against leaves: shrubs at
noon 10 / 20 m -6.7 / -6.6 -> -1.6 / -2.5 levels, golden -8.1 / -5.4 ->
-5.6 / -2.5; broadleaf noon side 45 / 75 / 120 m +1.7 / +2.1 / +2.1 ->
+0.6 / +0.9 / +0.7. (Without the skip, 0.8 matched the broadleaf but took
the shrubs 3.5 levels further down; with no lobe self-shade at all the
shrubs matched.)

Casters for thinned leaves (bro d6212931): a leaf is shaded by the casters
alone, and a tree's caster had gone to clump discs (each past its lobe's
middle, shading the lobes behind) while the camera still drew its leaves.
Where the camera draws leaves the caster now keeps leaves at the tile's
widening. Broadleaf auto against its leaves, golden 45 / 75 m +6.3 /
+11.1 -> +1.5 / +7.0 levels, 120 m +0.9 after (not measured before; the
commit's "+2.2" was not a measurement); noon 75 m +3.9 -> +2.4. The
flight's shadow vertices 6.2 -> 6.3 M. The flight trace's GPU times on
the Windows box swing tenfold between identical runs (one second 51 ms
in one, 4.4 ms in the other, same vertex counts): frame time is judged
on jserve, not there.

The needle trees' mid range (forest tiers, part=needle, shadow=0, noon):
auto at 45 / 75 m reads 100.8 / 100.6 against the leaves tier's 107.1 /
107.2, and it is not the widening: with no thinning (lod leafPixels 1)
auto is 102.4, and with the bark's cut taken to nothing (barkPixels 0.02)
107.1, the leaves tier exactly. A pine's fine wood, all under the bark's
cut and the ribbons' (half of it), is about 5 levels of its crown's
colour at 45 m: the orange shoots through its needles. Tried and reverted
(bro, uncommitted): raising the drawn ribbons' cover by the cut wood's
area over theirs, and drawing the 64 thickest cut segments as ribbons:
no change at all, unexplained (with a flat 8x on the natural ribbon band
at barkPixels 0.35 Y rose to 112, so the ribbons do reach the image
there). Then tried: a needle node's ribbons down to 0.07 px wide whatever
the widening: 45 / 75 m 100.8 / 100.6 -> 104.6 / 104.6 (0.03 px: 104.6 /
105.9), but the flight's vertices 19.0 -> 29.9 M; capped at its 128
thickest extra pieces a tree and none on crowns, no cost and no change.
The colour is in a pine's thousands of finest shoots, so no per-piece
draw buys it cheaply; the spray card's own twig line is about 3% of the
spray, too thin to. Next (a design): tint a needle node's sprays and
lobes toward its bark by the share of its wood the cut leaves out at the
tree's widening (per specimen, from its segments' areas in keep-radius
order against its crown's), on a channel of its own.

Done (bro 158be7c7): the tint, by 0.6 of the cut wood's area over the
crown's outline, carried in a draw entry's third record. Needle auto
against leaves at 45 / 75 m: unshadowed noon 100.8 / 100.6 -> 107.0 /
108.1 (leaves 107.1 / 107.2); noon -5.8 / -9.2 -> -0.4 / -3.2 levels;
golden +4.3 / +3.7 -> +9.1 / +9.2. The golden excess is not the tint's: with
the bark cut to nothing (lod barkPixels 0.02, so no tint) auto at 45 m
is -2.0 at noon and +3.4 at golden unshadowed, and with no widening
(leafPixels 0.01) exactly the leaves tier at both; the near form kept
while widened (cardWidening 100) swings the same (104.7 / 121.2 against
107.1 / 117.4). So the widening itself moves with the sun. A widened
spray's light is its anchor's (vegCrownShade: depth AO and the bulk
normal from the crown's centre), and a sparse kept set lets more of the
crown's far side show; with a low sun that side faces the light when
the view is toward it. Not yet tested.

Still open:

- Pine and spruce seen from above in By size (canopy shot): grey-green
  pads with none of the pine's orange limbs; needle-only skim at golden
  10-18 levels less red than the leaves. (The 80 m brightness is fixed,
  above; the limbs are the same sub-pixel bark cut.)
- Shrubs: clumps 7-14 levels darker than their leaves at noon (bro
  a6143926 capped a lobe's self-shade at 0.75 optical depth: 10 / 20 m
  -8.2 / -8.1 -> -6.7 / -6.6; the rest is neither the cast shade, which
  shadow=0 leaves at -7.6 / -10, nor the lobe's shell path); the
  darkest species still darker and more speckled than their leaves in the
  canopy view; crowns tier pinholes (cover 71% against 87% at 10 m).
- Broadleaf crowns in By size read paler and cottony up close (canopy
  20-40 m).
- On the Mac (MoltenVK) two renders of one still frame differ in ~40
  scattered foliage pixels (R and G by ~5 levels): grove/forest calm-still
  tests fail there. Not on Windows or halo.

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
   ground and the bark (forest floor, turf, bark plates and moss, root
   flare and collars).

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
