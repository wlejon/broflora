// The forest emit's blooms (broflora/forest.h): a flowering plant whose
// species bears blooms carries them per specimen, thinned apart, each on a
// segment of its own specimen at its twig's tip; a species with none (a fir)
// carries none.

#include "test_framework.h"

#include "broflora/forest.h"
#include "broflora/presets.h"

#include <cmath>
#include <vector>

using namespace broflora;

namespace {

WorldState bloomWorld() {
    WorldState world;
    world.rngState = 0xb100f;
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

}  // namespace

TEST(forest_blooms_a_flowering_hydrangea_bears_a_fir_none) {
    WorldState w = bloomWorld();
    const auto hy = addSpeciesPreset(w, "hydrangea");
    const auto fir = addSpeciesPreset(w, "fir");
    ASSERT(hy && fir, "the presets register");
    if (!hy || !fir) return;
    plantSpecies(w, *hy, {-5.0f, 0.0f, 0.0f});
    plantSpecies(w, *fir, {5.0f, 0.0f, 0.0f});
    // Past the species' flowering age (10).
    for (int t = 0; t < 600; ++t) step(w, 0.02f);
    ASSERT(w.plants.size() == 2, "both grow");
    if (w.plants.size() != 2) return;

    const ForestEmit f = emitForest(w);
    ASSERT(f.specimens.size() == 2 && f.blooms.size() % 8 == 0, "two specimens, whole bloom records");
    if (f.specimens.size() != 2) return;
    uint32_t total = 0;
    for (const ForestSpecimen& sp : f.specimens) {
        const Plant& plant = w.plants[sp.plant];
        total += sp.bloomCount;
        if (plant.species.bloomForm == static_cast<uint32_t>(BloomForm::None)) {
            ASSERT(sp.bloomCount == 0 && sp.bloomForm == 0, "a fir bears no blooms");
            continue;
        }
        ASSERT(plant.flowering, "the hydrangea flowers");
        ASSERT(sp.bloomForm == static_cast<uint32_t>(BloomForm::Head), "a hydrangea's blooms are heads");
        ASSERT(sp.bloomCount >= 3, "the hydrangea bears several heads");
        ASSERT(std::fabs(sp.bloomColor.x - plant.species.bloomColor.x) < 1e-6f && sp.bloomSize == plant.species.bloomSize,
               "the specimen carries the species' bloom colour and size");
        bool sized = true, onTwig = true, unit = true, apart = true;
        for (uint32_t i = sp.bloomStart; i < sp.bloomStart + sp.bloomCount; ++i) {
            const float* b = &f.blooms[i * 8];
            sized = sized && b[3] > 0.0f && b[3] <= sp.bloomSize + 1e-6f;
            unit = unit && std::fabs(std::sqrt(b[4] * b[4] + b[5] * b[5] + b[6] * b[6]) - 1.0f) < 1e-3f;
            // Its segment is the specimen's own, its tip where the bloom is
            // (positions relative to the specimen, as the segments'); a head
            // stands proud of it, by under half its size along its facing.
            const uint32_t g = static_cast<uint32_t>(b[7]);
            if (g >= sp.segCount) { onTwig = false; continue; }
            const float* s = &f.segments[(sp.segStart + g) * 8];
            const float dx = s[4] - b[0], dy = s[5] - b[1], dz = s[6] - b[2];
            const float reach = 0.45f * b[3] + 1e-3f;
            onTwig = onTwig && dx * dx + dy * dy + dz * dz < reach * reach;
            for (uint32_t j = sp.bloomStart; j < i; ++j) {
                const float* c = &f.blooms[j * 8];
                const float ex = c[0] - b[0], ey = c[1] - b[1], ez = c[2] - b[2];
                const float d = 0.9f * 0.5f * (b[3] + c[3]) * 0.999f;
                apart = apart && ex * ex + ey * ey + ez * ez >= d * d;
            }
        }
        ASSERT(sized, "each bloom is at most the species' size");
        ASSERT(unit, "each bloom faces a unit direction");
        ASSERT(onTwig, "each bloom stands at the tip of its own specimen's segment");
        ASSERT(apart, "no two heads crowd each other");
        ASSERT(sp.bloomCount < emitPlantBloomAnchors(plant).size(), "the anchors are thinned");
    }
    ASSERT(total * 8 == f.blooms.size(), "the specimens' runs cover the blooms");

    // The same world emits the same blooms.
    const ForestEmit again = emitForest(w);
    ASSERT(again.blooms == f.blooms, "the blooms are deterministic");
}
