// The species presets grow the habits they name: a conifer is a cone of
// boughs on one leader, a cypress a column, a shrub a low mound of stems.
// (tests/preset_report.cpp prints the same measures for tuning.)

#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using namespace broflora;

namespace {

WorldState openWorld() {
    WorldState world;
    world.rngState = 0xf0e57;
    world.climate.annualTempBase = 15.0f;
    world.climate.annualPrecip = 1100.0f;
    const float W = 30.0f;
    world.shadow.origin = {-W / 2, -2.0f, -W / 2};
    world.shadow.cellSize = 1.0f;
    world.shadow.width = uint32_t(W);
    world.shadow.height = 40;
    world.shadow.depth = uint32_t(W);
    world.shadow.qg.assign(size_t(world.shadow.width) * world.shadow.height * world.shadow.depth, 1.0f);
    return world;
}

struct Shape {
    float height = 0.0f;
    float spread[4] = {};   // farthest segment end from the axis, by height quarter
    float maxSpread = 0.0f;
    int baseStems = 0;      // segments leaving the ground at the root
};

Shape shapeOf(const Plant& p) {
    Shape s;
    const auto segs = emitPlantSegments(p);
    for (const auto& g : segs) s.height = std::max(s.height, std::max(g.from.y, g.to.y) - p.origin.y);
    for (const auto& g : segs) {
        for (const auto& q : {g.from, g.to}) {
            const int b = std::clamp(static_cast<int>((q.y - p.origin.y) / std::max(s.height, 1e-3f) * 4.0f), 0, 3);
            const float r = std::hypot(q.x - p.origin.x, q.z - p.origin.z);
            s.spread[b] = std::max(s.spread[b], r);
            s.maxSpread = std::max(s.maxSpread, r);
        }
        if (g.from.y - p.origin.y < 0.02f && std::hypot(g.from.x - p.origin.x, g.from.z - p.origin.z) < 0.05f &&
            g.to.y > g.from.y)
            ++s.baseStems;
    }
    return s;
}

Shape grow(const char* name, int ticks, WorldState* keep = nullptr) {
    WorldState world = openWorld();
    auto h = addSpeciesPreset(world, name);
    if (!h) return {};
    plantSpecies(world, *h, {0.0f, 0.0f, 0.0f});
    for (int t = 0; t < ticks; ++t) step(world, 0.02f);
    Shape s = world.plants.empty() ? Shape{} : shapeOf(world.plants[0]);
    if (keep) *keep = std::move(world);
    return s;
}

}  // namespace

TEST(presets_every_name_registers) {
    for (const auto& name : speciesPresetNames()) {
        WorldState world = openWorld();
        const auto h = addSpeciesPreset(world, name);
        ASSERT(h.has_value(), "a listed preset registers");
        ASSERT(!h->habit.empty() && h->habit == speciesPresetHabit(name), "it names its habit");
        const auto again = addSpeciesPreset(world, name);
        ASSERT(again && again->rootPrototype == h->rootPrototype &&
                   again->species.prototypeGroup == h->species.prototypeGroup &&
                   again->species.lateralGroup == h->species.lateralGroup,
               "registering again finds the same prototypes");
    }
    WorldState world = openWorld();
    ASSERT(!addSpeciesPreset(world, "baobab").has_value(), "an unknown name is refused");
    ASSERT(speciesPresetHabit("baobab").empty(), "and has no habit");
}

TEST(presets_species_keep_their_groups_apart) {
    WorldState world = openWorld();
    const auto fir = addSpeciesPreset(world, "fir");
    const auto box = addSpeciesPreset(world, "boxwood");
    ASSERT(fir && box, "both register");
    ASSERT(fir->species.prototypeGroup != fir->species.lateralGroup, "a conifer's boughs are their own group");
    ASSERT(box->species.prototypeGroup != fir->species.prototypeGroup &&
               box->species.prototypeGroup != fir->species.lateralGroup,
           "another species picks from its own modules");
    plantSpecies(world, *fir, {-6.0f, 0.0f, 0.0f});
    plantSpecies(world, *box, {6.0f, 0.0f, 0.0f});
    for (int t = 0; t < 400; ++t) step(world, 0.02f);
    ASSERT(world.plants.size() == 2, "both grow");
    for (const auto& p : world.plants) {
        const std::string prefix = p.origin.x < 0.0f ? "fir:" : "boxwood:";
        bool own = p.modules.size() > 4;
        for (const auto& m : p.modules)
            own = own && m.prototype && m.prototype->name && std::strncmp(m.prototype->name, prefix.c_str(), prefix.size()) == 0;
        ASSERT(own, "every module of a plant is its own species' module");
    }
}

