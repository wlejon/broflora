#pragma once

// Herbs: the low plants of a meadow and a forest floor (ferns, wildflowers,
// forbs and ground cover), as species presets any app gets by name.
//
// They are far too many to grow one by one. A herb is drawn where it stands
// from its species and a hash of its place: bro.scene's grass node scatters
// one plant a cell of its own grid (scene_herb.glsl), chooses its species by
// how much light the ground there gets (the canopy overhead) and by each
// species' colonies, and builds the plant from the numbers here: its leaves
// or fronds round its root (or up its stems, or strewn as a carpet), its
// stems, its blooms at their tips, each leaf's outline cut per pixel.
// Further off the plants hand over to the grass's blades and the turf, which
// take the species' mean colours (herbMeanColor) so a meadow keeps its
// colour and its flowers' speckle to the horizon. Nothing is baked per plant.
//
// A plant sways as the trees do (broflora/sway.h): each stem or frond is a
// cantilever, its stiffness from its thickness (3 E I / L^3, a green stem's
// modulus), its mass from its tissue and what it carries, its drag from the
// leaf or bloom face it shows the wind near the ground (herbSway).

#include "broflora/sway.h"

#include <bromath/vec.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace broflora {

// Where a herb's leaves stand (HerbSpecies::habit).
enum class HerbHabit : uint32_t {
    Rosette = 0,   // leaves or fronds round the root; leafless scapes (flower stalks) from the middle
    Leafy = 1,     // leafy stems from the root, the leaves up them in pairs or turns
    Carpet = 2,    // ground cover: small leaves on stalks strewn over the plant's patch
};
inline constexpr uint32_t kHerbHabitCount = 3;
// A carpet's `leaves` are its stalked leaves' places: each holds a cluster
// of this many leaves of its own size, which the renderer cuts out of one
// card (a carpet of clover is hundreds of leaves a square metre).
inline constexpr uint32_t kHerbCarpetCluster = 6;

// A herb leaf's form (HerbSpecies::leafShape); the renderer cuts it per pixel.
enum class HerbLeaf : uint32_t {
    Frond = 0,       // a fern's: `pinnae` pairs of leaflets along a rachis, each lobed into pinnules by `lobes`
    Simple = 1,      // one blade: `width`, `tip`, `teeth` / `toothDepth`, pinnate veins
    Strap = 2,       // long, narrow, parallel-veined (a bluebell's)
    Trifoliate = 3,  // three leaflets on one stalk (clover rounded, wood sorrel notched: `tip`)
    Lobed = 4,       // pinnately lobed: `pinnae` pairs of lobes cut `lobes` deep (a poppy's; a yarrow's finely)
    Ribbed = 5,      // broad, with veins running its length (a plantain's)
};
inline constexpr uint32_t kHerbLeafCount = 6;

// What a herb flowers as (HerbSpecies::bloomForm).
enum class HerbBloom : uint32_t {
    None = 0,
    Ray = 1,     // a daisy: many narrow rays round a yellow disc
    Cup = 2,     // a poppy, a wood sorrel: a few broad petals, cupped, a dark or yellow centre
    Head = 3,    // a clover: a round head of tiny florets
    Spike = 4,   // a plantain: a dense cylinder of florets at the top of its stalk
    Umbel = 5,   // a yarrow: a flat-topped cluster of tiny florets
    Bell = 6,    // a bluebell, a foxglove: `bloomCount` bells hanging from the top of the stalk
};
inline constexpr uint32_t kHerbBloomCount = 7;

