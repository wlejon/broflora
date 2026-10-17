#include "broflora/herbs.h"

#include <algorithm>
#include <cmath>

namespace broflora {

using bromath::Vec3;

namespace {

constexpr float kPi = 3.14159265f;
// Herbaceous tissue: a green stem's or rachis' bending modulus and density,
// and a soft leaf's mass a square metre (all layers).
constexpr float kHerbModulus = 2.0e9f;
constexpr float kHerbDensity = 900.0f;
constexpr float kHerbLeafMass = 0.12f;
constexpr float kHerbBloomMass = 0.6f;    // kg a m^2 of bloom face (petals, florets, a head's body)
constexpr float kHerbDrag = 0.45f;        // Cd of a leaf or a flower that flutters, not a streamlined crown
// The wind near the ground: a log profile over a meadow of roughness z0
// (m), against kSwayReferenceWind at 10 m.
constexpr float kMeadowRoughness = 0.03f;

float windShare(float z) {
    z = std::max(z, 2.0f * kMeadowRoughness);
    return std::log(z / kMeadowRoughness) / std::log(10.0f / kMeadowRoughness);
}

Vec3 rgb(float r, float g, float b) { return {r, g, b}; }

// The share of a leaf's length x width card its outline fills.
float leafFill(HerbLeaf s) {
    switch (s) {
    case HerbLeaf::Frond: return 0.42f;
    case HerbLeaf::Simple: return 0.62f;
    case HerbLeaf::Strap: return 0.7f;
    case HerbLeaf::Trifoliate: return 0.55f;
    case HerbLeaf::Lobed: return 0.4f;
    case HerbLeaf::Ribbed: return 0.66f;
    }
    return 0.6f;
}

// A bloom's face (m^2): what it shows the eye from above and the wind.
float bloomFace(const HerbSpecies& h) {
    const float s = h.bloomSize, r = 0.5f * s;
    switch (h.bloomForm) {
    case HerbBloom::None: return 0.0f;
    case HerbBloom::Ray: return 0.8f * kPi * r * r;
    case HerbBloom::Cup: return 0.75f * kPi * r * r;
    case HerbBloom::Head: return kPi * r * r;
    case HerbBloom::Spike: return s * 0.22f * s;
    case HerbBloom::Umbel: return 0.7f * kPi * r * r;
    case HerbBloom::Bell: {
        // `bloomCount` bells up a raceme `bloomSize` long, each about 1.8
        // spacings long (as the renderer hangs them), 1.5 x 0.55 its size.
        const float n = static_cast<float>(std::max(h.bloomCount, 1u));
        const float bell = std::clamp(1.8f * s / n, 0.012f, 0.05f);
        return n * 0.8f * bell * bell;
    }
    }
    return 0.0f;
}

float leafFace(const HerbSpecies& h) {
    return h.leafLength * h.leafLength * h.leafWidth * (1.0f - h.stalk) * leafFill(h.leafShape);
}

// A cantilever of base diameter `d`, length `L`, carrying `mass` kg of what
// it holds (in its mode: as phi^2 weighs it) and `area` m^2 of face (as phi
// weighs it) at the wind's share `wind` of the reference; `sideways` a
// stalk standing out from upright, so it holds its own weight up.
Cantilever herbCantilever(float d, float L, float mass, float area, float wind, bool sideways) {
    Cantilever c;
    d = std::max(d, 3.0e-4f);
    c.length = std::max(L, 0.005f);
    const float d2 = d * d;
    const float beam = (33.0f / 140.0f) * kHerbDensity * kPi * 0.25f * d2 * c.length;
    float k = 3.0f * kHerbModulus * (kPi * d2 * d2 / 64.0f) / (c.length * c.length * c.length);
    // A stalk held out sideways is at least stiff enough to hold up its
    // weight (to kSwaySagLimit of its length): what keeps a frond arched,
    // not lying on the ground.
    if (sideways) k = std::max(k, 9.81f * (mass * 1.6f + beam) / (kSwaySagLimit * c.length));
    const float m = beam + mass;
    c.freq = std::clamp(std::sqrt(k / std::max(m, 1e-6f)) / (2.0f * kPi), 0.3f, 12.0f);
    const float U = kSwayReferenceWind * wind;
    const float F = 0.5f * kSwayAirDensity * kHerbDrag * area * U * U;
    c.sway = std::clamp(F / (k * c.length), 0.0f, 0.6f);
    return c;
}

std::vector<HerbSpecies> makePresets() {
    std::vector<HerbSpecies> out;
    auto add = [&](HerbSpecies h) {
        herbSway(h);
        out.push_back(std::move(h));
    };

    // --- Ferns -----------------------------------------------------------

    {   // Bracken: one tall frond a plant from a creeping rhizome, a bare
        // stipe holding a broad triangular blade, thrice cut, out near flat;
        // in great colonies at woodland edges and open woods.
        HerbSpecies h;
        h.name = "bracken";
        h.habit = HerbHabit::Rosette;
        h.height = 1.2f;
        h.spread = 0.5f;
        h.spacing = 0.45f;   // the rhizome sends up a frond every half metre
        h.leaves = 2;
        h.leafShape = HerbLeaf::Frond;
        h.leafLength = 1.5f;
        h.leafWidth = 0.62f;
        h.leafTip = 1.0f;
        h.pinnae = 11;
        h.lobes = 0.85f;
        h.stalk = 0.45f;
        h.rise = 0.12f;
        h.arch = 1.25f;
        h.leafColor = rgb(0.075f, 0.13f, 0.03f);
        h.stemColor = rgb(0.09f, 0.1f, 0.035f);
        h.ribDiameter = 0.007f;
        h.shade = 0.4f;
        h.shadeRange = 0.4f;
        h.abundance = 1.0f;
        h.patch = 14.0f;
        h.patchCover = 0.4f;
        add(h);
    }
    {   // Lady fern: a shuttlecock of soft, bright, twice-cut fronds, tapering
        // to both ends, arching out from a crown; damp shade.
        HerbSpecies h;
        h.name = "ladyfern";
        h.habit = HerbHabit::Rosette;
        h.height = 0.7f;
        h.spread = 0.5f;
        h.leaves = 11;
        h.leafShape = HerbLeaf::Frond;
        h.leafLength = 0.85f;
        h.leafWidth = 0.3f;
        h.leafTip = 0.0f;
        h.pinnae = 16;
        h.lobes = 0.6f;
        h.stalk = 0.18f;
        h.rise = 0.4f;
        h.arch = 0.9f;
        h.leafColor = rgb(0.08f, 0.16f, 0.035f);
        h.stemColor = rgb(0.1f, 0.12f, 0.04f);
        h.ribDiameter = 0.004f;
        h.shade = 0.75f;
        h.shadeRange = 0.3f;
        h.abundance = 0.9f;
        h.patch = 6.0f;
        h.patchCover = 0.4f;
        add(h);
    }

    // --- Wildflowers -----------------------------------------------------

    {   // Oxeye daisy: a rosette of dark, round-toothed leaves; stiff stems
        // each holding one white-rayed head with a yellow disc to the sun.
        HerbSpecies h;
        h.name = "daisy";
        h.habit = HerbHabit::Rosette;
        h.height = 0.5f;
        h.spread = 0.12f;
        h.leaves = 7;
        h.stems = 5;
        h.leafShape = HerbLeaf::Simple;
        h.leafLength = 0.11f;
        h.leafWidth = 0.32f;
        h.leafTip = 1.0f;
        h.teeth = 5;
        h.toothDepth = 0.18f;
        h.stalk = 0.35f;
        h.rise = 1.0f;
        h.arch = 0.4f;
        h.leafColor = rgb(0.045f, 0.095f, 0.025f);
        h.stemColor = rgb(0.06f, 0.1f, 0.03f);
        h.stemDiameter = 0.003f;
        h.bloomForm = HerbBloom::Ray;
        h.bloomSize = 0.05f;
        h.petals = 22;
        h.bloomColor = rgb(0.82f, 0.82f, 0.78f);
        h.centreColor = rgb(0.8f, 0.52f, 0.03f);
        h.nod = 0.05f;
        h.shade = 0.0f;
        h.shadeRange = 0.25f;
        h.abundance = 1.0f;
        h.patch = 6.0f;
        h.patchCover = 0.55f;
        add(h);
    }
    {   // Field poppy: grey-green, deeply lobed leaves; hairy stems with four
        // broad, crumpled scarlet petals round a black eye, nodding a little.
        HerbSpecies h;
        h.name = "poppy";
        h.habit = HerbHabit::Rosette;
        h.height = 0.5f;
        h.spread = 0.14f;
        h.leaves = 5;
        h.stems = 3;
        h.leafShape = HerbLeaf::Lobed;
        h.leafLength = 0.14f;
        h.leafWidth = 0.38f;
        h.pinnae = 4;
        h.lobes = 0.7f;
        h.stalk = 0.2f;
        h.rise = 0.9f;
        h.arch = 0.5f;
        h.leafColor = rgb(0.06f, 0.1f, 0.04f);
        h.stemColor = rgb(0.07f, 0.1f, 0.04f);
        h.stemDiameter = 0.0025f;
        h.bloomForm = HerbBloom::Cup;
        h.bloomSize = 0.075f;
        h.petals = 4;
        h.bloomColor = rgb(0.72f, 0.035f, 0.015f);
        h.centreColor = rgb(0.015f, 0.012f, 0.012f);
        h.nod = 0.25f;
        h.shade = 0.0f;
        h.shadeRange = 0.2f;
        h.abundance = 0.85f;
        h.patch = 5.0f;
        h.patchCover = 0.45f;
        add(h);
    }
    {   // Foxglove: a rosette of big, soft, veined leaves and one tall spike
        // of purple bells hanging to one side; clearings and wood edges.
        HerbSpecies h;
        h.name = "foxglove";
        h.habit = HerbHabit::Rosette;
        h.height = 1.2f;
        h.spread = 0.3f;
        h.leaves = 8;
        h.stems = 1;
        h.leafShape = HerbLeaf::Simple;
        h.leafLength = 0.3f;
        h.leafWidth = 0.3f;
        h.leafTip = 0.3f;
        h.teeth = 10;
        h.toothDepth = 0.05f;
        h.stalk = 0.15f;
        h.rise = 1.1f;
        h.arch = 0.5f;
        h.leafColor = rgb(0.055f, 0.1f, 0.035f);
        h.stemColor = rgb(0.07f, 0.1f, 0.04f);
        h.stemDiameter = 0.011f;   // a stout stem at its base
        h.bloomForm = HerbBloom::Bell;
        h.bloomSize = 0.5f;
        h.bloomCount = 17;
        h.bloomColor = rgb(0.46f, 0.11f, 0.33f);
        h.centreColor = rgb(0.75f, 0.68f, 0.7f);
        h.nod = 0.0f;
        h.shade = 0.35f;
        h.shadeRange = 0.25f;
        h.abundance = 0.45f;
        h.patch = 7.0f;
        h.patchCover = 0.25f;
        add(h);
    }
    {   // Bluebell: glossy strap leaves and an arching stalk drooping at its
        // tip, its violet bells hanging to one side; carpets under trees.
        HerbSpecies h;
        h.name = "bluebell";
        h.habit = HerbHabit::Rosette;
        h.height = 0.35f;
        h.spread = 0.1f;
        h.leaves = 5;
        h.stems = 2;
        h.leafShape = HerbLeaf::Strap;
        h.leafLength = 0.3f;
        h.leafWidth = 0.05f;
        h.leafTip = 0.4f;
        h.stalk = 0.0f;
        h.rise = 0.45f;
        h.arch = 1.1f;
        h.leafColor = rgb(0.035f, 0.085f, 0.03f);
        h.gloss = 0.6f;
        h.stemColor = rgb(0.06f, 0.1f, 0.05f);
        h.stemDiameter = 0.0035f;
        h.bloomForm = HerbBloom::Bell;
        h.bloomSize = 0.12f;
        h.bloomCount = 8;
        h.bloomColor = rgb(0.12f, 0.11f, 0.52f);
        h.centreColor = rgb(0.5f, 0.5f, 0.75f);
        h.nod = 0.8f;
        h.shade = 0.6f;
        h.shadeRange = 0.25f;
        h.abundance = 0.8f;
        h.patch = 9.0f;
        h.patchCover = 0.35f;
        add(h);
    }
    {   // Yarrow: feathery, finely cut leaves and stiff stems topped by a flat
        // umbel of tiny white florets.
        HerbSpecies h;
        h.name = "yarrow";
        h.habit = HerbHabit::Rosette;
        h.height = 0.55f;
        h.spread = 0.12f;
        h.leaves = 6;
        h.stems = 2;
        h.leafShape = HerbLeaf::Lobed;
        h.leafLength = 0.15f;
        h.leafWidth = 0.22f;
        h.pinnae = 12;
        h.lobes = 0.95f;
        h.stalk = 0.1f;
        h.rise = 0.7f;
        h.arch = 0.6f;
        h.leafColor = rgb(0.055f, 0.095f, 0.04f);
        h.stemColor = rgb(0.07f, 0.09f, 0.04f);
        h.stemDiameter = 0.0035f;
        h.bloomForm = HerbBloom::Umbel;
        h.bloomSize = 0.08f;
        h.bloomColor = rgb(0.78f, 0.76f, 0.7f);
        h.centreColor = rgb(0.6f, 0.55f, 0.4f);
        h.shade = 0.05f;
        h.shadeRange = 0.25f;
        h.abundance = 0.8f;
        h.patch = 5.0f;
        h.patchCover = 0.45f;
        add(h);
    }

    // --- Forbs and ground cover ------------------------------------------

    {   // Stinging nettle: upright leafy stems, the leaves in opposite pairs,
        // heart-based and coarsely toothed, dark; rich soil at edges.
        HerbSpecies h;
        h.name = "nettle";
        h.habit = HerbHabit::Leafy;
        h.height = 0.9f;
        h.spread = 0.15f;
        h.leaves = 13;   // pairs all the way up its one stem (a patch is many plants)
        h.stems = 1;
        h.leafShape = HerbLeaf::Simple;
        h.leafLength = 0.13f;
        h.leafWidth = 0.55f;
        h.leafTip = 0.0f;
        h.teeth = 9;
        h.toothDepth = 0.2f;
        h.stalk = 0.2f;
        h.rise = 1.0f;
        h.arch = 0.5f;
        h.leafColor = rgb(0.035f, 0.075f, 0.02f);
        h.stemColor = rgb(0.06f, 0.08f, 0.03f);
        h.stemDiameter = 0.006f;
        h.shade = 0.3f;
        h.shadeRange = 0.3f;
        h.abundance = 0.6f;
        h.patch = 4.0f;
        h.patchCover = 0.3f;
        add(h);
    }
    {   // Broadleaf plantain: a flat rosette of broad, ribbed leaves on
        // stalks, slim brown spikes of tiny florets standing over it.
        HerbSpecies h;
        h.name = "plantain";
        h.habit = HerbHabit::Rosette;
        h.height = 0.25f;
        h.spread = 0.14f;
        h.leaves = 7;
        h.stems = 3;
        h.leafShape = HerbLeaf::Ribbed;
        h.leafLength = 0.16f;
        h.leafWidth = 0.55f;
        h.leafTip = 0.6f;
        h.stalk = 0.3f;
        h.rise = 0.85f;   // the leaves lift off the ground, their tips curving down
        h.arch = 0.45f;
        h.leafColor = rgb(0.05f, 0.095f, 0.03f);
        h.stemColor = rgb(0.07f, 0.09f, 0.04f);
        h.stemDiameter = 0.0025f;
        h.bloomForm = HerbBloom::Spike;
        h.bloomSize = 0.09f;
        h.bloomColor = rgb(0.13f, 0.11f, 0.06f);
        h.centreColor = rgb(0.55f, 0.5f, 0.35f);
        h.shade = 0.1f;
        h.shadeRange = 0.3f;
        h.abundance = 0.6f;
        h.patch = 4.0f;
        h.patchCover = 0.4f;
        add(h);
    }
    {   // White clover: a carpet of three round leaflets on creeping stalks,
        // round white heads on short stalks over it.
        HerbSpecies h;
        h.name = "clover";
        h.habit = HerbHabit::Carpet;
        h.height = 0.14f;
        h.spread = 0.2f;
        h.leaves = 10;
        h.stems = 2;
        h.leafShape = HerbLeaf::Trifoliate;
        h.leafLength = 0.09f;
        h.leafWidth = 0.6f;
        h.leafTip = 1.0f;
        h.stalk = 0.6f;
        h.rise = 0.35f;
        h.arch = 1.2f;
        h.leafColor = rgb(0.05f, 0.11f, 0.03f);
        h.stemColor = rgb(0.08f, 0.11f, 0.05f);
        h.stemDiameter = 0.0018f;
        h.bloomForm = HerbBloom::Head;
        h.bloomSize = 0.022f;
        h.bloomColor = rgb(0.75f, 0.72f, 0.66f);
        h.centreColor = rgb(0.5f, 0.38f, 0.38f);
        h.shade = 0.05f;
        h.shadeRange = 0.25f;
        h.abundance = 0.8f;
        h.patch = 3.0f;
        h.patchCover = 0.45f;
        add(h);
    }
    {   // Wood sorrel: a carpet of bright, folded, heart-notched leaflets in
        // threes; small white cups veined pink; deep shade.
        HerbSpecies h;
        h.name = "woodsorrel";
        h.habit = HerbHabit::Carpet;
        h.height = 0.1f;
        h.spread = 0.2f;
        h.leaves = 11;
        h.stems = 1;
        h.leafShape = HerbLeaf::Trifoliate;
        h.leafLength = 0.08f;
        h.leafWidth = 0.65f;
        h.leafTip = 0.0f;
        h.stalk = 0.6f;
        h.rise = 0.3f;
        h.arch = 1.2f;
        h.leafColor = rgb(0.075f, 0.15f, 0.035f);
        h.stemColor = rgb(0.12f, 0.1f, 0.06f);
        h.stemDiameter = 0.0015f;
        h.bloomForm = HerbBloom::Cup;
        h.bloomSize = 0.018f;
        h.petals = 5;
        h.bloomColor = rgb(0.78f, 0.72f, 0.72f);
        h.centreColor = rgb(0.75f, 0.62f, 0.15f);
        h.shade = 0.85f;
        h.shadeRange = 0.2f;
        h.abundance = 1.0f;
        h.patch = 3.0f;
        h.patchCover = 0.5f;
        add(h);
    }
    return out;
}

const std::vector<HerbSpecies>& presets() {
    static const std::vector<HerbSpecies> p = makePresets();
    return p;
}

} // namespace

const std::vector<std::string>& herbPresetNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const HerbSpecies& h : presets()) n.push_back(h.name);
        return n;
    }();
    return names;
}

