// bench_nursery — the step's cost on a tree nursery: 24 specimens of three
// habits grown 2,500 ticks of 0.02,
// timed per phase through the StepObserver, with a checksum of the grown
// world so a change to the step can be shown to grow the same plants.
//
//   broflora_bench_nursery [ticks=2500]
//
// Not a test: it prints timings. The checksum is printed for comparing two
// builds by hand; tests/test_regression.cpp holds the asserted goldens.

#include "broflora/broflora.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace broflora;
using Clock = std::chrono::steady_clock;

namespace {

BranchModulePrototype trunkPrototype() {
    BranchModulePrototype p;
    p.name = "trunk";
    const float H = 2.0f;
    const int n = 6;
    for (int i = 0; i <= n; ++i) {
        ModuleNode nd;
        nd.position = {0.03f * std::sin(i * 1.7f), H * i / n, 0.03f * std::cos(i * 2.3f)};
        nd.ageAtBirth = 0.03f * i;
        p.nodes.push_back(nd);
        if (i) p.edges.push_back({uint32_t(i - 1), uint32_t(i)});
    }
    for (int k = 0; k < 3; ++k) {
        const float a = k * 2.094f + 0.4f;
        ModuleNode nd;
        nd.position = {std::cos(a) * 0.25f, H + 0.15f, std::sin(a) * 0.25f};
        nd.ageAtBirth = 0.2f;
        nd.lengthMax = 0.8f;
        p.nodes.push_back(nd);
        p.edges.push_back({uint32_t(n), uint32_t(p.nodes.size() - 1)});
    }
    p.rootNode = 0;
    p.terminalNodes = {uint32_t(n), uint32_t(n + 1), uint32_t(n + 2), uint32_t(n + 3)};
    return p;
}

Species habit(int kind) {
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
    if (kind == 0) { s.growthScale = 2.5f; s.orthotropy = 0.55f; s.tropismG2 = 0.05f; }
    if (kind == 1) { s.growthScale = 2.0f; s.orthotropy = 0.7f;  s.tropismG2 = 0.03f; }
    if (kind == 2) { s.growthScale = 2.2f; s.orthotropy = 0.8f;  s.tropismG2 = 0.02f; s.apicalControl = 0.5f; }
    return s;
}

uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

}  // namespace

int main(int argc, char** argv) {
    const int ticks = argc > 1 ? std::atoi(argv[1]) : 2500;
    const int cols = 6, rows = 4;
    const float spacing = 9.0f;
    const float W = cols * spacing + 10, D = rows * spacing + 10;

    WorldState world;
    world.rngState = 0xf0e57;
    world.climate.annualTempBase = 15.0f;
    world.climate.annualPrecip = 1100.0f;
    world.shadow.origin = {-W / 2, -2.0f, -D / 2};
    world.shadow.cellSize = 1.0f;
    world.shadow.width = uint32_t(W);
    world.shadow.height = 26;
    world.shadow.depth = uint32_t(D);
    world.shadow.qg.assign(size_t(world.shadow.width) * world.shadow.height * world.shadow.depth, 1.0f);
    const uint32_t mono = addPrototype(world, monopodialLeaderModule(3, 0.7f));
    const uint32_t trunk = addPrototype(world, trunkPrototype());
    addVoronoiSite(world, mono, 0.5f, 0.3f);
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            Plant p;
            p.species = habit((i + 2 * j) % 3);
            p.origin = {(i - (cols - 1) / 2.0f) * spacing + 1.3f * std::sin(i * 2.1f + j), 0.0f,
                        (j - (rows - 1) / 2.0f) * spacing + 1.3f * std::cos(i * 1.3f + 2 * j)};
            p.effectiveRootVigorMax = p.species.rootVigorMax;
            BranchModuleInstance root;
            root.prototype = prototypeAt(world, trunk);
            root.vigor = p.species.minVigor * 2.0f;
            root.light = 1.0f;
            p.modules.push_back(root);
            addPlant(world, std::move(p));
        }
    }

    // Per phase: the time from the previous phase's callback to this one's.
    const char* names[] = {"index+light", "vigor", "development", "spawn", "senescence"};
    double phase[5] = {};
    Clock::time_point mark;
    auto lap = [&](int k) { const auto now = Clock::now(); phase[k] += ms(now - mark); mark = now; };
    StepObserver obs;
    obs.postLight = [&](const WorldState&) { lap(0); };
    obs.postVigor = [&](const WorldState&) { lap(1); };
    obs.postDevelopment = [&](const WorldState&) { lap(2); };
    obs.postSpawn = [&](const WorldState&) { lap(3); };
    obs.postSenescence = [&](const WorldState&) { lap(4); };

    const auto t0 = Clock::now();
    for (int t = 0; t < ticks; ++t) {
        mark = Clock::now();
        stepWithObserver(world, 0.02f, obs);
    }
    const double total = ms(Clock::now() - t0);

    size_t modules = 0;
    uint64_t h = 1469598103934665603ULL;
    for (const auto& pl : world.plants) {
        modules += pl.modules.size();
        for (const auto& m : pl.modules) {
            h = fnv(h, &m.worldPos, sizeof(m.worldPos));
            h = fnv(h, &m.vigor, sizeof(m.vigor));
            h = fnv(h, &m.light, sizeof(m.light));
            h = fnv(h, &m.age, sizeof(m.age));
            h = fnv(h, &m.bboxRadius, sizeof(m.bboxRadius));
        }
    }
    std::printf("%d ticks, %zu plants, %zu modules: %.0f ms (%.3f ms a tick)\n", ticks, world.plants.size(), modules,
                total, total / ticks);
    for (int k = 0; k < 5; ++k) std::printf("  %-12s %8.0f ms  %5.1f%%\n", names[k], phase[k], 100.0 * phase[k] / total);
    std::printf("checksum %016llx\n", static_cast<unsigned long long>(h));
    return 0;
}
