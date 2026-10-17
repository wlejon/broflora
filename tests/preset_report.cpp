// preset_report — grows each species preset (or the ones named) on its own
// for a while and prints its shape: height, crown spread by height band (a
// cone, a column, a mound), modules, segments and clumps. Not a test: the
// tool for tuning the presets' growth (tests/test_presets.cpp asserts the
// habits).
//
//   broflora_preset_report [ticks=2500] [name...]

#include "broflora/broflora.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace broflora;

namespace {

// `field=value` overrides on a preset's species, for tuning by hand.
float* speciesField(Species& s, const std::string& f) {
    struct F { const char* n; float Species::*p; };
    static const F fields[] = {
        {"apicalControl", &Species::apicalControl}, {"apicalControlMature", &Species::apicalControlMature},
        {"determinacy", &Species::determinacy}, {"shadeTolerance", &Species::shadeTolerance},
        {"tropismG1", &Species::tropismG1}, {"tropismG2", &Species::tropismG2},
        {"growthScale", &Species::growthScale}, {"moduleMatureAge", &Species::moduleMatureAge},
        {"pipeExp", &Species::pipeExp}, {"leafDiameter", &Species::leafDiameter},
        {"orthotropy", &Species::orthotropy}, {"lateralOrthotropy", &Species::lateralOrthotropy},
        {"lateralElevation", &Species::lateralElevation}, {"rootVigorMax", &Species::rootVigorMax},
        {"maxVigor", &Species::maxVigor}, {"minVigor", &Species::minVigor},
        {"clumpScale", &Species::clumpScale}, {"apicalDominance", &Species::apicalDominance},
        {"maxBranchOrder", &Species::maxBranchOrder}, {"crownSlope", &Species::crownSlope},
        {"crownBase", &Species::crownBase}, {"crownRadiusMax", &Species::crownRadiusMax}, {"crownLift", &Species::crownLift}, {"heightMax", &Species::heightMax},
        {"clumpFlat", &Species::clumpFlat}, {"clumpReach", &Species::clumpReach},
        {"clumpFill", &Species::clumpFill}, {"clumpInner", &Species::clumpInner}, {"clumpGap", &Species::clumpGap},
        {"distributionWeightTropism", &Species::distributionWeightTropism},
        {"distributionWeightCollisions", &Species::distributionWeightCollisions},
    };
    for (const auto& x : fields)
        if (f == x.n) return &(s.*(x.p));
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    int ticks = 2500;
    std::vector<std::string> names;
    std::vector<std::vector<std::string>> overrides;
    for (int i = 1; i < argc; ++i) {
        if (i == 1 && std::atoi(argv[1]) > 0) { ticks = std::atoi(argv[1]); continue; }
        const std::string a = argv[i];
        if (a.find('=') != std::string::npos && !names.empty()) { overrides.back().push_back(a); continue; }
        names.push_back(a);
        overrides.emplace_back();
    }
    if (names.empty()) {
        names = speciesPresetNames();
        overrides.assign(names.size(), {});
    }

    for (size_t ni = 0; ni < names.size(); ++ni) {
        const std::string& name = names[ni];
        const float W = 30.0f;
        WorldState world;
        world.rngState = 0xf0e57;
        world.climate.annualTempBase = 15.0f;
        world.climate.annualPrecip = 1100.0f;
        world.shadow.origin = {-W / 2, -2.0f, -W / 2};
        world.shadow.cellSize = 1.0f;
        world.shadow.width = uint32_t(W);
        world.shadow.height = 40;
        world.shadow.depth = uint32_t(W);
        world.shadow.qg.assign(size_t(world.shadow.width) * world.shadow.height * world.shadow.depth, 1.0f);
        auto h = addSpeciesPreset(world, name);
        if (!h) { std::printf("%s: unknown\n", name.c_str()); continue; }
        for (const auto& o : overrides[ni]) {
            const size_t eq = o.find('=');
            float* f = speciesField(h->species, o.substr(0, eq));
            if (f) *f = static_cast<float>(std::atof(o.c_str() + eq + 1));
            else std::printf("  (no field %s)\n", o.substr(0, eq).c_str());
        }
        plantSpecies(world, *h, {0.0f, 0.0f, 0.0f});
        const auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < ticks; ++t) step(world, 0.02f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (world.plants.empty()) { std::printf("%s: died\n", name.c_str()); continue; }
        const Plant& p = world.plants[0];
        const auto segs = emitPlantSegments(p);
        const auto clumps = emitPlantClumps(p);
        float top = 0.0f, trunk = 0.0f;
        for (const auto& s : segs) {
            top = std::max(top, std::max(s.from.y, s.to.y) - p.origin.y);
            trunk = std::max(trunk, s.radius);
        }
        // Spread by height band: the farthest segment end from the axis.
        constexpr int kBands = 8;
        float spread[kBands] = {};
        for (const auto& s : segs) {
            for (const auto& q : {s.from, s.to}) {
                const float y = q.y - p.origin.y;
                const int b = std::clamp(static_cast<int>(y / std::max(top, 1e-3f) * kBands), 0, kBands - 1);
                spread[b] = std::max(spread[b], std::hypot(q.x - p.origin.x, q.z - p.origin.z));
            }
        }
        float clumpLow = 1e9f, meanClump = 0.0f;
        for (const auto& c : clumps) { clumpLow = std::min(clumpLow, c.center.y - p.origin.y); meanClump += c.radius; }
        if (!clumps.empty()) meanClump /= static_cast<float>(clumps.size());
        std::printf("%-10s %-9s h %5.2f  trunk d %5.3f  modules %5zu  segs %6zu  clumps %4zu (r %.2f, from %.2f)  %6.0f ms\n",
                    name.c_str(), h->habit.c_str(), top, 2.0f * trunk, p.modules.size(), segs.size(), clumps.size(),
                    meanClump, clumps.empty() ? 0.0f : clumpLow, ms);
        std::printf("           spread by band (low to high):");
        for (float s : spread) std::printf(" %5.2f", s);
        std::printf("\n");
        if (std::getenv("REPORT_SWAY")) {
            // The wind's static bend at kSwayReferenceWind by height band:
            // the trunk's at the band, and the largest limb (+ twig) tip
            // deflection of the limbs leaving the trunk in it (metres), with
            // that limb's base diameter, length and frequency.
            const auto sw = emitPlantSway(p);
            auto bend = [](float s) { return s <= 1.0f ? s * s * (3.0f - s) * 0.5f : 1.0f + 1.5f * (s - 1.0f); };
            float trunkTip[kBands] = {}, limbTip[kBands] = {}, twigTip[kBands] = {}, limbD[kBands] = {},
                  limbL[kBands] = {}, limbF[kBands] = {};
            for (size_t i = 0; i < segs.size() && i < sw.size(); ++i) {
                const auto& s = sw[i];
                const float y = segs[i].to.y - p.origin.y;
                const int b = std::clamp(static_cast<int>(y / std::max(top, 1e-3f) * kBands), 0, kBands - 1);
                trunkTip[b] = std::max(trunkTip[b], s.trunk.sway * s.trunk.length * bend(std::clamp(y / s.height, 0.0f, 1.3f)));
                const bool limbStart = s.limb.sway > 0.0f && s.weightFrom == 0.0f;
                if (!limbStart) continue;
                const float yb = segs[i].from.y - p.origin.y;
                const int bb = std::clamp(static_cast<int>(yb / std::max(top, 1e-3f) * kBands), 0, kBands - 1);
                const float tip = s.limb.sway * s.limb.length;
                if (tip > limbTip[bb]) {
                    limbTip[bb] = tip;
                    twigTip[bb] = s.twig.sway * s.twig.length;
                    limbD[bb] = 2.0f * segs[i].radius;
                    limbL[bb] = s.limb.length;
                    limbF[bb] = s.limb.freq;
                }
            }
            std::printf("           trunk %.2f Hz sway %.4f\n", sw.empty() ? 0.0f : sw[0].trunk.freq,
                        sw.empty() ? 0.0f : sw[0].trunk.sway);
            for (int b = 0; b < kBands; ++b) {
                std::printf("           band %d: trunk %.3f m  limb tip %.3f m (d %.3f L %.2f f %.2f Hz)  twig %.3f m\n", b,
                            trunkTip[b], limbTip[b], limbD[b], limbL[b], limbF[b], twigTip[b]);
            }
        }
        if (std::getenv("REPORT_LEADER")) {
            // The leader chain from the root: each module's state and its
            // children's vigor.
            uint32_t i = 0;
            for (int depth = 0; depth < 200; ++depth) {
                const auto& m = p.modules[i];
                uint32_t next = UINT32_MAX;
                float latV = 0.0f, latL = 0.0f;
                int lat = 0;
                for (uint32_t j = i + 1; j < p.modules.size(); ++j) {
                    if (p.modules[j].parent != i) continue;
                    if (p.modules[j].isMainChild) next = j;
                    else { latV += p.modules[j].vigor; latL += p.modules[j].subtreeLight; ++lat; }
                }
                std::printf("    %3d y %5.2f age %6.2f vigor %6.3f light %5.3f qeff %5.3f sub %7.2f | %d laterals vigor %6.3f sub %7.2f\n",
                            depth, m.worldPos.y, m.age, m.vigor, m.lightExposure, m.light, m.subtreeLight, lat, latV,
                            latL);
                if (next == UINT32_MAX) break;
                i = next;
            }
        }
    }
    return 0;
}