struct HerbSpecies {
    std::string name;
    HerbHabit habit = HerbHabit::Rosette;
    // The plant.
    float height = 0.4f;        // m: how high its stems (or fronds) reach
    float spread = 0.25f;       // m: how far its leaves reach out from the root (a carpet's patch radius)
    uint32_t leaves = 6;        // leaves (fronds) a plant
    uint32_t stems = 0;         // stems (scapes) a plant; each bears one bloom (or raceme) at its tip
    // Its leaves.
    HerbLeaf leafShape = HerbLeaf::Simple;
    float leafLength = 0.12f;   // m, stalk included
    float leafWidth = 0.4f;     // width over length
    float leafTip = 0.0f;       // 0 pointed .. 1 rounded (a frond: 0 lanceolate .. 1 deltoid, widest at its base)
    uint32_t teeth = 0;         // teeth a side
    float toothDepth = 0.0f;    // of the half-width
    uint32_t pinnae = 0;        // a frond's leaflet pairs, a lobed leaf's lobe pairs
    float lobes = 0.0f;         // how deep a frond's pinnae are lobed into pinnules (0 entire .. 1 to the midrib)
    float stalk = 0.1f;         // the share of a leaf's length that is its stalk (stipe, petiole)
    float rise = 0.5f;          // rad: a leaf's angle from upright where it leaves the root or stem
    float arch = 0.6f;          // rad: how far it bends over toward its tip
    bromath::Vec3 leafColor{0.06f, 0.12f, 0.03f};   // linear albedo
    float gloss = 0.0f;         // 0 matt .. 1 waxy
    // Its stems.
    bromath::Vec3 stemColor{0.08f, 0.12f, 0.04f};
    float stemDiameter = 0.003f;   // m, at the base
    float ribDiameter = 0.002f;    // m: a leaf's midrib (a frond's rachis) at its base
    // Its blooms.
    HerbBloom bloomForm = HerbBloom::None;
    float bloomSize = 0.0f;        // m: a flower's or a head's across; a raceme's or a spike's length
    uint32_t bloomCount = 1;       // a raceme's bells
    uint32_t petals = 5;
    bromath::Vec3 bloomColor{0.0f, 0.0f, 0.0f};
    bromath::Vec3 centreColor{0.75f, 0.5f, 0.03f};
    float nod = 0.0f;              // 0 a bloom faces up .. 1 it hangs
    // Where it grows. The renderer weighs each species at a point by its
    // abundance, how well the light there suits it (the canopy overhead, 0
    // open .. 1 closed, against `shade` +- `shadeRange`) and its colonies
    // (noise of period `patch` m, `patchCover` of the ground in them).
    float shade = 0.0f;
    float shadeRange = 0.3f;
    float abundance = 1.0f;
    float patch = 5.0f;
    float patchCover = 0.4f;
    // Its sway (herbSway fills these; a preset comes with them set).
    Cantilever stemSway;   // a stem (or scape) from the root to its bloom
    Cantilever leafSway;   // a leaf or frond from its base to its tip
};

/// The built-in herb presets' names: ferns, then wildflowers, then forbs and
/// ground cover.
const std::vector<std::string>& herbPresetNames();

/// Preset `name` (its sway worked out), or nullopt.
std::optional<HerbSpecies> herbPreset(std::string_view name);

/// Work out `h`'s stem and leaf cantilevers from its build (herbaceous
/// tissue: E 1.2 GPa, 900 kg/m^3 stems, 0.12 kg a m^2 of leaf), and the drag
/// on what each carries at the wind near the ground at its height (a log
/// profile over a meadow, against kSwayReferenceWind at 10 m).
void herbSway(HerbSpecies& h);

/// The albedo a herb shows from above, over the ground it stands on: its
/// leaves', with its blooms' share of that face (`bloomShare`), and how much
/// of its patch its leaves cover (`cover`): what a far blade or the turf
/// stands in with.
bromath::Vec3 herbMeanColor(const HerbSpecies& h, float* bloomShare = nullptr, float* cover = nullptr);

/// One species as bro.scene's grass node reads it (scene_herb.glsl's
/// HerbSpecies), kHerbRecordFloats floats:
///   0  habit, height, spread, leaves
///   4  stems, leafShape, leafLength, leafWidth
///   8  leafTip, teeth, toothDepth, pinnae
///   12 lobes, stalk, rise, arch
///   16 leafColor rgb, gloss
///   20 stemColor rgb, stemDiameter
///   24 bloomForm, bloomSize, bloomCount, petals
///   28 bloomColor rgb, nod
///   32 centreColor rgb, ribDiameter
///   36 shade, shadeRange, abundance, patch
///   40 patchCover, bloomShare, cover, 0
///   44 stem freq, stem sway, leaf freq, leaf sway
inline constexpr size_t kHerbRecordFloats = 48;
void herbRecord(const HerbSpecies& h, float* out);
/// The records of several, end to end.
std::vector<float> herbRecords(const std::vector<HerbSpecies>& hs);

} // namespace broflora
