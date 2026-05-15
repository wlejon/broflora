#pragma once

// Environment — the ecosystem-scale spatial substrate plants live in.
//
// TerrainMap : 2D heightfield h(x,z).
// SoilMap    : 2D binary grid, 1 = blocked (water/rock), 0 = open.
// ShadowGrid : 3D uniform grid of light availability Q_G in [0,1].
// GlobalClimate : annual T(0), P; lapse rate y in T(h) = T(0) + y*h.
//
// All three 2D maps share the same world-space footprint (origin, cell
// size, dimensions). The ShadowGrid is independent so it can be coarser
// or finer than the terrain.
//
// Paper §2.

#include "broflora/vec_math.h"

#include <cstdint>
#include <vector>

namespace broflora {

struct GridFootprint2D {
    Vec2     origin   = {0.0f, 0.0f};  // world-space (x,z) of cell (0,0)
    float    cellSize = 1.0f;
    uint32_t width    = 0;             // cells along +x
    uint32_t depth    = 0;             // cells along +z
};

struct TerrainMap {
    GridFootprint2D footprint;
    std::vector<float> height;  // size = footprint.width * footprint.depth
};

struct SoilMap {
    GridFootprint2D footprint;
    std::vector<uint8_t> blocked;  // 0 = open, 1 = blocked
};

struct ShadowGrid {
    Vec3     origin   = {0.0f, 0.0f, 0.0f};
    float    cellSize = 1.0f;
    uint32_t width    = 0;   // +x
    uint32_t height   = 0;   // +y (vertical)
    uint32_t depth    = 0;   // +z

    // Q_G values, default 1.0 (full sun). Row order: x then y then z.
    // Index: (x * height + y) * depth + z.
    std::vector<float> qg;
};

inline uint32_t shadowIndex(const ShadowGrid& g, uint32_t x, uint32_t y, uint32_t z) {
    return (x * g.height + y) * g.depth + z;
}

struct GlobalClimate {
    float annualTempBase    = 15.0f;    // T(0), °C
    float annualPrecip      = 1000.0f;  // P, mm/yr
    float tempLapsePerUnit  = -0.0065f; // y in T(h) = T(0) + y*h
};

inline float climateTempAt(const GlobalClimate& c, float elevation) {
    return c.annualTempBase + c.tempLapsePerUnit * elevation;
}

} // namespace broflora