std::optional<HerbSpecies> herbPreset(std::string_view name) {
    for (const HerbSpecies& h : presets())
        if (h.name == name) return h;
    return std::nullopt;
}

void herbSway(HerbSpecies& h) {
    // A stem: its own tissue and its bloom at the tip (phi 1), and for a
    // leafy stem its share of the leaves up it (about phi 0.5).
    const float bloom = bloomFace(h);
    const uint32_t stems = std::max(h.stems, 1u);
    float stemMass = kHerbBloomMass * bloom;
    float stemArea = bloom + h.stemDiameter * h.height * 0.5f;
    if (h.habit == HerbHabit::Leafy) {
        const float perStem = static_cast<float>(h.leaves) / static_cast<float>(stems);
        stemMass += 0.3f * perStem * kHerbLeafMass * leafFace(h);
        stemArea += 0.5f * perStem * leafFace(h);
    }
    h.stemSway = h.stems > 0 || h.habit == HerbHabit::Leafy
                     ? herbCantilever(h.stemDiameter, h.height, stemMass, stemArea, windShare(0.75f * h.height), false)
                     : Cantilever{};
    // A leaf (a frond): its blade, out along it (phi^2 ~ 0.3 of its mass in
    // the mode, phi ~ 0.5 of its face), on its midrib.
    const float face = leafFace(h);
    const float leafTop = h.habit == HerbHabit::Leafy ? 0.6f * h.height : h.leafLength * std::cos(std::min(h.rise, 1.4f));
    h.leafSway = herbCantilever(h.ribDiameter, h.leafLength, 0.3f * kHerbLeafMass * face, 0.5f * face,
                                windShare(std::max(leafTop, 0.05f)), h.rise > 0.25f);
}

