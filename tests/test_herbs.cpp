// The herb presets (broflora/herbs.h): each sways as its build says (a
// thicker stem is quicker and moves less, a long frond is slower), stands
// where its light suits it, and packs into the record the grass node reads.

#include "test_framework.h"

#include <cmath>
#include <cstdio>

using namespace broflora;

TEST(herb_presets_are_whole) {
    const auto& names = herbPresetNames();
    ASSERT(names.size() >= 11, "at least eleven herb presets");
    for (const std::string& n : names) {
        auto h = herbPreset(n);
        ASSERT(h.has_value(), "every named preset resolves");
        if (!h) continue;
        ASSERT(h->height > 0.0f && h->leaves > 0, "a herb has height and leaves");
        ASSERT(h->leafSway.freq >= 0.3f && h->leafSway.freq <= 12.0f, "a leaf rings at a plant's frequency");
        ASSERT(h->leafSway.sway > 0.0f && h->leafSway.sway <= 0.6f, "a leaf bends in the reference wind");
        if (h->stems > 0) ASSERT(h->stemSway.freq > 0.0f && h->stemSway.sway > 0.0f, "a stem sways");
        float bs = 0.0f, cover = 0.0f;
        const auto mean = herbMeanColor(*h, &bs, &cover);
        ASSERT(cover > 0.0f && cover <= 1.0f, "a herb covers some of its patch");
        ASSERT(mean.y > 0.0f, "a herb has a colour");
        if (h->bloomForm != HerbBloom::None) ASSERT(bs > 0.0f && bs < 1.0f, "a flowering herb's blooms show");
        std::printf("  %-10s stem %.2f Hz %.3f  leaf %.2f Hz %.3f  cover %.2f blooms %.2f\n", n.c_str(),
                    h->stemSway.freq, h->stemSway.sway, h->leafSway.freq, h->leafSway.sway, cover, bs);
    }
    ASSERT(!herbPreset("no such herb").has_value(), "an unknown name is nothing");
}

TEST(herb_sway_follows_stiffness) {
    HerbSpecies h = *herbPreset("daisy");
    const Cantilever base = h.stemSway;
    h.stemDiameter *= 2.0f;
    herbSway(h);
    ASSERT(h.stemSway.freq > base.freq, "a thicker stem rings faster");
    ASSERT(h.stemSway.sway < base.sway * 0.25f, "a stem twice as thick bends far less");
    h = *herbPreset("daisy");
    h.height *= 2.0f;
    herbSway(h);
    ASSERT(h.stemSway.freq < base.freq, "a longer stem rings slower");
    // A bracken's long frond is slower than a clover leaf.
    ASSERT(herbPreset("bracken")->leafSway.freq < herbPreset("clover")->leafSway.freq,
           "a long frond is slower than a small leaf");
}

TEST(herb_record_layout) {
    const HerbSpecies h = *herbPreset("foxglove");
    float r[kHerbRecordFloats];
    herbRecord(h, r);
    ASSERT(r[0] == static_cast<float>(HerbHabit::Rosette) && r[1] == h.height, "habit and height lead");
    ASSERT(r[24] == static_cast<float>(HerbBloom::Bell) && r[26] == static_cast<float>(h.bloomCount),
           "the bloom's form and count");
    ASSERT(r[44] == h.stemSway.freq && r[47] == h.leafSway.sway, "the sway closes the record");
    const auto all = herbRecords({h, *herbPreset("bracken")});
    ASSERT(all.size() == 2 * kHerbRecordFloats && all[kHerbRecordFloats + 5] == static_cast<float>(HerbLeaf::Frond),
           "records end to end");
}
