# Look and motion: broflora beautiful by default

broflora's simulation grows good plants, but what reaches the screen does not
look or move like a tree. This plan adopts what looked best in a search that
built the same valley six ways from code (`../opus-worlds`, a local scratch
repo: `blocks/<id>/`, `gallery/<id>/`, `notes/<id>.md`). The aim is that any app
calling broflora and bro.scene with defaults gets trees, shrubs and grass that
are beautiful standing still and move like plants.

This file keeps steps 1 to 5, which are still what the records, the sway and
the grass are. The later rounds (steps 6 to 17: bro's forest node, its
tiers, impostors and far canopy, and the grove and forest demos they were
judged on) were retired with that node; their notes are in this file's git
history. Today's foliage path is bro's plants node (`scene.createPlants`,
bro `docs/scene-nodes-api.js`), drawn from the same records and the same
pose.

## What looked best, and why

| Quality | Reference | What makes it work |
|---|---|---|
| Trees standing still | volume (`blocks/volume/trees.js`, `sprites.js`, `gallery/volume/r2_*`) | kinked branches (never straight), smooth-unioned bark; the crown is **clumps** of leaf mass with gaps between them, not a uniform spray; a crown envelope gives each point an interior depth, so the crown is dark inside and lit at the rim |
| Tree motion | dense (`blocks/dense/flora.js` `treeBend`, `element.js`) and growth (`blocks/growth/flora.js`) | hierarchical sway: the trunk leans into a **travelling gust** (offset grows with height squared); each limb bobs on its own phase, weighted by branch weight squared (0 on the trunk, 1 at a twig tip); each leaf flutters about its own stem and shows its paler underside; all of it in the vertex shader, nothing on the CPU |
| Lighting a mass of leaves | dense (bulk normals) | each leaf is lit partly by the normal of the mass it belongs to (crown centre outward), plus ao that darkens leaves deep inside: thousands of cards read as one rounded form with a lit side and a shade side; translucency where the sun is behind |
| Grass | dense (`blocks/dense/ground.js`, `flora.js`, `gallery/dense/r2_ground.png`) | dense procedural blades placed from the instance id over tiles around the camera (no per-blade data), lit by the ground's bulk normal, bent by the same travelling gusts, parted by things moving through it |

What did **not** work: volume's trees move as a single blob, because a
volume can only bend as a whole. bro.scene's wind before step 1 had the same
problem: one sine per vertex, weighted by vertex colour R, with no gusts and
no hierarchy.

## Where each piece lives

- **broflora (C++ emit)**: the geometry and the per-vertex data motion and
  lighting need: branch weight, a per-limb phase, the leaf's stem anchor and
  flutter axis, the plant's root and height, the crown centre and radius. Better
  geometry: kinked, smooth-joined branches, leaf clumps, crown interior depth.
- **bro.scene (engine)**: a wind field with travelling gusts behind
  `setWind`. A vegetation path in the mesh/instanced shaders: hierarchical
  sway, bulk-normal blend, crown ao, translucency, a two-sided leaf
  underside. The same sway runs in the shadow pass. A grass node.
- **bromesh**: leaf-card and scatter changes only where the emit needs them.
- **bro.flora JS**: defaults that wire it all up, so the simplest app looks right.
- **broworkshop demos**: the showcase and the oracle (today `demos/biomes`
  and `demos/flora-lab`).

## Motion oracle

A reviewer who reads text and stills cannot watch a video. The motion
oracle turns a run's motion into sheets and a short text that point at
the faults. It lives in broworkshop: `lib/kit/motion.js` (the image half,
for any demo) and `lib/kit/sway.js` (the ground truth: the wind field and
`vegPose` / `vegLeafTurn` ported to JS, evaluated as the GPU does).

The image half tracks 16 px blocks frame to frame. For each block:

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

The truth half gives the following for each tracked part (trunk top, limb,
twigs, a leaf):

- Its rms, dominant Hz, largest step and jerk spikes, each attributed to a
  gust band edge or a kick kink.
- The share of its variance that is the drag's mean bend.
- Its coherence with the other parts.
- The leaf flutter's instantaneous frequency over a long wind clock.
- A stand's trunk tops: correlation and lag against the gust's travel.

With a moving camera every block is "lost": the view's own motion is not
plant motion. Read only the events there. Only large ones count (D over
about 30, tens of blocks); their crops show what jumped.

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

Per segment (the GPU tube and scatter paths) it is 16 floats too
(`segment_vegetation` in `sway.h`): root.xyz, height, crown.xyz,
crownRadius, weightFrom, weightTo, 0, 0, 0, twig, limb, trunk.

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

