#pragma once

// Internal environment-grid samplers shared across .cpp files. Senescence
// (seeding), world.cpp (per-tick origin snap), and development.cpp
// (slope-based root anchoring) all need the same TerrainMap / SoilMap
// lookups; keeping them out of any public header preserves the "no new
// public surface in env.h until needed" convention.

#include "broflora/environment.h"
#include "bromath/vec.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace broflora::internal {

// 2D-grid lookup that returns false when (x,z) is outside the footprint.
// Cells are addressed (ix, iz) with stride `depth`. Same indexing the
// rest of broflora has used since the maps were introduced.
template <typename T>
inline bool sampleGrid(const GridFootprint2D& fp,
                       const std::vector<T>& cells,
                       bromath::Vec3 world,
                       T& out) {
    if (fp.cellSize <= 0.0f || fp.width == 0 || fp.depth == 0) return false;
    float fx = (world.x - fp.origin.x) / fp.cellSize;
    float fz = (world.z - fp.origin.y) / fp.cellSize;
    if (fx < 0.0f || fz < 0.0f) return false;
    uint32_t ix = static_cast<uint32_t>(fx);
    uint32_t iz = static_cast<uint32_t>(fz);
    if (ix >= fp.width || iz >= fp.depth) return false;
    out = cells[ix * fp.depth + iz];
    return true;
}

inline float terrainHeightAt(const TerrainMap& t,
                             bromath::Vec3 world,
                             float fallback) {
    float h;
    return sampleGrid<float>(t.footprint, t.height, world, h) ? h : fallback;
}

inline bool soilBlockedAt(const SoilMap& s, bromath::Vec3 world) {
    uint8_t v;
    return sampleGrid<uint8_t>(s.footprint, s.blocked, world, v) && v != 0;
}

// Terrain normal at `world.xz`, computed by central differences over
// the heightfield. Returns +Y when the position is outside the
// footprint or the map is degenerate. The result is unit-length.
inline bromath::Vec3 terrainNormalAt(const TerrainMap& t, bromath::Vec3 world) {
    const float cs = t.footprint.cellSize;
    if (cs <= 0.0f || t.footprint.width == 0 || t.footprint.depth == 0)
        return {0.0f, 1.0f, 0.0f};
    bromath::Vec3 px = world; px.x += cs;
    bromath::Vec3 nx = world; nx.x -= cs;
    bromath::Vec3 pz = world; pz.z += cs;
    bromath::Vec3 nz = world; nz.z -= cs;
    float h0 = terrainHeightAt(t, world, 0.0f);
    float dhx = terrainHeightAt(t, px, h0) - terrainHeightAt(t, nx, h0);
    float dhz = terrainHeightAt(t, pz, h0) - terrainHeightAt(t, nz, h0);
    // Surface gradient is (dh/dx, dh/dz); normal is (-dh/dx, 2·cs, -dh/dz).
    bromath::Vec3 n = { -dhx, 2.0f * cs, -dhz };
    return bromath::vnorm(n);
}

// Nearest-cell Q_G at a world position. Returns `fallback` when the
// position is outside the grid (or there is no grid). Mirrors the
// shadow-cell lookup in light.cpp; lives here so the seeding pass can
// read canopy light for recruitment without reaching into light.cpp.
inline float shadowAt(const ShadowGrid& g, bromath::Vec3 p, float fallback) {
    if (g.cellSize <= 0.0f || g.width == 0 || g.height == 0 || g.depth == 0)
        return fallback;
    float fx = (p.x - g.origin.x) / g.cellSize;
    float fy = (p.y - g.origin.y) / g.cellSize;
    float fz = (p.z - g.origin.z) / g.cellSize;
    if (fx < 0.0f || fy < 0.0f || fz < 0.0f) return fallback;
    uint32_t x = static_cast<uint32_t>(fx);
    uint32_t y = static_cast<uint32_t>(fy);
    uint32_t z = static_cast<uint32_t>(fz);
    if (x >= g.width || y >= g.height || z >= g.depth) return fallback;
    return g.qg[shadowIndex(g, x, y, z)];
}

// Is the xz of `p` inside the shadow grid footprint? When there is no
// grid the concept doesn't apply, so we return true (no containment).
inline bool withinShadowXZ(const ShadowGrid& g, bromath::Vec3 p) {
    if (g.cellSize <= 0.0f || g.width == 0 || g.depth == 0) return true;
    float fx = (p.x - g.origin.x) / g.cellSize;
    float fz = (p.z - g.origin.z) / g.cellSize;
    return fx >= 0.0f && fz >= 0.0f
        && fx < static_cast<float>(g.width) && fz < static_cast<float>(g.depth);
}

// Angle between the terrain normal at `world` and +Y, in radians. 0 on
// flat ground, π/2 on a vertical cliff.
inline float terrainSlopeAt(const TerrainMap& t, bromath::Vec3 world) {
    bromath::Vec3 n = terrainNormalAt(t, world);
    // Clamp because rounding can push the dot product just past 1.
    float cosA = n.y;
    if (cosA > 1.0f) cosA = 1.0f;
    if (cosA < -1.0f) cosA = -1.0f;
    return std::acos(cosA);
}

} // namespace broflora::internal
