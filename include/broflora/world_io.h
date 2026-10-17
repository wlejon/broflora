#pragma once

// World IO — a WorldState to bytes and back, and growing a world through a
// persistent cache of grown worlds.
//
// The bytes hold every field the step reads or writes (environment,
// prototypes, Voronoi sites, plants, rng, time); an instance's prototype
// pointer is stored as its index into `prototypes`. Reading them back gives
// a world that steps, emits and serialises exactly as the one written.
// The format is for caches, not archives: it is native-endian, and a
// build whose step source differs reads it as a miss rather than a world.

#include "broflora/world.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace broflora {

// The world's bytes. Fails (empty, with *err) only when an instance's
// prototype is not one of the world's own.
std::vector<uint8_t> writeWorld(const WorldState& world, std::string* err = nullptr);

// Replace `out` with the world in `data`. On failure (truncated, corrupt,
// or written by another format) `out` is untouched and *err says why.
bool readWorld(const uint8_t* data, size_t size, WorldState& out, std::string* err = nullptr);

// A copy of `name` that lives as long as the process, for
// BranchModulePrototype::name (a `const char*`, which the world does not
// own). The same text returns the same pointer.
const char* internName(std::string_view name);

// The hash of the step's source this build was compiled from
// (BROFLORA_SIM_HASH, see cmake/sim_hash.cmake).
const char* simHash();

struct GrowResult {
    bool     cached = false;  // the grown world came from the cache
    uint64_t key    = 0;      // the cache key (also the file's name)
    std::string error;        // why the cache was not read or written, if it was not
};

// Advance `world` by `ticks` steps of `dt`, as `ticks` calls of step(world,
// dt) would. With a non-empty `cacheDir`, the grown world is looked up
// under a key of everything the result depends on (the world as it stands,
// ticks, dt, the step's source hash, compiler and configuration) and, on a
// miss, written there once grown (to a temporary file renamed into place,
// so a concurrent reader sees the whole file or none). A cache that cannot
// be read or written is skipped: the world is grown and the reason is in
// GrowResult::error.
GrowResult growCached(WorldState& world, uint32_t ticks, float dt, const std::string& cacheDir);

} // namespace broflora