Vec3 herbMeanColor(const HerbSpecies& h, float* bloomShare, float* cover) {
    // Seen from above: the leaves' face, foreshortened by how they stand (a
    // flat rosette shows all of it, an upright one less), over the patch
    // the plant spreads across.
    const float foot = kPi * std::max(h.spread, 0.03f) * std::max(h.spread, 0.03f);
    const float lean = 0.35f + 0.65f * std::sin(std::clamp(h.rise + 0.5f * h.arch, 0.0f, 1.5708f));
    const float perCard = h.habit == HerbHabit::Carpet ? static_cast<float>(kHerbCarpetCluster) : 1.0f;
    const float leaf = static_cast<float>(h.leaves) * perCard * leafFace(h) * lean;
    const float bloom = static_cast<float>(h.stems) * bloomFace(h);
    const float bs = bloom > 0.0f ? bloom / (bloom + leaf) : 0.0f;
    if (bloomShare) *bloomShare = bs;
    if (cover) *cover = std::clamp((leaf + bloom) / foot, 0.0f, 1.0f);
    return h.leafColor * (1.0f - bs) + h.bloomColor * bs;
}

float herbLeafTop(const HerbSpecies& h) {
    if (h.habit == HerbHabit::Leafy) return 0.7f * h.height;
    return std::min(h.height, h.leafLength * std::cos(std::min(h.rise, 1.5f)));
}