The grove demo of the time had uneven, colour-varied ground rising to hills,
plants standing on it, thicker trunks (`pipeExp` 2.0, `leafDiameter`
0.028), a darker leaf mean, golden-hour haze pushed past the grove and an
under-the-crown close shot.

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
- **Cost** (1280x720, RTX 4090, perf.gpuFrameMs over 60 frames): the grass was
  1.6 to 2.2 ms of a 12.7 to 13 ms grove frame (10.7 to 11.4 ms without it),
  about 210k / 110k / 100k instances in the three rings.

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
    long deflects 4x more (the `sway_cantilever_terms` test).

The physical constants are textbook values: green hardwood E 9 GPa and
800 kg/m^3, Cd 0.3 for a crown streamlined at 10 m/s, and 0.5 kg of leaves
per m^2 of leaf face. The **gains** divide each level's stiffness for what
a uniform beam on a rigid footing leaves out:

- trunk 10: its taper and the root plate turning in the soil;
- limb 4: the give at its fork;
- spray 1.

They were set against field ranges. Trees about 10 m drawn come out like
this:

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

The oracle was a port of the shader's response, line for line, against an
integrated damped oscillator under the same drag over 60 s, plus 15%
unresolved turbulence: deflection RMS and speed agreed within 15% at every
level, in breeze and in gusty.

The pose is two layers. **`vegPose(VegPlant, VegLimb, VegWind, rest,
weight)`** is the shared one: per plant the root, the height and the trunk
word (record floats 0-3 and 15), per limb the twig and limb words (floats 13
and 14), per point its rest position and branch weight. Whatever draws a
plant calls it with the same data and gets the same offset and rotation, so
one plant's whole motion fits in one record. **`vegLeaf`** is the leaf's
own layer on top (below), and needs the leaf's axis (float 12).

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
- **Cup**: a cupped card (`cup` 0.55; bromesh `leafCard` recomputes
  normals) varies its normal across the blade.
- **Edges**: leaves are cut-out geometry, not alpha-tested, so they want
  MSAA.

### Trunks close up

- Tube rings are framed by a minimal rotation from +Y to the joint's
  tangent (`tubeRingFrame`), not `cross(axis, Y)`, which flipped on
  near-vertical trunks.
- The rings are placed in the joint plane at the segment's end point, so
  the two segments share the ring and its normals: there is no seam, crease
  or twist, and the kink stays.

### Cost

GPU ms at 1280x720 on an RTX 4090, mean `perf.gpuFrameMs` over 60 frames,
in the grove of the time:

| Shot | Before step 4 | After, no MSAA | After, 4x MSAA |
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
- **Soil.** Soil darker and cooler than the blades (lush
  `[0.028, 0.038, 0.022]`) is the shade at the foot of a sward, so the gaps
  read as depth.

### Cost

A vertex costs about the same whether the shader culls it at its first test
or draws it. So culling inside the shader saves nothing; only not issuing
the vertex does. Two changes follow from that:

- **Tiles in view.** The drawer splits each ring's visible box into tiles of
  16 x 16 cells. It keeps only the tiles that reach the ring, where
  something grows, and whose box (ground to the tallest blade) is in the
  view frustum. The field's coarse blocks (`GrassNode::fieldRange`) give
  each tile's height range.
- **Shadow casters.** These keep a third of the near ring's blades,
  widened.

GPU ms at 1280x720, RTX 4090, 4x MSAA, mean `perf.gpuFrameMs` over 60
frames, grass casting:

| Shot | Frame before | Frame after | Cells issued before | Cells issued after |
|---|---|---|---|---|
| ground (in the grass) | 5.18 | 4.72 | 211k / 112k / 100k | 9k / 17k / 28k / 12k |
| meadow | 6.49 | 5.98 | 206k / 109k / 97k | 9k / 18k / 29k / 16k |
| grove | 6.61 | 5.75 | 213k / 114k / 101k | 0 / 15k / 29k / 17k |

The grass's own share (frame minus frame without it) in the ground shot fell
from 1.35 to 0.89 ms. Blade work is per vertex issued: budget grass by
vertices issued, not by blades drawn.

## Order

1. Motion: the wind field, hierarchical sway, bulk normals and translucency in
   bro.scene; broflora emits the sway data.
2. Look: crown clumps, kinked bark, interior depth, leaf underside.
3. Grass: the grass node on the same wind, parted by movers.
4. Wood and leaves: cantilever terms from the geometry, damped-oscillator
   response, the leaf layer, softer leaf light, MSAA, seamless tubes.
5. Lush grass: tufts of fine blades an instance, four rings at a level
   cover, view-culled tiles, darker soil.

Each step is judged by motion strips and short videos next to the reference
galleries, not by stills. What comes next for plants at world scale is bro's
`docs/scene-world-architecture.md`.
