#pragma once

// Wind sway: what a renderer needs to move a plant in wind the way wood and
// leaves move, and the vegetation records that carry it to bro.scene
// (docs/look-and-motion.md "Step 4 as built").
//
// The motion model. A plant is three levels of cantilever, each bending in
// the wind like a beam fixed at its base:
//
//   trunk  the plant's stem, from its root to its height;
//   limb   each subtree leaving the trunk, from where it leaves to its
//          farthest twig tip;
//   twig   the limb's terminal shoots.
//
// Each level is a damped oscillator in its first bending mode, with two
// numbers, both from the simulation's own geometry (the pipe model sets the
// diameters, the foliage the leaves):
//
//   freq   the natural frequency, f = sqrt(k / m) / 2 pi (Rayleigh): the
//          tip stiffness k = 3 E I / L^3 of a beam of its base diameter d
//          (I = pi d^4 / 64) over the mass the mode moves, m = sum of
//          every piece it carries (wood and leaves) times phi(s)^2, phi the
//          mode's shape s^2 (3 - s) / 2 at the piece's place s along it.
//          A bare uniform beam gives Euler-Bernoulli's f ~ d / L^2 (thick
//          and short is quick; long and slender is slow); what a stem
//          carries far out slows it further, so a tree's trunk, long and
//          holding the whole crown, is the slowest even though it is the
//          thickest, and its twigs are the quickest.
//   sway   the static deflection of the tip over the length at the
//          reference wind kSwayReferenceWind: the drag on the leaves it
//          carries (0.5 rho Cd A U^2, A the leaf face: the crown's disc
//          times kSwayCrownFill, shared among segments by their foliage),
//          each piece weighted by phi(s) and by the shelter of the crown
//          above it (kSwayCrownShelter), over k. At one length and load a
//          twice thicker branch deflects 16x less; a twice longer one 4x
//          more.
//
// The renderer drives each level with the wind's drag at the plant and
// gives it its own response (a lagged mean, ringing at its frequency when
// the drag changes, damped), adds the levels down the hierarchy, and turns
// a leaf's frame with the bend of what it hangs from. bro.scene's
// scene_vegetation.glsl is the reference.

#include <bromath/vec.h>
#include <bromesh/mesh_data.h>
#include <bromesh/procedural/leaf_scatter.h>
#include <bromesh/procedural/space_colonization.h>

#include "broflora/plant.h"
#include "broflora/world.h"

#include <cstdint>
#include <vector>