float herbSideCover(const HerbSpecies& h, float* bloomShare) {
    // Seen from the side, averaged round the plant (2 / pi of a face turned
    // at random about the vertical): a leaf shows its face as far as it
    // stands up from flat, a stem its length times its width, a raceme or a
    // spike its whole face, a flower's disc as far as it tilts from facing
    // the sky (about half a radian, more as it nods) and a head all of it.
    const float foot = kPi * std::max(h.spread, 0.03f) * std::max(h.spread, 0.03f);
    const float perCard = h.habit == HerbHabit::Carpet ? static_cast<float>(kHerbCarpetCluster) : 1.0f;
    const float tilt = std::clamp(h.rise + 0.5f * h.arch, 0.0f, 1.5708f);
    const float stand = h.habit == HerbHabit::Carpet ? 0.15f : std::max(std::cos(tilt), 0.2f);
    const float leaf = static_cast<float>(h.leaves) * perCard * leafFace(h) * 0.64f * stand;
    const float stems = static_cast<float>(h.stems) * h.height * h.stemDiameter;
    float bloomSide = 0.0f;
    switch (h.bloomForm) {
    case HerbBloom::None: break;
    case HerbBloom::Head:
    case HerbBloom::Spike:
    case HerbBloom::Bell: bloomSide = bloomFace(h); break;
    default: bloomSide = bloomFace(h) * 0.64f * std::sin(std::clamp(0.5f + 1.2f * h.nod, 0.0f, 1.5708f)); break;
    }
    const float bloom = static_cast<float>(h.stems) * bloomSide;
    const float all = leaf + stems + bloom;
    if (bloomShare) *bloomShare = all > 0.0f ? bloom / all : 0.0f;
    return all / foot;
}

