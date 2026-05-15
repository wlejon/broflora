#pragma once

// Deterministic rng. SplitMix64 generator, no global state — caller
// threads a uint64_t through. Same primitive bromesh / brogameagent use
// to keep simulations reproducible from a seed.

#include "broflora/vec_math.h"

#include <cmath>
#include <cstdint>

namespace broflora {

inline uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Uniform float in [0,1).
inline float randFloat01(uint64_t& state) {
    // 24-bit mantissa worth of entropy.
    return (splitmix64(state) >> 40) * (1.0f / 16777216.0f);
}

// Uniform float in [-1, 1].
inline float randFloatSigned(uint64_t& state) {
    return randFloat01(state) * 2.0f - 1.0f;
}

// Box–Muller standard normal.
inline float randNormal(uint64_t& state) {
    float u1 = randFloat01(state);
    if (u1 < 1e-7f) u1 = 1e-7f;
    float u2 = randFloat01(state);
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.28318530718f * u2);
}

// 2D Gaussian disc sample with stdev `sigma` around origin (XZ plane).
inline Vec2 randGaussian2D(uint64_t& state, float sigma) {
    return {randNormal(state) * sigma, randNormal(state) * sigma};
}

} // namespace broflora
