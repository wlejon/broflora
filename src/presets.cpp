#include "broflora/presets.h"

#include "broflora/mesh_emit.h"   // refreshSupports
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

// The broadleaf trees the demos grew before presets existed: a clear trunk,
// monopodial modules over it.
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
    s.rootFlare = 0.2f;
    s.branchCollar = 0.3f;
    s.barkFurrow = 0.2f;
    s.barkScale = 0.6f;
    s.barkMoss = 0.2f;
    // A dense mound: every shoot leaved, inside as well as out, the inner
    // shoots too (clumpInner), so the foliage runs to the ground over the
    // stems.
    s.clumpFill = 1.0f;
    s.clumpInner = 1.0f;
    return s;
}

// A palm's stem: a clear column `height` tall, leaning and curving a little,
// topped by a rosette of `fronds` terminals, the inner ones near upright and
// the outer ones level (the spear leaf at terminal 0, straight up).
BranchModulePrototype palmTrunkModule(float height, uint32_t fronds, const char* name) {
    BranchModulePrototype p;
    p.name = name;
    constexpr uint32_t n = 8;
    for (uint32_t i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        p.nodes.push_back({{0.25f * height * t * t * 0.3f, height * t, 0.02f * std::sin(t * 5.0f)}, 0.03f * i, 1.0f, 1.0f});
        if (i) p.edges.push_back({i - 1, i});
    }
    p.rootNode = 0;
    const Vec3 top = p.nodes[n].position;
    p.nodes.push_back({top + Vec3{0.0f, 0.2f, 0.0f}, 0.25f, 1.0f, 1.0f});
    p.edges.push_back({n, n + 1});
    p.terminalNodes.push_back(n + 1);
    for (uint32_t k = 0; k < fronds; ++k) {
        // A golden-angle spiral, each frond lower than the last.
        const float a = static_cast<float>(k) * 2.39996f;
        const float lift = 1.2f - 1.1f * static_cast<float>(k) / static_cast<float>(std::max(fronds - 1, 1u));
        const Vec3 d{std::cos(a) * std::cos(lift), std::sin(lift), std::sin(a) * std::cos(lift)};
        p.nodes.push_back({top + d * 0.15f, 0.25f, 1.0f, 1.0f});
        p.edges.push_back({n, static_cast<uint32_t>(p.nodes.size() - 1)});
        p.terminalNodes.push_back(static_cast<uint32_t>(p.nodes.size() - 1));
    }
    return p;
}

// A palm frond: one unbranched rachis `length` long, curving out from its
// stalk (no terminals: it never branches on).
BranchModulePrototype frondModule(float length, const char* name) {
    BranchModulePrototype p;
    p.name = name;
    constexpr uint32_t n = 8;
    for (uint32_t i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        p.nodes.push_back({{0.0f, length * t, 0.0f}, 0.04f * i, 1.0f, 1.0f});
        if (i) p.edges.push_back({i - 1, i});
    }
    p.rootNode = 0;
    return p;
}