TEST(presets_leaf_forms) {
    for (const auto& name : speciesPresetNames()) {
        WorldState world = openWorld();
        const auto h = addSpeciesPreset(world, name);
        const auto form = static_cast<LeafForm>(h->species.leafForm);
        if (name == "cypress" || name == "juniper") ASSERT(form == LeafForm::Scale, "cypresses bear scale leaves");
        else if (h->habit == "conifer") ASSERT(form == LeafForm::Needle, "the other conifers bear needles");
        else ASSERT(form == LeafForm::Broad, "broadleaves and shrubs bear broad leaves");
        ASSERT(h->species.barkColor.x > 0.0f || h->habit == "broadleaf", "conifers and shrubs carry a bark colour");
    }
}

TEST(presets_fir_grows_a_cone) {
    const Shape s = grow("fir", 800);
    ASSERT(s.height > 5.0f && s.height < 20.0f, "a fir grows a tree's height");
    ASSERT(s.spread[0] > 2.0f * s.spread[3], "its crown is widest at the foot, narrowest at the top");
    ASSERT(s.spread[0] >= s.spread[1] * 0.9f && s.spread[1] >= s.spread[2] * 0.9f, "and narrows going up");
    ASSERT(s.maxSpread < 0.6f * s.height, "a cone, not a dome");
}

TEST(presets_cypress_grows_a_column) {
    const Shape s = grow("cypress", 800);
    ASSERT(s.height > 5.0f, "a cypress grows tall");
    ASSERT(s.maxSpread < 0.2f * s.height, "and stays narrow");
}

TEST(presets_shrubs_grow_low_mounds_of_stems) {
    for (const char* name : {"boxwood", "hydrangea"}) {
        const Shape s = grow(name, 600);
        ASSERT(s.height > 0.5f && s.height < 3.0f, "a shrub stays low");
        ASSERT(s.maxSpread > 0.5f * s.height, "and spreads as wide as it is tall");
        ASSERT(s.baseStems >= 3, "from several stems at the root");
    }
}

TEST(presets_conifer_twigs_are_thin_and_its_trunk_stout) {
    // A conifer's many twigs are a few millimetres thick, so a renderer's
    // bark detail drops them with distance, while its boughs add up to a
    // stout trunk.
    WorldState world;
    grow("fir", 900, &world);
    const auto segs = emitPlantSegments(world.plants[0]);
    float trunk = 0.0f;
    size_t thin = 0;
    for (const auto& s : segs) {
        trunk = std::max(trunk, s.radius);
        if (s.radius < 0.006f) ++thin;
    }
    ASSERT(thin * 2 > segs.size(), ("most of a fir's shoots are twigs under 6 mm: " + std::to_string(thin) + " of " +
                                    std::to_string(segs.size())).c_str());
    ASSERT(trunk > 0.08f, ("and its trunk is stout: " + std::to_string(trunk)).c_str());
}