void herbRecord(const HerbSpecies& h, float* o) {
    float bs = 0.0f, cover = 0.0f, sideBloom = 0.0f;
    herbMeanColor(h, &bs, &cover);
    const float side = herbSideCover(h, &sideBloom);
    const float spacing = h.spacing > 0.0f ? h.spacing : 1.6f * std::max(h.spread, 0.02f);
    const float v[kHerbRecordFloats] = {
        static_cast<float>(h.habit), h.height, h.spread, static_cast<float>(h.leaves),
        static_cast<float>(h.stems), static_cast<float>(h.leafShape), h.leafLength, h.leafWidth,
        h.leafTip, static_cast<float>(h.teeth), h.toothDepth, static_cast<float>(h.pinnae),
        h.lobes, h.stalk, h.rise, h.arch,
        h.leafColor.x, h.leafColor.y, h.leafColor.z, h.gloss,
        h.stemColor.x, h.stemColor.y, h.stemColor.z, h.stemDiameter,
        static_cast<float>(h.bloomForm), h.bloomSize, static_cast<float>(h.bloomCount), static_cast<float>(h.petals),
        h.bloomColor.x, h.bloomColor.y, h.bloomColor.z, h.nod,
        h.centreColor.x, h.centreColor.y, h.centreColor.z, h.ribDiameter,
        h.shade, h.shadeRange, h.abundance, h.patch,
        h.patchCover, bs, cover, 0.0f,
        h.stemSway.freq, h.stemSway.sway, h.leafSway.freq, h.leafSway.sway,
        spacing, side, sideBloom, herbLeafTop(h),
    };
    std::copy(std::begin(v), std::end(v), o);
}

std::vector<float> herbRecords(const std::vector<HerbSpecies>& hs) {
    std::vector<float> out(hs.size() * kHerbRecordFloats);
    for (size_t i = 0; i < hs.size(); ++i) herbRecord(hs[i], out.data() + i * kHerbRecordFloats);
    return out;
}

} // namespace broflora