// A columnar cactus's stem: a column `height` tall, its top the leader
// (terminal 0), with `arms` arms leaving its upper half level and turning
// straight up, each arm's end a terminal facing up.
BranchModulePrototype cactusTrunkModule(float height, uint32_t arms, const char* name) {
    BranchModulePrototype p;
    p.name = name;
    constexpr uint32_t n = 8;
    for (uint32_t i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        p.nodes.push_back({{0.0f, height * t, 0.0f}, 0.03f * i, 1.0f, 1.0f});
        if (i) p.edges.push_back({i - 1, i});
    }
    p.rootNode = 0;
    p.terminalNodes.push_back(n);
    for (uint32_t k = 0; k < arms; ++k) {
        const uint32_t at = 5 + (k * 3 % 3);   // nodes 5..7: the column's upper part
        const float a = static_cast<float>(k) * 2.39996f + 0.7f;
        const Vec3 out{std::cos(a), 0.0f, std::sin(a)};
        const Vec3 base = p.nodes[at].position;
        const float age = p.nodes[at].ageAtBirth + 0.1f;
        p.nodes.push_back({base + out * 0.6f + Vec3{0.0f, 0.05f, 0.0f}, age, 1.0f, 1.0f});
        p.edges.push_back({at, static_cast<uint32_t>(p.nodes.size() - 1)});
        p.nodes.push_back({base + out * 0.8f + Vec3{0.0f, 0.3f, 0.0f}, age + 0.05f, 1.0f, 1.0f});
        p.edges.push_back({static_cast<uint32_t>(p.nodes.size() - 2), static_cast<uint32_t>(p.nodes.size() - 1)});
        p.terminalNodes.push_back(static_cast<uint32_t>(p.nodes.size() - 1));
    }
    return p;
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
        } else {
            // Silver birch: slender and narrow, a light open crown of small
            // leaves on twigs that hang; smooth bark.
            s.growthScale = 2.0f; s.orthotropy = 0.85f; s.tropismG2 = 0.09f; s.apicalControl = 0.55f;
            s.leafSize = 0.6f; s.pipeExp = 2.4f;   // a slender stem
            bark(s, 0.15f, 0.7f, 0.1f, 0.3f);
        }
        return d;
    }
    // Weeping willow: a short stout trunk, a few big limbs rising and
    // arching over, and from them long thin shoots that hang to near the
    // ground under their leaves' weight (Species::sag), clothed along their
    // length in narrow, finely toothed, yellow-green leaves on short stalks.
    if (name == "willow") {
        d.habit = "broadleaf";
        d.species = broadleafBase();
        d.root = trunkModule(2.4f, 4);
        d.leader = monopodialLeaderModule(3, 0.7f);
        Species& s = d.species;
        s.growthScale = 2.6f; s.orthotropy = 0.6f; s.tropismG2 = 0.08f; s.apicalControl = 0.4f;
        s.apicalDominance = 0.35f;
        s.pipeExp = 2.3f;
        s.leafDiameter = 0.01f;   // thin, whippy shoots
        s.sag = 30.0f;
        s.clumpFill = 1.0f;
        s.clumpInner = 0.6f;
        s.clumpScale = 0.35f;   // short runs of leaves, following the whips
        s.leafSize = 1.2f;
        bark(s, 0.55f, 1.3f, 0.3f, 0.6f);   // deep, criss-crossing furrows
        return d;
    }
    // Palm: a clear, unbranched column topped by a rosette of long fronds
    // that never branch, arching over under their own weight (Species::sag),
    // each a rachis lined with long narrow leaflets.
    if (name == "palm") {
        d.habit = "broadleaf";
        d.species = broadleafBase();
        d.root = palmTrunkModule(5.0f, 14, "palm:root");
        d.leader = frondModule(3.2f, "palm:leader");
        Species& s = d.species;
        s.growthScale = 2.6f; s.orthotropy = 0.0f; s.tropismG2 = 0.0f; s.apicalControl = 0.4f;
        s.pipeExp = 1.3f;
        s.leafDiameter = 0.03f;   // a frond's stalk, tapering to a third
        s.sag = 10.0f;
        s.clumpFill = 1.0f;
        s.clumpInner = 1.0f;
        s.leafSize = 1.6f;
        bark(s, 0.2f, 0.5f, 0.0f, 0.4f);
        return d;
    }
    // Succulent (an agave): a stemless rosette of thick, fleshy, pointed
    // leaves, the inner near upright and the outer spreading, each drawn as
    // its own wood (a module that never branches, tapering to its point);
    // no clumps (every leaf past the clumps' limit).
    if (name == "succulent") {
        d.habit = "shrub";
        d.species = shrubBase();
        d.root = palmTrunkModule(0.06f, 22, "succulent:root");
        d.leader = frondModule(0.55f, "succulent:leader");
        Species& s = d.species;
        s.growthScale = 1.6f; s.orthotropy = 0.0f; s.tropismG2 = 0.0f; s.kink = 0.0f;
        s.pipeExp = 3.0f;
        s.leafDiameter = 0.08f;
        s.sag = 2.0f;
        s.clumpScale = 0.0f;   // leafless: its leaves are its wood
        s.clumpFill = 0.0f; s.clumpInner = 0.0f;
        bark(s, 0.0f, 1.0f, 0.0f, 0.0f);
        s.barkMoss = 0.0f;
        s.branchCollar = 0.0f;
        return d;
    }
    // Vine (a creeping one: ground ivy, a trailing groundcover): runners
    // fanned out level from the root, branching flat along the ground and
    // never rising past a hand's height, clothed along their length in
    // glossy, heart-shaped, dark leaves.
    if (name == "vine") {
        d.habit = "shrub";
        d.species = shrubBase();
        d.root = basalStemsModule(7, 1.0f, 0.12f, 1.0f, "vine:root");
        d.leader = plagiotropicModule(2, 0.8f, 0.5f, 0.35f, 1.0f, "vine:leader");
        Species& s = d.species;
        s.growthScale = 2.4f; s.orthotropy = 0.0f; s.tropismG2 = 0.0f; s.kink = 0.35f;
        s.lateralOrthotropy = 0.0f; s.lateralElevation = 0.0f;
        s.heightMax = 0.15f;
        s.leafDiameter = 0.006f;
        s.clumpFlat = 0.22f; s.clumpScale = 0.3f; s.clumpInner = 1.0f;
        s.leafSize = 0.5f;
        bark(s, 0.1f, 0.6f, 0.0f, 0.0f);
        return d;
    }
    // Ivy (a climber): planted where a tree stands, a few stems up its
    // trunk and its leading limbs (Species::climb), wound round them a
    // turn every eight metres, short side shoots hugging the bark, clothed
    // in the creeping vine's glossy dark leaves. Grown as a column; with
    // nothing to climb it stands as one.
    if (name == "ivy") {
        d.habit = "shrub";
        d.species = shrubBase();
        d.root = basalStemsModule(3, 0.2f, 1.2f, 1.0f, "ivy:root");
        d.leader = monopodialLeaderModule(2, 0.6f);
        Species& s = d.species;
        s.climb = 0.12f;
        s.growthScale = 2.4f; s.orthotropy = 1.0f; s.tropismG2 = 0.0f; s.kink = 0.3f;
        s.heightMax = 9.0f;
        s.pipeExp = 2.5f;
        s.leafDiameter = 0.006f;
        s.clumpFlat = 0.5f; s.clumpScale = 0.35f; s.clumpInner = 1.0f;
        s.leafSize = 0.6f;
        bark(s, 0.3f, 0.5f, 0.0f, 0.0f);
        return d;
    }
    // Cactus (a saguaro): a thick, ribbed green column whose few arms leave
    // it level and turn straight up; no leaves (its stems are all thicker
    // than any shoot that carries them).
    if (name == "cactus") {
        d.habit = "broadleaf";
        d.species = broadleafBase();
        d.root = cactusTrunkModule(2.2f, 3, "cactus:root");
        d.leader = straightModule();
        Species& s = d.species;
        s.growthScale = 1.6f; s.orthotropy = 1.0f; s.tropismG2 = 0.0f; s.apicalControl = 0.5f;
        s.apicalDominance = 1.0f;
        s.maxBranchOrder = 1.0f;   // the arms only lengthen
        s.heightMax = 5.0f;
        s.kink = 0.0f;
        s.pipeExp = 3.0f;
        s.leafDiameter = 0.3f;     // an arm's tip: no shoot thin enough to leaf
        bark(s, 0.9f, 2.6f, 0.0f, 0.0f);
        s.barkRibs = 1.0f;   // 18 deep ribs
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
            // Tufts at the boughs' ends with sky between them, no spire:
            // only the outermost shoots carry needles (clumpFill 0), so the
            // limbs inside the crown are bare.
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
        } else if (name == "fir") {
            // Silver fir: a regular cone of level boughs and flat sprays,
            // smooth grey bark.
            d.root = leaderWhorlModule(5, 0.1f, 1.0f, 0.25f, 1.0f, "fir:root");
            d.leader = leaderWhorlModule(5, 0.12f, 0.55f, 0.25f, 1.0f, "fir:leader");
            d.lateral = plagiotropicModule(2, 0.85f, 0.5f, 0.3f, 1.0f, "fir:bough");
            s.tropismG2 = 0.05f;
            bark(s, 0.06f, 1.2f, 0.4f, 0.5f);
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
        } else if (name == "cypress") {
            // Italian cypress: a dark column, every bough turned up tight
            // against the leader; scale-leaf sprays.
            d.root = leaderWhorlModule(6, 0.9f, 0.6f, 0.2f, 1.0f, "cypress:root");
            d.leader = leaderWhorlModule(6, 0.9f, 0.5f, 0.2f, 1.0f, "cypress:leader");
            d.lateral = plagiotropicModule(2, 0.45f, 0.4f, 0.22f, 1.0f, "cypress:bough");
            s.lateralElevation = 1.2f; s.lateralOrthotropy = 0.9f;
            s.crownSlope = 0.08f; s.crownBase = 0.5f; s.crownRadiusMax = 0.9f;
            s.tropismG2 = 0.01f;
            bark(s, 0.5f, 0.5f, 0.2f, 0.5f);
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
            bark(s, 0.75f, 0.35f, 0.25f, 0.4f);
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
            s.clumpScale = 0.85f;
            s.leafSize = 0.3f;   // small oval leaves
        } else if (name == "holly") {
            // Upright and dense.
            d.root = basalStemsModule(4, 0.3f, 0.45f, 1.0f, "holly:root");
            d.leader = scaledModule(whorlModule(3, 0.5f), 0.3f);
            s.orthotropy = 0.62f;
            s.leafSize = 0.55f;
        } else if (name == "hydrangea") {
            // A broad rounded mound of arching stems, big leaves.
            d.root = basalStemsModule(8, 0.7f, 0.3f, 1.0f, "hydrangea:root");
            d.leader = scaledModule(whorlModule(3, 0.75f), 0.3f);
            s.orthotropy = 0.45f;
            s.tropismG2 = 0.1f;
            s.leafSize = 1.1f;
        } else if (name == "hibiscus") {
            // Upright, open.
            d.root = basalStemsModule(4, 0.35f, 0.5f, 1.0f, "hibiscus:root");
            d.leader = scaledModule(whorlModule(3, 0.55f), 0.35f);
            s.orthotropy = 0.65f;
            s.leafSize = 0.8f;
        } else {
            // Lavender: a low cushion of upright shoots.
            d.root = basalStemsModule(8, 0.8f, 0.18f, 1.0f, "lavender:root");
            d.leader = scaledModule(whorlModule(4, 0.4f), 0.15f);
            s.orthotropy = 0.75f;
            // Fine soft shoots from a short woody base, not gnarled stems.
            s.leafDiameter = 0.006f; s.pipeExp = 2.6f;
            s.clumpScale = 0.7f;
            s.leafSize = 0.25f;
        }
        return d;
    }
    return std::nullopt;
}

}  // namespace

const std::vector<std::string>& speciesPresetNames() {
    static const std::vector<std::string> names = {
        "oak", "elm", "beech", "birch", "willow", "palm", "cactus",
        "pine", "spruce", "fir", "cedar", "redwood", "cypress", "juniper",
        "boxwood", "holly", "hydrangea", "hibiscus", "lavender", "succulent", "vine", "ivy",
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
    Plant& added = addPlant(world, std::move(p));
    // A climber takes its host's way up now, as it will each step.
    if (added.species.climb > 0.0f) refreshSupports(world);
    return added;
}

} // namespace broflora
