#include "test_framework.h"

#include <filesystem>
#include <string>

using namespace broflora;

namespace {

WorldState smallWorld() {
    WorldState world;
    world.rngState = 0x5eed;
    world.shadow.origin = {-12.0f, -1.0f, -12.0f};
    world.shadow.cellSize = 1.0f;
    world.shadow.width = 24;
    world.shadow.height = 16;
    world.shadow.depth = 24;
    world.shadow.qg.assign(24 * 16 * 24, 1.0f);
    std::string name = "leader";  // the prototype's name outlives this string only if interned
    BranchModulePrototype proto = monopodialLeaderModule(3, 0.7f);
    proto.name = internName(name);
    const uint32_t idx = addPrototype(world, proto);
    addVoronoiSite(world, idx, 0.5f, 0.3f);
    for (int i = 0; i < 4; ++i) {
        Plant p;
        p.species.growthScale = 2.0f;
        p.species.individualVariation = 0.25f;
        p.species.seedingRadius = 0.0f;
        p.origin = {static_cast<float>(i % 2) * 6.0f - 3.0f, 0.0f, static_cast<float>(i / 2) * 6.0f - 3.0f};
        p.effectiveRootVigorMax = p.species.rootVigorMax;
        BranchModuleInstance root;
        root.prototype = prototypeAt(world, idx);
        root.vigor = p.species.minVigor * 2.0f;
        p.modules.push_back(root);
        addPlant(world, std::move(p));
    }
    return world;
}

}  // namespace

TEST(world_io_round_trip_steps_identically) {
    WorldState a = smallWorld();
    for (int t = 0; t < 120; ++t) step(a, 0.05f);
    std::string err;
    const auto bytes = writeWorld(a, &err);
    ASSERT(!bytes.empty(), "writeWorld succeeds");
    WorldState b;
    ASSERT(readWorld(bytes.data(), bytes.size(), b, &err), "readWorld succeeds");
    ASSERT(writeWorld(b) == bytes, "a read world writes the same bytes");
    ASSERT(b.plants[0].modules[0].prototype == &b.prototypes[0], "instances point at the read world's prototypes");
    ASSERT(std::string(b.prototypes[0].name) == "leader", "prototype names survive");
    for (int t = 0; t < 60; ++t) {
        step(a, 0.05f);
        step(b, 0.05f);
    }
    ASSERT(writeWorld(a) == writeWorld(b), "the read world grows exactly as the original");
}

TEST(world_io_rejects_damaged_bytes) {
    WorldState a = smallWorld();
    for (int t = 0; t < 40; ++t) step(a, 0.05f);
    const auto bytes = writeWorld(a);
    WorldState b = smallWorld();
    const auto untouched = writeWorld(b);
    std::string err;
    for (size_t cut : {size_t(0), size_t(3), bytes.size() / 2, bytes.size() - 1}) {
        ASSERT(!readWorld(bytes.data(), cut, b, &err), "a truncated world is refused");
    }
    ASSERT(writeWorld(b) == untouched, "a refused read leaves the world alone");
    auto extra = bytes;
    extra.push_back(0);
    ASSERT(!readWorld(extra.data(), extra.size(), b, &err), "trailing bytes are refused");
}

TEST(world_io_grow_cached_matches_growing) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("broflora_grow_cache_test_" + std::string(simHash()));
    std::error_code ec;
    fs::remove_all(dir, ec);

    WorldState plain = smallWorld();
    for (int t = 0; t < 150; ++t) step(plain, 0.05f);
    const auto want = writeWorld(plain);

    WorldState miss = smallWorld();
    const GrowResult r1 = growCached(miss, 150, 0.05f, dir.string());
    ASSERT(!r1.cached && r1.error.empty(), "the first grow misses and writes the cache");
    ASSERT(writeWorld(miss) == want, "a missed grow is the plain grow");

    WorldState hit = smallWorld();
    const GrowResult r2 = growCached(hit, 150, 0.05f, dir.string());
    ASSERT(r2.cached && r2.key == r1.key, "the second grow is read from the cache");
    ASSERT(writeWorld(hit) == want, "the cached world is the grown world");

    WorldState other = smallWorld();
    const GrowResult r3 = growCached(other, 149, 0.05f, dir.string());
    ASSERT(!r3.cached && r3.key != r1.key, "another tick count is another key");
    WorldState seeded = smallWorld();
    seeded.rngState ^= 1;
    const GrowResult r4 = growCached(seeded, 150, 0.05f, dir.string());
    ASSERT(!r4.cached && r4.key != r1.key, "another world is another key");

    fs::remove_all(dir, ec);
}
