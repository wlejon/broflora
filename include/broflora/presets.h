#pragma once

// Species presets: the plants any app gets by default. A preset is a
// Species (growth, habit, bark, foliage and colour) plus the modules its
// axes grow from; `addSpeciesPreset` registers those modules in a world
// under habit groups of their own, so conifers, shrubs and broadleaf trees
// grow side by side in one world, each from its own modules:
//
//   broadleaf  oak (spreading), elm (vase), beech (tall), birch (slender, white bark): a clear trunk
//              topped by scaffold limbs, then monopodial modules; willow
//              (its young shoots hung by Species::sag); palm (a clear column
//              topped by a rosette of unbranched fronds, sagging into arches);
//              cactus (a saguaro: a ribbed green column, arms turning up, no
//              leaves)
//   conifer    pine, spruce, fir, cedar, redwood (needles), cypress,
//              juniper (scale leaves): a monopodial leader that tops each
//              internode with a whorl of boughs, the boughs branching flat
//              (plagiotropic) and drooping, level, rising or turning up
//              (columnar) as the species carries them
//   shrub      boxwood, holly, hydrangea, hibiscus, lavender: a clump of
//              stems from the ground, small dense modules over them;
//              succulent (an agave: a stemless rosette of thick pointed
//              leaves drawn as its wood, leafless otherwise); vine (a
//              creeping groundcover: level runners, glossy heart leaves);
//              ivy (a climber, Species::climb: planted where a tree
//              stands, its stems wound up the tree's trunk and leading
//              limbs, hugging the bark and swaying with it)
//
// What a renderer draws the foliage as (Species::leafForm), its colour and
// the bark's ride in the Species too; bro.flora's JS layer pairs each leaf
// form with its card and material.

#include "broflora/world.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace broflora {

/// A preset registered in a world: the species (its habit groups set) and
/// the root module a plant of it starts from.
struct SpeciesHandle {
    std::string name;
    std::string habit;           // "broadleaf", "conifer" or "shrub"
    Species species;
    uint32_t rootPrototype = 0;  // index into WorldState::prototypes
};

/// The built-in presets' names, broadleaf first, then conifers, then shrubs.
const std::vector<std::string>& speciesPresetNames();

/// Preset `name`'s habit ("broadleaf", "conifer", "shrub"), or empty.
std::string speciesPresetHabit(std::string_view name);

/// Register preset `name` in `world`: its modules and Voronoi sites under
/// new habit groups (once: a second call finds the first's modules by name
/// and returns the same handle). nullopt for an unknown name.
std::optional<SpeciesHandle> addSpeciesPreset(WorldState& world, std::string_view name);

/// A plant of `handle` standing at `origin` (its root module, as addPlant
/// takes it).
Plant& plantSpecies(WorldState& world, const SpeciesHandle& handle, bromath::Vec3 origin);

/// A broadleaf tree's root module: a clear stem `height` tall (slightly
/// kinked), its top the leader (terminal 0), with `scaffolds` (1..6) limbs
/// leaving there for the crown's modules.
BranchModulePrototype trunkModule(float height = 2.0f, uint32_t scaffolds = 3, const char* name = "trunk");

} // namespace broflora
