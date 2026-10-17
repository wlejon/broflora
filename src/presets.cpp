#include "broflora/presets.h"

#include "broflora/prototypes.h"
#include "broflora/world_io.h"   // internName

#include <algorithm>
#include <cmath>
#include <string>

namespace broflora {

using bromath::Vec3;

BranchModulePrototype trunkModule(float height, uint32_t scaffolds, const char* name) {
    scaffolds = std::clamp(scaffolds, 1u, 6u);
    height = std::max(height, 0.1f);
    BranchModulePrototype p;
    p.name = name;
    // {position, ageAtBirth, lengthMax, thickening}
    constexpr uint32_t n = 6;
    for (uint32_t i = 0; i <= n; ++i) {
        const float k = static_cast<float>(i);
        p.nodes.push_back({{0.03f * std::sin(k * 1.7f), height * k / static_cast<float>(n), 0.03f * std::cos(k * 2.3f)},
                           0.03f * k, 1.0f, 1.0f});
        if (i) p.edges.push_back({i - 1, i});
    }
    p.rootNode = 0;
    p.terminalNodes.push_back(n);
    for (uint32_t k = 0; k < scaffolds; ++k) {
        const float a = static_cast<float>(k) * bromath::TWO_PI / static_cast<float>(scaffolds) + 0.4f;
        p.nodes.push_back({{std::cos(a) * 0.25f, height + 0.15f, std::sin(a) * 0.25f}, 0.2f, 0.8f, 1.0f});
        p.edges.push_back({n, static_cast<uint32_t>(p.nodes.size() - 1)});
        p.terminalNodes.push_back(static_cast<uint32_t>(p.nodes.size() - 1));
    }
    return p;
}

namespace {

struct PresetDef {
    std::string habit;
    Species species;
    BranchModulePrototype root;
    BranchModulePrototype leader;
    BranchModulePrototype lateral;   // no nodes: the leader's group serves laterals too
};

Vec3 rgb(float r, float g, float b) { return {r, g, b}; }

// The broadleaf trees the forest and grove demos grew before presets
// existed: a clear trunk, monopodial modules over it.
Species broadleafBase() {
    Species s;
    s.pipeExp = 2.0f;
    s.leafDiameter = 0.028f;
    s.shadeTolerance = 0.35f;
    s.moduleMatureAge = 0.6f;
    s.rootVigorMax = 4.0f;
    s.apicalControl = 0.35f;
    s.apicalControlMature = 0.3f;
    s.individualVariation = 0.25f;
    s.maxAge = 400.0f;
    s.seedingRadius = 0.0f;
    s.leafForm = static_cast<uint32_t>(LeafForm::Broad);
    return s;
}

// A conifer: a strong leader (high apical control), straight up, whorls of
// boughs that branch flat.
Species coniferBase() {
    Species s;
    // A spray's twig is a few millimetres thick, and the many boughs add up
    // to a stout stem: thin tips under a low pipe exponent (a 12 m fir's
    // trunk ~0.25 m in radius, a bough at the stem ~3 cm, its twigs 4-7 mm).
    s.pipeExp = 1.75f;
    s.leafDiameter = 0.007f;
    s.shadeTolerance = 0.3f;
    s.moduleMatureAge = 0.5f;
    s.rootVigorMax = 4.0f;
    s.apicalControl = 0.75f;
    s.apicalControlMature = 0.7f;
    s.individualVariation = 0.2f;
    s.maxAge = 400.0f;
    s.seedingRadius = 0.0f;
    s.orthotropy = 1.0f;
    s.growthScale = 2.6f;
    s.tropismG2 = 0.06f;
    s.kink = 0.12f;
    s.leafForm = static_cast<uint32_t>(LeafForm::Needle);
    s.branchCollar = 0.35f;
    // The habit: the leader keeps half its share however big the boughs
    // grow; boughs branch twice (bough, spray) and no more; they hold to a
    // set angle (lateralElevation, here level) with a strong return; and the
    // crown's outline is a cone, boughs not budding past it.
    s.apicalControl = 0.65f;
    s.apicalControlMature = 0.65f;
    s.apicalDominance = 0.5f;
    s.rootVigorMax = 24.0f;
    s.maxBranchOrder = 2.0f;
    s.lateralOrthotropy = 0.7f;
    s.lateralElevation = 0.0f;
    s.crownSlope = 0.35f;
    s.crownBase = 0.8f;
    // Small clumps (an emit for needles packs them closer, flora.js's
    // form defaults): many sprays' worth along every bough, so the crown
    // is a dense cone, not a few tufts at the boughs' ends.
    s.clumpScale = 0.3f;
    // Flat pads along level boughs, with dark gaps between the tiers, and
    // small clumps on the short top boughs so the crown ends in a spire.
    s.clumpFlat = 0.42f;
    s.clumpReach = 0.6f;
    // Evergreen: every shoot clothed, the leader to its tip.
    s.clumpFill = 1.0f;
    return s;
}

// A shrub: many thin stems, small modules, shade tolerant.
Species shrubBase() {
    Species s;
    s.pipeExp = 2.0f;
    s.leafDiameter = 0.012f;
    s.shadeTolerance = 0.6f;
    s.moduleMatureAge = 0.45f;
    s.rootVigorMax = 3.0f;
    s.apicalControl = 0.4f;
    s.apicalControlMature = 0.35f;
    s.individualVariation = 0.25f;
    s.maxAge = 400.0f;
    s.seedingRadius = 0.0f;
    s.orthotropy = 0.5f;
    s.growthScale = 2.0f;
    s.tropismG2 = 0.06f;
    s.kink = 0.25f;
    s.leafForm = static_cast<uint32_t>(LeafForm::Broad);
    s.rootFlare = 0.2f;
    s.branchCollar = 0.3f;
    s.barkFurrow = 0.2f;
    s.barkScale = 0.6f;
    s.barkMoss = 0.2f;
    s.barkColor = rgb(0.19f, 0.16f, 0.12f);
    // A dense mound: every shoot leaved, inside as well as out, the inner
    // shoots too (clumpInner), so the foliage runs to the ground over the
    // stems.
    s.clumpFill = 1.0f;
    s.clumpInner = 1.0f;
    return s;
}

void bark(Species& s, float furrow, float scale, float moss, float flare) {
    s.barkFurrow = furrow;
    s.barkScale = scale;
    s.barkMoss = moss;
    s.rootFlare = flare;
}

std::optional<PresetDef> presetDef(std::string_view name) {
    PresetDef d;
    // --- broadleaf --------------------------------------------------------
    if (name == "oak" || name == "elm" || name == "beech" || name == "birch") {
        d.habit = "broadleaf";
        d.species = broadleafBase();
        d.root = trunkModule(2.0f, 3);
        d.leader = monopodialLeaderModule(3, 0.7f);
        Species& s = d.species;
        if (name == "oak") {           // spreading, round crown
            s.growthScale = 2.5f; s.orthotropy = 0.55f; s.tropismG2 = 0.05f;
        } else if (name == "elm") {    // vase
            s.growthScale = 2.0f; s.orthotropy = 0.7f; s.tropismG2 = 0.03f;
        } else if (name == "beech") {  // tall, narrower
            s.growthScale = 2.2f; s.orthotropy = 0.8f; s.tropismG2 = 0.02f; s.apicalControl = 0.5f;
            bark(s, 0.12f, 1.0f, 0.45f, 0.5f);   // smooth grey beech bark
            s.barkColor = rgb(0.30f, 0.29f, 0.26f);
        } else {
            // Silver birch: slender and narrow, a light open crown of small
            // bright leaves on twigs that hang; white bark.
            s.growthScale = 2.0f; s.orthotropy = 0.85f; s.tropismG2 = 0.09f; s.apicalControl = 0.55f;
            s.leafSize = 0.6f; s.pipeExp = 2.4f;   // a slender stem
            s.leafColor = rgb(0.08f, 0.155f, 0.04f);
            // Triangular-ovate leaves, double toothed.
            s.leafWidth = 0.65f; s.leafTeeth = 9.0f; s.leafToothDepth = 0.07f; s.leafGloss = 0.2f;
            bark(s, 0.15f, 0.7f, 0.1f, 0.3f);
            s.barkColor = rgb(0.72f, 0.70f, 0.65f);
        }
        return d;
    }
    // --- conifers ---------------------------------------------------------
    if (name == "pine" || name == "spruce" || name == "fir" || name == "cedar" || name == "redwood" ||
        name == "cypress" || name == "juniper") {
        d.habit = "conifer";
        d.species = coniferBase();
        Species& s = d.species;
        if (name == "pine") {
            // Scots pine: rising boughs in open whorls, the lower ones shed
            // (crownLift) so the crown sits on the top half of a bare trunk,
            // flattening once the leader stops (heightMax) and the upper
            // boughs fill in under it; long needles in tufts; red-brown
            // plated bark.
            d.root = leaderWhorlModule(4, 0.25f, 1.5f, 0.3f, 1.0f, "pine:root");
            d.leader = leaderWhorlModule(4, 0.3f, 0.8f, 0.3f, 1.0f, "pine:leader");
            d.lateral = plagiotropicModule(2, 0.7f, 0.7f, 0.35f, 1.0f, "pine:bough");
            s.apicalDominance = 0.35f;
            s.lateralElevation = 0.5f; s.lateralOrthotropy = 0.5f;
            // A wide cone outline (crownSlope) under an early stop
            // (heightMax): the crown is widest near its top, flat over it.
            s.crownSlope = 0.6f; s.crownBase = 2.5f; s.crownRadiusMax = 5.0f;
            s.crownLift = 0.55f; s.heightMax = 16.0f;
            // A straight, stout stem (a Scots pine of 17 m is about half a
            // metre across): a lower pipe exponent than the other conifers'.
            s.kink = 0.1f; s.individualVariation = 0.35f;
            s.pipeExp = 1.55f;
            s.tropismG2 = 0.04f;
            // Red-orange plates parted by narrower furrows than the
            // spruce's or the fir's (the upper stem's bark is thin).
            bark(s, 0.6f, 1.8f, 0.2f, 0.5f);
            s.barkColor = rgb(0.50f, 0.21f, 0.085f);
            s.leafColor = rgb(0.05f, 0.085f, 0.055f);
            // Tufts at the boughs' ends with sky between them, no spire:
            // only the outermost shoots carry needles (clumpFill 0), so the
            // limbs inside the crown are bare. Each tuft a brush of needles
            // radiating from its shoot's end, swept up.
            s.sprayBrush = 1.0f;
            s.clumpFlat = 0.4f; s.clumpReach = 0.0f; s.clumpScale = 0.6f;
            s.clumpFill = 0.0f; s.clumpGap = 1.3f;
        } else if (name == "spruce") {
            // Norway spruce: a narrow spire, boughs level then drooping, the
            // sprays hanging from them; dark; fine grey-brown scaly bark.
            d.root = leaderWhorlModule(5, 0.0f, 1.6f, 0.3f, 1.0f, "spruce:root");
            d.leader = leaderWhorlModule(5, 0.05f, 0.6f, 0.3f, 1.0f, "spruce:leader");
            d.lateral = plagiotropicModule(3, 0.8f, 0.55f, 0.32f, 1.0f, "spruce:bough");
            s.lateralElevation = -0.3f; s.lateralOrthotropy = 0.5f;
            s.crownSlope = 0.27f;
            s.tropismG2 = 0.1f;
            bark(s, 0.3f, 0.5f, 0.35f, 0.7f);
            s.barkColor = rgb(0.22f, 0.17f, 0.13f);
            s.leafColor = rgb(0.028f, 0.068f, 0.045f);
        } else if (name == "fir") {
            // Silver fir: a regular cone of level boughs and flat sprays,
            // smooth grey bark.
            d.root = leaderWhorlModule(5, 0.1f, 1.0f, 0.25f, 1.0f, "fir:root");
            d.leader = leaderWhorlModule(5, 0.12f, 0.55f, 0.25f, 1.0f, "fir:leader");
            d.lateral = plagiotropicModule(2, 0.85f, 0.5f, 0.3f, 1.0f, "fir:bough");
            s.tropismG2 = 0.05f;
            bark(s, 0.06f, 1.2f, 0.4f, 0.5f);
            s.barkColor = rgb(0.31f, 0.30f, 0.28f);
            s.leafColor = rgb(0.032f, 0.08f, 0.05f);
        } else if (name == "cedar") {
            // Cedar of Lebanon: broad flat shelves of long level boughs,
            // blue-green; dark fissured bark.
            d.root = leaderWhorlModule(4, 0.05f, 1.3f, 0.4f, 1.0f, "cedar:root");
            d.leader = leaderWhorlModule(4, 0.05f, 0.9f, 0.4f, 1.0f, "cedar:leader");
            d.lateral = plagiotropicModule(3, 0.95f, 0.8f, 0.5f, 1.0f, "cedar:bough");
            s.apicalDominance = 0.25f;
            s.lateralElevation = 0.15f; s.lateralOrthotropy = 0.8f;
            s.crownSlope = 0.8f; s.crownBase = 4.0f; s.crownRadiusMax = 7.0f;
            // The leader gives out early and the shelves spread under it:
            // broader than tall, a flat top.
            s.heightMax = 15.0f;
            s.tropismG2 = 0.03f;
            // Thin, wide plates on each bough, open sky between the shelves.
            s.clumpFlat = 0.3f; s.clumpScale = 0.45f;
            bark(s, 0.6f, 0.8f, 0.3f, 0.6f);
            s.barkColor = rgb(0.24f, 0.20f, 0.17f);
            s.leafColor = rgb(0.055f, 0.1f, 0.085f);
        } else if (name == "redwood") {
            // Coast redwood: very tall and narrow, a long clear trunk, short
            // boughs drooping then lifting; deep red fibrous bark, a big
            // flare at the foot.
            d.root = leaderWhorlModule(4, -0.05f, 2.2f, 0.3f, 1.0f, "redwood:root");
            d.leader = leaderWhorlModule(4, -0.05f, 0.8f, 0.3f, 1.0f, "redwood:leader");
            d.lateral = plagiotropicModule(2, 0.7f, 0.5f, 0.3f, 1.0f, "redwood:bough");
            // A leader far ahead of its short, near-level boughs, the lower
            // half of the trunk clear: a column, not a cone.
            s.apicalDominance = 0.8f;
            s.lateralElevation = -0.05f; s.lateralOrthotropy = 0.6f;
            s.crownSlope = 0.15f; s.crownBase = 0.6f; s.crownRadiusMax = 1.6f;
            s.crownLift = 0.45f;
            // A massive trunk for its boughs (a low pipe exponent): the
            // column stands on a pillar.
            s.pipeExp = 1.2f;
            s.growthScale = 3.0f;
            s.tropismG2 = 0.08f;
            bark(s, 1.0f, 0.55f, 0.15f, 1.3f);
            s.barkColor = rgb(0.36f, 0.15f, 0.08f);
            s.leafColor = rgb(0.05f, 0.1f, 0.045f);
        } else if (name == "cypress") {
            // Italian cypress: a dark column, every bough turned up tight
            // against the leader; scale-leaf sprays.
            d.root = leaderWhorlModule(6, 0.9f, 0.6f, 0.2f, 1.0f, "cypress:root");
            d.leader = leaderWhorlModule(6, 0.9f, 0.5f, 0.2f, 1.0f, "cypress:leader");
            d.lateral = plagiotropicModule(2, 0.45f, 0.4f, 0.22f, 1.0f, "cypress:bough");
            s.lateralElevation = 1.2f; s.lateralOrthotropy = 0.9f;
            s.crownSlope = 0.08f; s.crownBase = 0.5f; s.crownRadiusMax = 0.9f;
            s.tropismG2 = 0.01f;
            s.leafForm = static_cast<uint32_t>(LeafForm::Scale);
            bark(s, 0.5f, 0.5f, 0.2f, 0.5f);
            s.barkColor = rgb(0.24f, 0.18f, 0.13f);
            s.leafColor = rgb(0.035f, 0.075f, 0.035f);
            s.clumpScale = 0.2f;
            // Upright sprays packed round the leader to its tip: round
            // clumps, full size however close to the axis (a flame, not a
            // spire); packed and leaved to the core (clumpInner), many small
            // clumps, so the column is one smooth flame rather than lumps.
            s.clumpFlat = 1.0f; s.clumpReach = 0.0f;
            s.clumpScale = 0.16f; s.clumpInner = 0.8f;
        } else {
            // Juniper: a small tree of several stems, irregular, blue-grey
            // scale-leaf sprays; shreddy bark.
            d.root = basalStemsModule(3, 0.35f, 0.7f, 1.0f, "juniper:root");
            d.leader = leaderWhorlModule(4, 0.5f, 0.5f, 0.25f, 1.0f, "juniper:leader");
            d.lateral = plagiotropicModule(2, 0.55f, 0.45f, 0.3f, 1.0f, "juniper:bough");
            s.apicalControl = 0.5f; s.apicalControlMature = 0.5f;
            // Its leaders ahead of rising boughs: a cone, widest at the
            // foot.
            s.apicalDominance = 0.5f;
            s.orthotropy = 0.7f;
            s.lateralElevation = 0.2f; s.lateralOrthotropy = 0.5f;
            s.crownSlope = 0.35f; s.crownBase = 0.6f; s.crownRadiusMax = 2.2f;
            s.rootVigorMax = 10.0f;
            s.leafForm = static_cast<uint32_t>(LeafForm::Scale);
            bark(s, 0.75f, 0.35f, 0.25f, 0.4f);
            s.barkColor = rgb(0.30f, 0.21f, 0.16f);
            s.leafColor = rgb(0.06f, 0.09f, 0.07f);
            // Bushy: rounded tufts, no spire, filled to the core.
            s.clumpFlat = 0.8f; s.clumpReach = 0.0f;
            s.clumpInner = 1.0f;
        }
        return d;
    }
    // --- shrubs -----------------------------------------------------------
    if (name == "boxwood" || name == "holly" || name == "hydrangea" || name == "hibiscus" || name == "lavender") {
        d.habit = "shrub";
        d.species = shrubBase();
        Species& s = d.species;
        if (name == "boxwood") {
            // A dense, rounded mound of tiny dark leaves.
            d.root = basalStemsModule(7, 0.6f, 0.3f, 1.0f, "boxwood:root");
            d.leader = scaledModule(whorlModule(3, 0.7f), 0.25f);
            s.orthotropy = 0.45f;
            s.leafColor = rgb(0.035f, 0.08f, 0.025f);
            s.clumpScale = 0.85f;
            s.leafSize = 0.3f;
            // Small oval leaves, blunt and round-tipped, entire, with a
            // soft sheen.
            s.leafWidth = 0.6f; s.leafTip = 1.0f; s.leafGloss = 0.35f;
        } else if (name == "holly") {
            // Upright and dense, glossy dark green.
            d.root = basalStemsModule(4, 0.3f, 0.45f, 1.0f, "holly:root");
            d.leader = scaledModule(whorlModule(3, 0.5f), 0.3f);
            s.orthotropy = 0.62f;
            s.leafColor = rgb(0.02f, 0.05f, 0.022f);
            s.leafSize = 0.55f;
            // Stiff, waxy leaves edged with a few sharp spines.
            s.leafWidth = 0.55f; s.leafTip = 0.2f; s.leafTeeth = 3.0f; s.leafToothDepth = 0.32f;
            s.leafGloss = 0.9f;
        } else if (name == "hydrangea") {
            // A broad rounded mound of arching stems, big pale leaves.
            d.root = basalStemsModule(8, 0.7f, 0.3f, 1.0f, "hydrangea:root");
            d.leader = scaledModule(whorlModule(3, 0.75f), 0.3f);
            s.orthotropy = 0.45f;
            s.tropismG2 = 0.1f;
            s.leafColor = rgb(0.065f, 0.13f, 0.04f);
            s.leafSize = 1.1f;
            // Broad, ovate leaves drawn to a point, finely toothed, matte.
            s.leafWidth = 0.62f; s.leafTip = 0.0f; s.leafTeeth = 12.0f; s.leafToothDepth = 0.06f;
            s.leafGloss = 0.1f;
            // Mophead blooms: big round heads of pale blue florets.
            s.bloomForm = static_cast<uint32_t>(BloomForm::Head);
            s.bloomColor = rgb(0.42f, 0.5f, 0.85f);
            s.bloomSize = 0.24f;
        } else if (name == "hibiscus") {
            // Upright, open; mid-green.
            d.root = basalStemsModule(4, 0.35f, 0.5f, 1.0f, "hibiscus:root");
            d.leader = scaledModule(whorlModule(3, 0.55f), 0.35f);
            s.orthotropy = 0.65f;
            s.leafColor = rgb(0.06f, 0.125f, 0.035f);
            s.leafSize = 0.8f;
            // Glossy ovate leaves, coarsely toothed.
            s.leafWidth = 0.6f; s.leafTip = 0.1f; s.leafTeeth = 7.0f; s.leafToothDepth = 0.08f;
            s.leafGloss = 0.6f;
            // Large open red flowers.
            s.bloomForm = static_cast<uint32_t>(BloomForm::Flower);
            s.bloomColor = rgb(0.8f, 0.06f, 0.08f);
            s.bloomSize = 0.14f;
        } else {
            // Lavender: a low cushion of upright shoots, grey-green.
            d.root = basalStemsModule(8, 0.8f, 0.18f, 1.0f, "lavender:root");
            d.leader = scaledModule(whorlModule(4, 0.4f), 0.15f);
            s.orthotropy = 0.75f;
            // Fine soft shoots from a short woody base, not gnarled stems.
            s.leafDiameter = 0.006f; s.pipeExp = 2.6f;
            s.leafColor = rgb(0.20f, 0.24f, 0.16f);
            s.clumpScale = 0.7f;
            s.leafSize = 0.25f;
            // Purple spikes held above the cushion.
            s.bloomForm = static_cast<uint32_t>(BloomForm::Spike);
            s.bloomColor = rgb(0.3f, 0.16f, 0.55f);
            s.bloomSize = 0.12f;
        }
        return d;
    }
    return std::nullopt;
}

}  // namespace

const std::vector<std::string>& speciesPresetNames() {
    static const std::vector<std::string> names = {
        "oak", "elm", "beech", "birch",
        "pine", "spruce", "fir", "cedar", "redwood", "cypress", "juniper",
        "boxwood", "holly", "hydrangea", "hibiscus", "lavender",
    };
    return names;
}

std::string speciesPresetHabit(std::string_view name) {
    const auto d = presetDef(name);
    return d ? d->habit : std::string();
}

std::optional<SpeciesHandle> addSpeciesPreset(WorldState& world, std::string_view name) {
    auto d = presetDef(name);
    if (!d) return std::nullopt;
    SpeciesHandle h;
    h.name = std::string(name);
    h.habit = d->habit;
    h.species = d->species;

    // Registered already: its modules are named "<preset>:root" etc.
    const std::string rootName = h.name + ":root";
    for (uint32_t i = 0; i < world.prototypes.size(); ++i) {
        const char* n = world.prototypes[i].name;
        if (!n || rootName != n) continue;
        h.rootPrototype = i;
        const std::string leader = h.name + ":leader", lateral = h.name + ":lateral";
        for (const auto& site : world.voronoi) {
            if (site.prototypeIndex >= world.prototypes.size()) continue;
            const char* sn = world.prototypes[site.prototypeIndex].name;
            if (sn && leader == sn) h.species.prototypeGroup = site.group;
            if (sn && lateral == sn) h.species.lateralGroup = site.group;
        }
        if (h.species.lateralGroup == Species::kSameGroup) h.species.lateralGroup = h.species.prototypeGroup;
        return h;
    }

    d->root.name = internName(rootName);
    d->leader.name = internName(h.name + ":leader");
    h.rootPrototype = addPrototype(world, std::move(d->root));
    const uint32_t leaderGroup = nextPrototypeGroup(world);
    addVoronoiSite(world, addPrototype(world, std::move(d->leader)), 0.5f, 0.5f, leaderGroup);
    h.species.prototypeGroup = leaderGroup;
    h.species.lateralGroup = leaderGroup;
    if (!d->lateral.nodes.empty()) {
        d->lateral.name = internName(h.name + ":lateral");
        const uint32_t lateralGroup = nextPrototypeGroup(world);
        addVoronoiSite(world, addPrototype(world, std::move(d->lateral)), 0.5f, 0.5f, lateralGroup);
        h.species.lateralGroup = lateralGroup;
    }
    return h;
}

Plant& plantSpecies(WorldState& world, const SpeciesHandle& handle, Vec3 origin) {
    Plant p;
    p.species = handle.species;
    p.origin = origin;
    p.effectiveRootVigorMax = p.species.rootVigorMax;
    BranchModuleInstance root;
    root.prototype = prototypeAt(world, handle.rootPrototype);
    root.parent = UINT32_MAX;
    root.age = 0.0f;
    root.vigor = p.species.minVigor * 2.0f;
    root.light = 1.0f;
    if (root.prototype) p.modules.push_back(root);
    return addPlant(world, std::move(p));
}

} // namespace broflora