namespace broflora {

// The model's constants. The physical ones are textbook values. The gains
// (kSway*Gain) divide a level's stiffness, standing for what a uniform beam
// on a rigid footing leaves out: a trunk tapers, so it bends most near the
// top, and its root plate turns in the soil (in tree-pulling tests the
// anchorage gives as much as the stem); a limb's base gives in its fork.
// They were set by eye against field ranges (an open-grown tree of 10 m
// sways at ~0.5-1 Hz; a leafy twig flutters at a few hertz).
inline constexpr float kSwayReferenceWind = 10.0f;  // m/s: `sway` is the deflection at this wind
inline constexpr float kSwayAirDensity = 1.2f;      // kg/m^3
inline constexpr float kSwayDrag = 0.3f;            // Cd of a leafy crown streamlined at 10 m/s
inline constexpr float kSwayCrownFill = 0.5f;       // the share of a crown's disc the wind meets as leaves
inline constexpr float kSwayLeafBreadth = 0.2f;     // m^2 of leaf face a metre of leafy shoot
inline constexpr float kSwayLeafMass = 0.5f;        // kg of leaves (all layers) a m^2 of leaf face
inline constexpr float kSwaySprayLength = 0.5f;     // m: a leafy spray, the shoots beyond a twig's
inline constexpr float kSwaySprayDiameter = 0.008f; //    last fork, at its base
inline constexpr float kSwayWoodModulus = 9.0e9f;   // Pa: green hardwood
inline constexpr float kSwayWoodDensity = 800.0f;   // kg/m^3: green hardwood
inline constexpr float kSwayTrunkGain = 10.0f;
inline constexpr float kSwayLimbGain = 4.0f;
inline constexpr float kSwayTwigGain = 1.0f;
// The wind inside and under a crown: it falls off down through the leaves
// the crown holds above, u(z) = u_top exp(-a (1 - z / H)) (Cionco's canopy
// profile; a from about 1 in a sparse crown to 4 in a dense one), so the
// drag on a piece at height z is exp(-2 a (1 - z / H)) of the drag at the
// top. A tree's lowest boughs, long and heavy but in the still air under
// the crown, hardly move; the upper crown and the leader carry the motion.
inline constexpr float kSwayCrownShelter = 2.5f;
// Where in the plant kSwayReferenceWind blows: at this share of its height
// (a broadleaf crown's middle), stronger above, weaker below.
inline constexpr float kSwayReferenceHeight = 0.75f;
// The drag on a piece at height `z` above the plant's base of height `h`,
// against the drag at its top.
float swayShelter(float z, float h);

// One level as a cantilever. `length` in the plant's units (metres).
struct Cantilever {
    float freq = 0.0f;     // Hz
    float sway = 0.0f;     // tip deflection / length at kSwayReferenceWind; 0: does not bend
    float length = 0.0f;
};

// What a cantilever's first mode carries: every piece's mass times phi(s)^2
// and the leaf face it shows the wind times phi(s), s its place along the
// cantilever.
struct CantileverLoad {
    float mass = 0.0f;   // kg (the beam's own wood included)
    float area = 0.0f;   // m^2 of leaf face
    // kg: every piece's mass times phi(s), what gravity bends a level that
    // leaves its parent sideways by (a limb, a twig; 0 for an upright trunk).
    float weight = 0.0f;
};

// A limb holds its own weight: whatever the beam of its base diameter gives,
// it is at least stiff enough that its weight bends its tip no more than
// this share of its length (wood grows to its load: thigmomorphogenesis and
// reaction wood; a beam of the simulation's pipe-model diameter alone would
// let a long, leafy low bough sag metres). So a long heavy bough, which
// must be stiff to stand out at all, deflects little in the wind, and a
// light shoot high in the crown, with little weight to bear, moves most.
inline constexpr float kSwaySagLimit = 0.08f;
// The bending strain at a level's base the reference wind takes it to at
// most (green wood breaks near 0.7%; its leaves streamline and spill the
// wind long before), so a slender, top-heavy stem leans, not whips.
inline constexpr float kSwayWorkingStrain = 0.002f;

// The mode's shape at s in [0, 1] along a cantilever: s^2 (3 - s) / 2.
float swayModeShape(float s);
// A uniform beam's own share of its mode's mass (33/140 of its wood).
float swayBeamMass(float d, float L);

// The cantilever of base diameter `d` and length `L` carrying `load`, its
// stiffness divided by `gain`. Frequencies are clamped to [0.08, 12] Hz and
// sway to [0, 0.5].
Cantilever swayCantilever(float d, float L, const CantileverLoad& load, float gain);

// The sway of one segment. Everything but the weights is shared: the plant
// fields by every segment of the plant, the limb fields by every segment of
// the limb, so a limb moves as one piece and no joint opens.
//
//   - `root`, `height`    the plant's base (its origin) and its height above
//                          it (top of the highest segment).
//   - `crown`, `crownRadius`  centre and radius of the leafy volume: the
//                          foliage-mass-weighted centre of the leaf-bearing
//                          segments and the distance to the farthest one.
//                          A leaf's normal blends toward its centre-out
//                          direction ("bulk normals") so a crown shades as
//                          one form, and leaves deep inside darken.
//   - `weightFrom`, `weightTo`  branch weight at the two ends: 0 on the trunk
//                          and where a limb leaves it, rising along the limb
//                          to 1 at every twig tip (distance travelled over
//                          distance travelled plus the longest way still to
//                          go). It places a point along its limb's bend.
//   - `phase`             in [0, 1): one per limb, so limbs move out of
//                          step.
//   - `trunk`             the plant's stem as a cantilever (length = height).
//   - `limb`              this segment's limb; zero on the trunk.
//   - `twig`              the limb's terminal shoots; zero on the trunk.
//
// The trunk is the leader chain from the thickest root segment: the
// thickest child at each fork while it keeps at least 80% of its parent's
// radius and 10% of the base radius: a conifer's leader runs on to its top,
// so its upper boughs are light limbs of their own rather than one heavy
// limb the whole upper crown hangs from.
struct SegmentSway {
    bromath::Vec3 root{};
    float height = 1.0f;
    bromath::Vec3 crown{};
    float crownRadius = 0.0f;
    float weightFrom = 0.0f;
    float weightTo = 0.0f;
    float phase = 0.0f;
    // Bark: the way along the wood from the plant's root to this segment's
    // `from` (metres, through every fork), so a pattern laid along the stem
    // runs on unbroken across joints; and the plant's bark word
    // (packBarkWord).
    float pathFrom = 0.0f;
    float bark = 0.0f;
    Cantilever trunk;
    Cantilever limb;
    Cantilever twig;
};

std::vector<SegmentSway> emitPlantSway(const Plant& plant);
std::vector<SegmentSway> emitWorldSway(const WorldState& world);

// ---- Vegetation records ------------------------------------------------------
//
// Per vertex / per instance: 16 floats, four vec4s,
//   (root.xyz, height) (crown.xyz, crownRadius) (anchor.xyz, weight) (axis, twig, limb, trunk)
// the layout bromesh::MeshData::vegetation and bro.scene read. `anchor` is
// the point a leaf hangs from (a bark vertex's point on its skeleton);
// `crownRadius` is zero on bark, which takes no bulk normal. The last four
// are packed words, each an integer below 2^24 held exactly in a float:
//
//   axis   a leaf's stem (tip) direction, octahedral, 12 bits a component,
//          plus one (packSwayAxis); 0 on wood, which does not flutter
//   twig   phase (6 bits), twig freq, sway, length (6 bits each)
//   limb   limb freq, sway, length (8 bits each)
//   trunk  trunk freq, sway (12 bits each)
//
// Each number is quantised on a log scale over its range below (code 0 is
// zero); unpackSway* invert them, as the shader does.
inline constexpr size_t kVegetationFloats = 16;
inline constexpr float kSwayFreqRange[2] = {0.05f, 50.0f};
inline constexpr float kSwaySwayRange[2] = {1.0e-5f, 1.0f};
inline constexpr float kSwayLengthRange[2] = {0.005f, 200.0f};

float packSwayAxis(const bromath::Vec3& axis);
float packSwayTwig(float phase, const Cantilever& twig);
float packSwayLimb(const Cantilever& limb);
float packSwayTrunk(const Cantilever& trunk);
bromath::Vec3 unpackSwayAxis(float word);
Cantilever unpackSwayTwig(float word, float* phase = nullptr);
Cantilever unpackSwayLimb(float word);
Cantilever unpackSwayTrunk(float word);

// Write one record. `weight` is the branch weight at this point; pass a zero
// axis for wood.
void writeVegetation(float* out, const SegmentSway& s, const bromath::Vec3& anchor,
                     float weight, const bromath::Vec3& axis, bool leaf);

// Write a wood *segment* record, for geometry that spans a whole segment
// under one record (an instanced cylinder per segment): wood takes no bulk
// normal, so the crown floats carry the segment's tip instead,
//   (root.xyz, height) (to.xyz, -(1 + weightTo)) (from.xyz, weightFrom) (0, twig, limb, trunk)
// and the renderer gives each vertex the weight at its own point along
// from -> to (a negative crownRadius marks the form). The segment's tip
// then bends exactly as its child's base does.
void writeSegmentVegetation(float* out, const SegmentSway& s, const bromath::Vec3& from,
                            const bromath::Vec3& to);

// Vegetation records for leaves placed by `bromesh::placeLeavesOnBranches`
// over `segs` (`sway` aligned with them): one per placement, the anchor at
// the leaf's origin, the axis its +Z (the tip direction), the weight
// interpolated along its segment.
std::vector<float> leafVegetation(const bromesh::LeafPlacements& pl,
                                  const std::vector<bromesh::BranchSegment>& segs,
                                  const std::vector<SegmentSway>& sway);

// Repeat each 16-float record over `verts` consecutive vertices: per-leaf
// records onto a mesh stamped `verts` vertices per leaf.
std::vector<float> vegetationPerVertex(const std::vector<float>& perItem, size_t verts);

// Per-segment vegetation blocks for bro.scene's tube / scatter records,
// 16 floats per segment: (root.xyz, height) (crown.xyz, crownRadius)
// (weightFrom, weightTo, path, bark) (0, twig, limb, trunk). The renderer
// puts each leaf's own axis in the zero. `path` is the way from the root to
// the record's from-end (SegmentSway::pathFrom) and `bark` the plant's bark
// word: what bro.scene's bark shading reads.
//
// A tube piece covering only [along0, along1] of its segment (tubeSegments
// splits a segment at a root flare or a branch collar) takes the weights
// and the path at its own ends; `length` is the whole segment's.
inline constexpr size_t kSegmentVegetationFloats = 16;
void appendSegmentVegetation(std::vector<float>& out, const SegmentSway& s);
void appendSegmentVegetation(std::vector<float>& out, const SegmentSway& s, float along0, float along1,
                             float length);

// The bark word: Species::barkFurrow, barkScale and barkMoss as three 8-bit
// codes in one float (an integer below 2^24, exact). Code 0 of a field means
// "unset" (the renderer's default); furrow and moss are linear over 0..1,
// scale logarithmic over kBarkScaleRange.
inline constexpr float kBarkScaleRange[2] = {0.25f, 4.0f};
float packBarkWord(float furrow, float scale, float moss);
void unpackBarkWord(float word, float& furrow, float& scale, float& moss);

// Bark-mesh vegetation for `mesh` as bromesh::meshBranches laid it out over
// `segs` with `sides` sides (emitPlantMesh / emitWorldMesh).
void bakeVegetation(bromesh::MeshData& mesh, const std::vector<bromesh::BranchSegment>& segs,
                    const std::vector<SegmentSway>& sway, int sides);

} // namespace broflora
