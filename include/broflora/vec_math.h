#pragma once

// Minimal vector math for broflora. Plain PODs — flat, bindings-friendly,
// no operator overloading on member functions so the structs stay
// trivially copyable. Mirrors the style of bromesh::Vec3 without taking
// a dependency on it.

#include <cmath>

namespace broflora {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Euler angles (paper notation): roll φ, pitch θ, yaw ψ. Radians.
struct Euler3 {
    float phi   = 0.0f;
    float theta = 0.0f;
    float psi   = 0.0f;
};

inline Vec3 v3_add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 v3_sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 v3_scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float v3_len2(Vec3 a) { return v3_dot(a, a); }
inline float v3_len(Vec3 a) { return std::sqrt(v3_len2(a)); }

inline Vec3 v3_normalize(Vec3 a) {
    float l = v3_len(a);
    if (l <= 0.0f) return {0.0f, 0.0f, 0.0f};
    float inv = 1.0f / l;
    return {a.x * inv, a.y * inv, a.z * inv};
}

// Smooth-step S(x) = 3x^2 - 2x^3 on x in [0,1] (paper §3.3, growth rate).
inline float smoothstep01(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

// Linear interp.
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace broflora