TEST(presets_newborn_modules_stand_on_their_plant) {
    // A module born in a step is drawn where it attaches even when the
    // world is emitted before the next step places it (not at the world's
    // origin): a plant away from the origin keeps every segment by it, at
    // whatever step the emit falls.
    WorldState world = openWorld();
    auto h = addSpeciesPreset(world, "fir");
    ASSERT(h.has_value(), "fir registers");
    const bromath::Vec3 at{9.0f, 0.0f, 6.0f};
    plantSpecies(world, *h, at);
    int stray = 0, steps = 0;
    for (int t = 0; t < 420; ++t) {
        step(world, 0.02f);
        if (t < 300) continue;
        ++steps;
        const Plant& p = world.plants[0];
        float height = 0.0f;
        const auto segs = emitPlantSegments(p);
        for (const auto& g : segs) height = std::max(height, g.to.y - at.y);
        for (const auto& g : segs)
            for (const auto& q : {g.from, g.to})
                if (std::hypot(q.x - at.x, q.z - at.z) > std::max(2.0f, height)) ++stray;
    }
    ASSERT(steps > 100 && stray == 0, "no segment strays from its plant");
}

TEST(presets_ivy_climbs_its_host) {
    WorldState world = openWorld();
    auto oak = addSpeciesPreset(world, "oak");
    auto ivy = addSpeciesPreset(world, "ivy");
    ASSERT(oak && ivy, "both presets register");
    plantSpecies(world, *oak, {0.0f, 0.0f, 0.0f});
    plantSpecies(world, *ivy, {0.0f, 0.0f, 0.0f});
    for (int t = 0; t < 400; ++t) step(world, 0.02f);
    const Plant& host = world.plants[0];
    const Plant& vine = world.plants[1];
    ASSERT(vine.support.size() >= 8, "the ivy takes the oak's way up");
    const auto& sup = vine.support;
    // Every joint of the ivy lies on the oak's bark: within its stem, the
    // reach a side shoot is let stand off and a little more of the wood
    // under it (past the support's top it runs on along its last piece).
    const auto segs = emitPlantSegments(vine);
    ASSERT(!segs.empty(), "the ivy grows");
    float worst = 0.0f;
    for (const auto& g : segs) {
        for (const auto& q : {g.from, g.to}) {
            float best = 1e9f;
            for (size_t k = 0; k + 1 < sup.size() / 4; ++k) {
                const bromath::Vec3 a{sup[4 * k], sup[4 * k + 1], sup[4 * k + 2]};
                bromath::Vec3 b{sup[4 * k + 4], sup[4 * k + 5], sup[4 * k + 6]};
                // The last piece runs on past the top.
                if (k + 2 == sup.size() / 4) b = a + (b - a) * 100.0f;
                const bromath::Vec3 ab = b - a;
                const float t = std::clamp(bromath::vdot(q - a, ab) / std::max(bromath::vdot(ab, ab), 1e-9f), 0.0f, 1.0f);
                const float r = std::max(sup[4 * k + 3], sup[4 * k + 7]);
                best = std::min(best, bromath::vlen(q - (a + ab * t)) - r);
            }
            worst = std::max(worst, best);
        }
    }
    ASSERT(worst < segs.front().radius + 0.12f + 0.05f, "the ivy hugs the oak's bark");
    // And moves with it: its wood takes the oak's stem's sway.
    const auto hs = emitPlantSway(host), vs = emitPlantSway(vine);
    ASSERT(!vs.empty() && vs.back().trunk.freq == hs.front().trunk.freq && vs.back().height == hs.front().height,
           "the ivy sways as the oak's stem");
    ASSERT(vs.back().limb.sway == 0.0f, "with no limb of its own");
    // A climber's world round-trips.
    const auto bytes = writeWorld(world);
    WorldState back;
    ASSERT(readWorld(bytes.data(), bytes.size(), back) && writeWorld(back) == bytes, "a climber's world round-trips");
}

TEST(presets_world_round_trips) {
    WorldState a;
    grow("spruce", 200, &a);
    const auto bytes = writeWorld(a);
    ASSERT(!bytes.empty(), "a preset world writes");
    WorldState b;
    ASSERT(readWorld(bytes.data(), bytes.size(), b), "and reads back");
    ASSERT(writeWorld(b) == bytes, "to the same bytes");
    for (int t = 0; t < 50; ++t) {
        step(a, 0.02f);
        step(b, 0.02f);
    }
    ASSERT(writeWorld(a) == writeWorld(b), "and grows on exactly as the original");
}
