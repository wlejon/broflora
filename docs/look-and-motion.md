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

## Order

1. Motion: the wind field, hierarchical sway, bulk normals and translucency in
   bro.scene; broflora emits the sway data; the lab shows it.
2. Look: crown clumps, kinked bark, interior depth, leaf underside.
3. Grass: the grass node on the same wind, parted by movers.

Each step is judged by motion strips and short videos next to the reference
galleries, not by stills.
