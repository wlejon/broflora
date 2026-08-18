#include "broflora/leaf_cluster.h"
#include "broflora/mesh_emit.h"

#include <bromath/bromath.h>
#include <bromath/spatial_hash.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace broflora {

using namespace bromath;

namespace {

constexpr float kTwoPi = bromath::TWO_PI;

// Fast per-segment RNG (splitmix64) for reproducible, lock-free stochastic sampling.
struct FastRng {
    uint64_t s;
    explicit FastRng(uint64_t seed) : s(seed) {}
    inline uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // 24-bit float in [0, 1).
    inline float uni01() { return (next() >> 40) * (1.0f / 16777216.0f); }
};

// Pick any unit vector perpendicular to t (assumed unit length).
Vec3 perpendicularUnit(Vec3 t) {
    Vec3 worldUp{0.0f, 1.0f, 0.0f};
    Vec3 c = vcross(t, worldUp);
    if (vdot(c, c) < 1e-8f) {
        c = vcross(t, Vec3{1.0f, 0.0f, 0.0f});
    }
    return vnorm(c);
}

// Append a sub-mesh to destination MeshData, remapping triangle indices.
void appendMesh(MeshData& dest, const MeshData& src) {
    if (src.empty()) return;
    const uint32_t baseIndex = static_cast<uint32_t>(dest.vertexCount());
    const size_t srcVCount = src.vertexCount();
    const bool hasNormals = src.hasNormals();
    const bool hasUVs = src.hasUVs();
    const bool hasColors = src.hasColors();

    dest.positions.insert(dest.positions.end(), src.positions.begin(), src.positions.end());

    if (hasNormals) {
        dest.normals.insert(dest.normals.end(), src.normals.begin(), src.normals.end());
    } else {
        dest.normals.reserve(dest.normals.size() + srcVCount * 3);
        for (size_t i = 0; i < srcVCount; ++i) {
            dest.normals.push_back(0.0f);
            dest.normals.push_back(1.0f);
            dest.normals.push_back(0.0f);
        }
    }

    if (hasUVs) {
        dest.uvs.insert(dest.uvs.end(), src.uvs.begin(), src.uvs.end());
    } else {
        dest.uvs.reserve(dest.uvs.size() + srcVCount * 2);
        for (size_t i = 0; i < srcVCount; ++i) {
            dest.uvs.push_back(0.0f);
            dest.uvs.push_back(0.0f);
        }
    }

    if (hasColors) {
        dest.colors.insert(dest.colors.end(), src.colors.begin(), src.colors.end());
    } else {
        dest.colors.reserve(dest.colors.size() + srcVCount * 4);
        for (size_t i = 0; i < srcVCount; ++i) {
            dest.colors.push_back(0.0f);
            dest.colors.push_back(0.0f);
            dest.colors.push_back(0.0f);
            dest.colors.push_back(1.0f);
        }
    }

    dest.indices.reserve(dest.indices.size() + src.indices.size());
    for (uint32_t idx : src.indices) {
        dest.indices.push_back(idx + baseIndex);
    }
}

// Build a tapered cylinder / tube along local +Z from (0,0,0) to (0,0,length).
MeshData buildMicroTwig(float length, float radius, int sides = 6, int rings = 4) {
    MeshData m;
    if (length <= 1e-5f || radius <= 1e-6f) return m;

    sides = std::max(3, sides);
    rings = std::max(2, rings);

    const int vcount = rings * (sides + 1);
    m.positions.reserve(vcount * 3);
    m.normals.reserve(vcount * 3);
    m.uvs.reserve(vcount * 2);
    m.colors.reserve(vcount * 4);

    for (int j = 0; j < rings; ++j) {
        float t = static_cast<float>(j) / static_cast<float>(rings - 1);
        float z = t * length;
        // Natural taper: base is full radius, tip narrows to 50%
        float r = radius * (1.0f - 0.5f * t);
        // Wind bend: 0.0 at base attachment, up to 0.35 at twig tip
        float wb = t * 0.35f;

        for (int i = 0; i <= sides; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(sides);
            float theta = u * kTwoPi;
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);

            m.positions.push_back(r * cosT);
            m.positions.push_back(r * sinT);
            m.positions.push_back(z);

            m.normals.push_back(cosT);
            m.normals.push_back(sinT);
            m.normals.push_back(0.0f);

            m.uvs.push_back(u);
            m.uvs.push_back(t);

            m.colors.push_back(wb);
            m.colors.push_back(0.0f);
            m.colors.push_back(0.0f);
            m.colors.push_back(1.0f);
        }
    }

    const int rowStride = sides + 1;
    for (int j = 0; j < rings - 1; ++j) {
        for (int i = 0; i < sides; ++i) {
            uint32_t a = static_cast<uint32_t>(j * rowStride + i);
            uint32_t b = static_cast<uint32_t>(j * rowStride + i + 1);
            uint32_t c = static_cast<uint32_t>((j + 1) * rowStride + i);
            uint32_t d = static_cast<uint32_t>((j + 1) * rowStride + i + 1);

            m.indices.push_back(a);
            m.indices.push_back(c);
            m.indices.push_back(b);

            m.indices.push_back(b);
            m.indices.push_back(c);
            m.indices.push_back(d);
        }
    }

    // End caps: bottom cap at z=0, top cap at z=length
    uint32_t baseCenterIdx = static_cast<uint32_t>(m.vertexCount());
    m.positions.push_back(0.0f);
    m.positions.push_back(0.0f);
    m.positions.push_back(0.0f);
    m.normals.push_back(0.0f);
    m.normals.push_back(0.0f);
    m.normals.push_back(-1.0f);
    m.uvs.push_back(0.5f);
    m.uvs.push_back(0.0f);
    m.colors.push_back(0.0f);
    m.colors.push_back(0.0f);
    m.colors.push_back(0.0f);
    m.colors.push_back(1.0f);

    for (int i = 0; i < sides; ++i) {
        uint32_t a = static_cast<uint32_t>(i);
        uint32_t b = static_cast<uint32_t>(i + 1);
        m.indices.push_back(baseCenterIdx);
        m.indices.push_back(a);
        m.indices.push_back(b);
    }

    uint32_t tipCenterIdx = static_cast<uint32_t>(m.vertexCount());
    m.positions.push_back(0.0f);
    m.positions.push_back(0.0f);
    m.positions.push_back(length);
    m.normals.push_back(0.0f);
    m.normals.push_back(0.0f);
    m.normals.push_back(1.0f);
    m.uvs.push_back(0.5f);
    m.uvs.push_back(1.0f);
    m.colors.push_back(0.35f);
    m.colors.push_back(0.0f);
    m.colors.push_back(0.0f);
    m.colors.push_back(1.0f);

    int topRowBase = (rings - 1) * rowStride;
    for (int i = 0; i < sides; ++i) {
        uint32_t a = static_cast<uint32_t>(topRowBase + i);
        uint32_t b = static_cast<uint32_t>(topRowBase + i + 1);
        m.indices.push_back(tipCenterIdx);
        m.indices.push_back(b);
        m.indices.push_back(a);
    }

    return m;
}

// Build a small tapered petiole (leaf stalk) connecting twig attachment to leaf card base.
MeshData buildPetiole(Vec3 from, Vec3 to, float radius, float baseBend, float tipBend, int sides = 4) {
    MeshData m;
    Vec3 axis = to - from;
    float len = vlen(axis);
    if (len <= 1e-5f || radius <= 1e-6f) return m;

    Vec3 dir = axis * (1.0f / len);
    Vec3 uAxis = perpendicularUnit(dir);
    Vec3 vAxis = vnorm(vcross(dir, uAxis));

    sides = std::max(3, sides);
    const int rings = 2;
    const int vcount = rings * (sides + 1);
    m.positions.reserve(vcount * 3);
    m.normals.reserve(vcount * 3);
    m.uvs.reserve(vcount * 2);
    m.colors.reserve(vcount * 4);

    for (int j = 0; j < rings; ++j) {
        float tj = static_cast<float>(j) / static_cast<float>(rings - 1);
        Vec3 center = from + axis * tj;
        float r = radius * (1.0f - 0.25f * tj);
        float wb = baseBend + tj * (tipBend - baseBend);

        for (int i = 0; i <= sides; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(sides);
            float theta = u * kTwoPi;
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);
            Vec3 radial = uAxis * cosT + vAxis * sinT;
            Vec3 p = center + radial * r;

            m.positions.push_back(p.x);
            m.positions.push_back(p.y);
            m.positions.push_back(p.z);

            m.normals.push_back(radial.x);
            m.normals.push_back(radial.y);
            m.normals.push_back(radial.z);

            m.uvs.push_back(u);
            m.uvs.push_back(tj);

            m.colors.push_back(wb);
            m.colors.push_back(0.0f);
            m.colors.push_back(0.0f);
            m.colors.push_back(1.0f);
        }
    }

    const int rowStride = sides + 1;
    for (int i = 0; i < sides; ++i) {
        uint32_t a = static_cast<uint32_t>(i);
        uint32_t b = static_cast<uint32_t>(i + 1);
        uint32_t c = static_cast<uint32_t>(rowStride + i);
        uint32_t d = static_cast<uint32_t>(rowStride + i + 1);

        m.indices.push_back(a);
        m.indices.push_back(c);
        m.indices.push_back(b);

        m.indices.push_back(b);
        m.indices.push_back(c);
        m.indices.push_back(d);
    }

    return m;
}

struct LeafSlot {
    float t;           // Fractional position along twig [0, 1]
    float phi;         // Azimuth around twig axis
    float spreadAngle; // Outward fan angle from +Z
};

std::vector<LeafSlot> computeSlots(Phyllotaxy phyllotaxy, int count, float spread) {
    std::vector<LeafSlot> slots;
    if (count <= 0) return slots;
    slots.reserve(static_cast<size_t>(count));

    switch (phyllotaxy) {
    case Phyllotaxy::Alternate: {
        // Distichous / alternating sides (0, PI, 0, PI) along twig
        for (int k = 0; k < count; ++k) {
            float t = (count == 1) ? 0.5f : (0.15f + 0.80f * (static_cast<float>(k) / static_cast<float>(count - 1)));
            float phi = (k % 2 == 0) ? 0.0f : PI;
            slots.push_back({t, phi, spread});
        }
        break;
    }
    case Phyllotaxy::Opposite: {
        // Decussate pairs (opposite each other, rotated 90° per node)
        int numPairs = (count + 1) / 2;
        for (int p = 0; p < numPairs; ++p) {
            float t = (numPairs == 1) ? 0.5f : (0.15f + 0.80f * (static_cast<float>(p) / static_cast<float>(numPairs - 1)));
            float phiBase = static_cast<float>(p) * (PI * 0.5f);
            slots.push_back({t, phiBase, spread});
            if (static_cast<int>(slots.size()) < count) {
                slots.push_back({t, phiBase + PI, spread});
            }
        }
        break;
    }
    case Phyllotaxy::Spiral: {
        // Golden angle rosette (~137.507764°)
        constexpr float kGoldenAngle = 2.39996322972865332f;
        for (int k = 0; k < count; ++k) {
            float t = (count == 1) ? 0.5f : (0.10f + 0.85f * (static_cast<float>(k) / static_cast<float>(count - 1)));
            float phi = static_cast<float>(k) * kGoldenAngle;
            slots.push_back({t, phi, spread});
        }
        break;
    }
    case Phyllotaxy::Fascicle: {
        // Pine needle bundle radiating from basal sheath
        for (int k = 0; k < count; ++k) {
            float phi = static_cast<float>(k) * (kTwoPi / static_cast<float>(count));
            slots.push_back({0.0f, phi, spread});
        }
        break;
    }
    case Phyllotaxy::CompoundPinnate: {
        // Central rachis: lateral pairs + 1 terminal leaflet at tip
        if (count == 1) {
            slots.push_back({1.0f, 0.0f, 0.0f});
        } else {
            int lateralCount = count - 1;
            int numPairs = (lateralCount + 1) / 2;
            for (int p = 0; p < numPairs; ++p) {
                float t = (numPairs == 1) ? 0.45f : (0.15f + 0.75f * (static_cast<float>(p) / static_cast<float>(numPairs - 1)));
                // Left leaflet (+X side)
                slots.push_back({t, PI * 0.5f, spread});
                // Right leaflet (-X side)
                if (static_cast<int>(slots.size()) < count - 1) {
                    slots.push_back({t, -PI * 0.5f, spread});
                }
            }
            // Terminal leaflet at tip
            slots.push_back({1.0f, 0.0f, 0.0f});
        }
        break;
    }
    }

    return slots;
}

void writeMatrix(std::vector<float>& out,
                 Vec3 sideAxis, Vec3 normal, Vec3 forward,
                 Vec3 origin, float scale) {
    out.push_back(sideAxis.x * scale);
    out.push_back(normal.x * scale);
    out.push_back(forward.x * scale);
    out.push_back(origin.x);

    out.push_back(sideAxis.y * scale);
    out.push_back(normal.y * scale);
    out.push_back(forward.y * scale);
    out.push_back(origin.y);

    out.push_back(sideAxis.z * scale);
    out.push_back(normal.z * scale);
    out.push_back(forward.z * scale);
    out.push_back(origin.z);

    out.push_back(1.0f);
    out.push_back(1.0f);
    out.push_back(1.0f);
    out.push_back(1.0f);
}

MeshData stampMeshAtPlacements(const MeshData& srcMesh, const LeafPlacements& pl) {
    if (srcMesh.empty()) return {};
    const size_t count = pl.count();
    if (count == 0) return {};

    const size_t srcVCount = srcMesh.vertexCount();
    const size_t srcICount = srcMesh.indices.size();
    const bool hasNormals  = srcMesh.hasNormals();
    const bool hasUVs      = srcMesh.hasUVs();
    const bool hasColors   = srcMesh.hasColors();

    MeshData out;
    out.positions.resize(count * srcVCount * 3);
    if (hasNormals) out.normals.resize(count * srcVCount * 3);
    if (hasUVs)     out.uvs.resize(count * srcVCount * 2);
    if (hasColors)  out.colors.resize(count * srcVCount * 4);
    out.indices.resize(count * srcICount);

    for (size_t i = 0; i < count; ++i) {
        const float* M = &pl.transforms[i * 16];
        size_t vBase = i * srcVCount;
        uint32_t indexOffset = static_cast<uint32_t>(vBase);

        for (size_t v = 0; v < srcVCount; ++v) {
            size_t destPos = (vBase + v) * 3;
            float x = srcMesh.positions[v * 3 + 0];
            float y = srcMesh.positions[v * 3 + 1];
            float z = srcMesh.positions[v * 3 + 2];
            out.positions[destPos + 0] = M[0]*x + M[1]*y + M[2]*z  + M[3];
            out.positions[destPos + 1] = M[4]*x + M[5]*y + M[6]*z  + M[7];
            out.positions[destPos + 2] = M[8]*x + M[9]*y + M[10]*z + M[11];

            if (hasNormals) {
                float nx = srcMesh.normals[v * 3 + 0];
                float ny = srcMesh.normals[v * 3 + 1];
                float nz = srcMesh.normals[v * 3 + 2];
                float tx = M[0]*nx + M[1]*ny + M[2]*nz;
                float ty = M[4]*nx + M[5]*ny + M[6]*nz;
                float tz = M[8]*nx + M[9]*ny + M[10]*nz;
                float L = std::sqrt(tx*tx + ty*ty + tz*tz);
                if (L > 1e-8f) {
                    tx /= L; ty /= L; tz /= L;
                } else {
                    float nLen = std::sqrt(M[1]*M[1] + M[5]*M[5] + M[9]*M[9]);
                    if (nLen > 1e-8f) { tx = M[1]/nLen; ty = M[5]/nLen; tz = M[9]/nLen; }
                    else { tx = 0.0f; ty = 1.0f; tz = 0.0f; }
                }
                out.normals[destPos + 0] = tx;
                out.normals[destPos + 1] = ty;
                out.normals[destPos + 2] = tz;
            }

            if (hasUVs) {
                out.uvs[(vBase + v) * 2 + 0] = srcMesh.uvs[v * 2 + 0];
                out.uvs[(vBase + v) * 2 + 1] = srcMesh.uvs[v * 2 + 1];
            }
            if (hasColors) {
                out.colors[(vBase + v) * 4 + 0] = srcMesh.colors[v * 4 + 0];
                out.colors[(vBase + v) * 4 + 1] = srcMesh.colors[v * 4 + 1];
                out.colors[(vBase + v) * 4 + 2] = srcMesh.colors[v * 4 + 2];
                out.colors[(vBase + v) * 4 + 3] = srcMesh.colors[v * 4 + 3];
            }
        }

        size_t iBase = i * srcICount;
        for (size_t idx = 0; idx < srcICount; ++idx) {
            out.indices[iBase + idx] = srcMesh.indices[idx] + indexOffset;
        }
    }

    return out;
}

} // namespace

MeshData leafCluster(Phyllotaxy phyllotaxy, const LeafClusterOptions& opts) {
    MeshData out;
    if (opts.count <= 0 && !opts.includeTwigMesh) {
        return out;
    }

    // 1. Build micro-twig / rachis mesh if enabled
    if (opts.includeTwigMesh && opts.twigLength > 1e-5f && opts.twigRadius > 1e-6f) {
        MeshData twig = buildMicroTwig(opts.twigLength, opts.twigRadius);
        appendMesh(out, twig);
    }

    if (opts.count <= 0) {
        return out;
    }

    // 2. Base template leaf card
    LeafShape effShape = (opts.leafShape != LeafShape::Oval) ? opts.leafShape : opts.shape;
    bromesh::LeafCardOptions cardOpts;
    cardOpts.width = opts.leafWidth;
    cardOpts.length = opts.leafLength;
    cardOpts.bend = opts.leafBend;
    cardOpts.curl = opts.leafCurl;
    cardOpts.cup = opts.leafCup;
    cardOpts.fullUV = opts.fullUV;
    cardOpts.shapedSilhouette = opts.shapedSilhouette;
    cardOpts.stemOffset = true;
    cardOpts.widthSegments = 4;
    cardOpts.lengthSegments = 8;

    MeshData baseCard = bromesh::leafCard(effShape, cardOpts);
    if (baseCard.empty()) return out;

    const size_t cardVCount = baseCard.vertexCount();
    const size_t cardICount = baseCard.indices.size();
    const bool cardHasNormals = baseCard.hasNormals();
    const bool cardHasUVs = baseCard.hasUVs();
    const bool cardHasColors = baseCard.hasColors();

    std::vector<LeafSlot> slots = computeSlots(phyllotaxy, opts.count, opts.spread);
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};

    for (const LeafSlot& slot : slots) {
        float t = slot.t;
        Vec3 attachPoint{0.0f, 0.0f, t * opts.twigLength};

        // Outward fan direction from twig axis
        Vec3 dir;
        if (slot.spreadAngle <= 1e-4f) {
            // Terminal leaflet pointing along +Z
            dir = Vec3{0.0f, 0.0f, 1.0f};
            if (opts.droop > 0.0f) {
                dir = vnorm(dir * (1.0f - opts.droop * 0.4f) + Vec3{0.0f, -1.0f, 0.0f} * (opts.droop * 0.4f));
            }
        } else {
            Vec3 radial{std::cos(slot.phi), std::sin(slot.phi), 0.0f};
            dir = Vec3{radial.x * std::sin(slot.spreadAngle),
                       radial.y * std::sin(slot.spreadAngle),
                       std::cos(slot.spreadAngle)};

            // Gravity droop sag
            if (opts.droop > 0.0f) {
                dir = vnorm(dir * (1.0f - opts.droop) + Vec3{0.0f, -1.0f, 0.0f} * opts.droop);
            }
            // Phototropic lift toward sky
            if (opts.upBias > 0.0f) {
                dir = vnormOr(dir * (1.0f - opts.upBias * 0.35f) + worldUp * (opts.upBias * 0.35f), dir);
            }
        }

        // Petiole geometry
        Vec3 leafBase = attachPoint;
        float baseBend = t * 0.35f;
        float petioleTipBend = baseBend + 0.25f;

        if (opts.petioleLength > 1e-4f) {
            leafBase = attachPoint + dir * opts.petioleLength;
            float petioleRadius = opts.twigRadius * 0.65f;
            MeshData petiole = buildPetiole(attachPoint, leafBase, petioleRadius, baseBend, petioleTipBend);
            appendMesh(out, petiole);
        }

        // Leaf orientation coordinate frame:
        // F = forward along leaf length
        // N = normal / upward adaxial surface (biased toward +Y)
        // side = lateral width axis
        Vec3 F = dir;
        Vec3 side = vcross(F, worldUp);
        if (vdot(side, side) < 1e-6f) {
            side = vcross(F, Vec3{1.0f, 0.0f, 0.0f});
        }
        side = vnorm(side);
        Vec3 N = vnorm(vcross(side, F));

        // When upBias is not 100%, blend with radial normal for natural 3D fullness
        if (opts.upBias < 0.99f && slot.spreadAngle > 1e-4f) {
            Vec3 radialN{std::cos(slot.phi), std::sin(slot.phi), 0.0f};
            Vec3 radialNormal = vnormOr(vcross(vcross(F, radialN), F), N);
            N = vnormOr(N * opts.upBias + radialNormal * (1.0f - opts.upBias), N);
            side = vnorm(vcross(F, N));
        }

        // Transform base card into cluster space and append
        uint32_t vOffset = static_cast<uint32_t>(out.vertexCount());
        out.positions.reserve(out.positions.size() + cardVCount * 3);
        if (cardHasNormals) out.normals.reserve(out.normals.size() + cardVCount * 3);
        if (cardHasUVs) out.uvs.reserve(out.uvs.size() + cardVCount * 2);
        if (cardHasColors) out.colors.reserve(out.colors.size() + cardVCount * 4);

        for (size_t v = 0; v < cardVCount; ++v) {
            float lx = baseCard.positions[v * 3 + 0];
            float ly = baseCard.positions[v * 3 + 1];
            float lz = baseCard.positions[v * 3 + 2];

            Vec3 pos = leafBase + side * lx + N * ly + F * lz;
            out.positions.push_back(pos.x);
            out.positions.push_back(pos.y);
            out.positions.push_back(pos.z);

            if (cardHasNormals) {
                float nx = baseCard.normals[v * 3 + 0];
                float ny = baseCard.normals[v * 3 + 1];
                float nz = baseCard.normals[v * 3 + 2];
                Vec3 rawNorm = side * nx + N * ny + F * nz;
                Vec3 norm = vnormOr(rawNorm, N);
                out.normals.push_back(norm.x);
                out.normals.push_back(norm.y);
                out.normals.push_back(norm.z);
            }

            if (cardHasUVs) {
                out.uvs.push_back(baseCard.uvs[v * 2 + 0]);
                out.uvs.push_back(baseCard.uvs[v * 2 + 1]);
            }

            if (cardHasColors) {
                float localBend = baseCard.colors[v * 4 + 0];
                float finalBend = petioleTipBend + localBend * (1.0f - petioleTipBend);
                finalBend = std::clamp(finalBend, 0.0f, 1.0f);
                out.colors.push_back(finalBend);
                out.colors.push_back(0.0f);
                out.colors.push_back(0.0f);
                out.colors.push_back(1.0f);
            }
        }

        out.indices.reserve(out.indices.size() + cardICount);
        for (size_t i = 0; i < cardICount; ++i) {
            out.indices.push_back(baseCard.indices[i] + vOffset);
        }
    }

    return out;
}

LeafPlacements placeLeafClustersOnBranches(
    const std::vector<BranchSegment>& segments,
    const LeafClusterPlacementOptions& opts) {
    LeafPlacements out;
    if (segments.empty()) return out;

    std::vector<int> childCount(segments.size(), 0);
    for (size_t i = 0; i < segments.size(); ++i) {
        int p = segments[i].parent;
        if (p >= 0 && static_cast<size_t>(p) < segments.size()) {
            ++childCount[p];
        }
    }

    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    const Vec3 gravity{0.0f, -1.0f, 0.0f};
    const float refRadius = (opts.maxRadius > 1e-8f) ? opts.maxRadius : 1.0f;
    const size_t segCount = segments.size();

    std::vector<std::vector<float>> segTransforms(segCount);
    std::vector<std::vector<float>> segRadius(segCount);
    std::vector<std::vector<int>> segDepth(segCount);

    #pragma omp parallel for schedule(dynamic, 16) if(segCount > 32)
    for (int i = 0; i < static_cast<int>(segCount); ++i) {
        const BranchSegment& seg = segments[static_cast<size_t>(i)];
        Vec3 d = seg.to - seg.from;
        float length = vlen(d);
        if (length < 1e-6f) continue;
        if (seg.depth < opts.minDepth) continue;
        if (seg.radius > 0.0f && seg.radius > opts.maxRadius) continue;
        if (opts.terminalOnly && childCount[static_cast<size_t>(i)] > 0) continue;

        Vec3 T = d * (1.0f / length);

        float weight = 1.0f;
        if (!opts.densityWeight.empty()) {
            weight = (static_cast<size_t>(i) < opts.densityWeight.size())
                         ? std::max(0.0f, opts.densityWeight[static_cast<size_t>(i)])
                         : 0.0f;
            if (weight <= 0.0f) continue;
        }

        FastRng rng(opts.seed ^ (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL));

        float expected = length * opts.perUnitLength * weight;
        int sampleCount = static_cast<int>(std::floor(expected));
        if (rng.uni01() < (expected - static_cast<float>(sampleCount))) {
            ++sampleCount;
        }
        if (sampleCount <= 0) continue;

        auto& transforms = segTransforms[static_cast<size_t>(i)];
        auto& radii = segRadius[static_cast<size_t>(i)];
        auto& depths = segDepth[static_cast<size_t>(i)];
        transforms.reserve(static_cast<size_t>(sampleCount) * 16);
        radii.reserve(static_cast<size_t>(sampleCount));
        depths.reserve(static_cast<size_t>(sampleCount));

        for (int s = 0; s < sampleCount; ++s) {
            float u = rng.uni01();
            float t = (opts.densityFalloff > 0.0f)
                ? 1.0f - std::pow(1.0f - u, 1.0f + opts.densityFalloff)
                : u;

            float phi = rng.uni01() * kTwoPi;
            Vec3 e1 = perpendicularUnit(T);
            Vec3 e2 = vcross(T, e1);
            Vec3 R = e1 * std::cos(phi) + e2 * std::sin(phi);

            // Attachment point on branch surface
            Vec3 P = seg.from + d * t + R * seg.radius;

            // Obstacle test on candidate origin
            const int selfTag = static_cast<int>(i);
            if (opts.avoid != nullptr && !opts.avoid->empty()) {
                if (opts.avoid->tooClose(P, opts.obstacleClearance, selfTag)) {
                    if (opts.obstaclePushout > 0.0f) {
                        auto n = opts.avoid->nearest(P, selfTag);
                        if (n.tag != -1 || std::isfinite(n.distance)) {
                            P = n.point + n.normal * (opts.obstacleClearance + opts.obstaclePushout);
                        }
                        if (opts.avoid->tooClose(P, opts.obstacleClearance, selfTag)) {
                            continue;
                        }
                    } else {
                        continue;
                    }
                }
            }
            if (!opts.keepOut.empty()) {
                bool blocked = false;
                for (const auto& sp : opts.keepOut) {
                    Vec3 dv = P - sp.center;
                    float r = sp.radius + opts.obstacleClearance;
                    if (vdot(dv, dv) <= r * r) { blocked = true; break; }
                }
                if (blocked) continue;
            }

            // Cluster growth direction: along branch tangent flared by branchAngle
            float flare = (t > 0.85f && childCount[static_cast<size_t>(i)] == 0)
                              ? (opts.branchAngle * 0.3f)
                              : opts.branchAngle;
            Vec3 D = vnormOr(T * (1.0f - flare) + R * flare, T);

            // Gravity sag / droop
            float horiz = std::sqrt(D.x * D.x + D.z * D.z);
            Vec3 F = vnormOr(D + gravity * (0.15f * horiz), D);

            // Phototropism: turn cluster toward light
            Vec3 Fraw = F * (1.0f - opts.upBias * 0.4f) + worldUp * (opts.upBias * 0.4f);
            F = vnormOr(Fraw, F);

            // Tilt jitter
            if (opts.tiltJitter > 0.0f) {
                float tiltAng = (rng.uni01() * 2.0f - 1.0f) * opts.tiltJitter;
                Vec3 tiltAxis = vnormOr(vcross(F, T), e2);
                F = vnorm(qrotate(qaxisAngle(tiltAxis, tiltAng), F));
            }

            // Upper surface normal faces skyward / light
            Vec3 sideAxis = vcross(F, worldUp);
            if (vdot(sideAxis, sideAxis) < 1e-8f) {
                sideAxis = vcross(F, Vec3{1.0f, 0.0f, 0.0f});
            }
            sideAxis = vnorm(sideAxis);
            Vec3 N = vnorm(vcross(sideAxis, F));

            // Roll jitter
            if (opts.rollJitter > 0.0f) {
                float rollAng = (rng.uni01() * 2.0f - 1.0f) * opts.rollJitter;
                Quat q = qaxisAngle(F, rollAng);
                sideAxis = vnorm(qrotate(q, sideAxis));
                N = vnorm(qrotate(q, N));
            }

            float jitter = (rng.uni01() * 2.0f - 1.0f) * opts.scaleJitter;
            float radiusFactor = 1.0f;
            if (opts.scaleByRadius > 0.0f) {
                float ratio = (seg.radius > 0.0f) ? (seg.radius / refRadius) : 1.0f;
                radiusFactor = 1.0f + opts.scaleByRadius * (ratio - 1.0f);
                if (radiusFactor < 0.05f) radiusFactor = 0.05f;
            }
            float scale = opts.baseScale * (1.0f + jitter) * radiusFactor;
            if (scale < 1e-6f) continue;

            writeMatrix(transforms, sideAxis, N, F, P, scale);
            radii.push_back(seg.radius);
            depths.push_back(seg.depth);
        }
    }

    size_t totalTransforms = 0;
    for (size_t i = 0; i < segCount; ++i) {
        totalTransforms += segRadius[i].size();
    }
    out.transforms.reserve(totalTransforms * 16);
    out.branchRadius.reserve(totalTransforms);
    out.branchDepth.reserve(totalTransforms);

    if (opts.dedupRadius > 0.0f) {
        SpatialHash3D dedupHash(opts.dedupRadius);
        int32_t dedupId = 0;
        std::vector<int32_t> nearby;
        for (size_t i = 0; i < segCount; ++i) {
            const auto& transforms = segTransforms[i];
            const size_t placed = segRadius[i].size();
            for (size_t k = 0; k < placed; ++k) {
                const float* M = transforms.data() + k * 16;
                Vec3 P{M[3], M[7], M[11]};
                nearby.clear();
                dedupHash.radiusQuery(P, opts.dedupRadius, nearby);
                if (!nearby.empty()) continue;
                dedupHash.insert(P, dedupId++);
                out.transforms.insert(out.transforms.end(), M, M + 16);
                out.branchRadius.push_back(segRadius[i][k]);
                out.branchDepth.push_back(segDepth[i][k]);
            }
        }
    } else {
        for (size_t i = 0; i < segCount; ++i) {
            out.transforms.insert(out.transforms.end(), segTransforms[i].begin(), segTransforms[i].end());
            out.branchRadius.insert(out.branchRadius.end(), segRadius[i].begin(), segRadius[i].end());
            out.branchDepth.insert(out.branchDepth.end(), segDepth[i].begin(), segDepth[i].end());
        }
    }

    return out;
}

MeshData scatterLeafClusters(
    const std::vector<BranchSegment>& segments,
    Phyllotaxy phyllotaxy,
    const LeafClusterOptions& clusterOpts,
    const LeafClusterPlacementOptions& placementOpts) {
    MeshData cluster = leafCluster(phyllotaxy, clusterOpts);
    if (cluster.empty()) return {};
    LeafPlacements pl = placeLeafClustersOnBranches(segments, placementOpts);
    return stampMeshAtPlacements(cluster, pl);
}

LeafPlacements placeLeafClustersOnPlant(
    const Plant& plant,
    const LeafClusterPlacementOptions& opts) {
    auto segs = emitPlantSegments(plant);
    if (segs.empty()) return {};

    LeafClusterPlacementOptions resolvedOpts = opts;
    if (resolvedOpts.densityWeight.empty()) {
        auto foliage = emitPlantFoliage(plant);
        resolvedOpts.densityWeight.resize(foliage.size());
        for (size_t i = 0; i < foliage.size(); ++i) {
            resolvedOpts.densityWeight[i] = foliage[i].mass;
        }
    }

    return placeLeafClustersOnBranches(segs, resolvedOpts);
}

LeafPlacements placeLeafClustersOnTerminals(
    const Plant& plant,
    const LeafClusterPlacementOptions& opts) {
    LeafClusterPlacementOptions terminalOpts = opts;
    terminalOpts.terminalOnly = true;
    return placeLeafClustersOnPlant(plant, terminalOpts);
}

MeshData emitPlantLeafClusters(
    const Plant& plant,
    Phyllotaxy phyllotaxy,
    const LeafClusterOptions& clusterOpts,
    const LeafClusterPlacementOptions& placementOpts) {
    MeshData cluster = leafCluster(phyllotaxy, clusterOpts);
    if (cluster.empty()) return {};
    LeafPlacements pl = placeLeafClustersOnPlant(plant, placementOpts);
    return stampMeshAtPlacements(cluster, pl);
}

MeshData emitWorldLeafClusters(
    const WorldState& world,
    Phyllotaxy phyllotaxy,
    const LeafClusterOptions& clusterOpts,
    const LeafClusterPlacementOptions& placementOpts) {
    auto segs = emitWorldSegments(world);
    if (segs.empty()) return {};

    LeafClusterPlacementOptions resolvedOpts = placementOpts;
    if (resolvedOpts.densityWeight.empty()) {
        auto foliage = emitWorldFoliage(world);
        resolvedOpts.densityWeight.resize(foliage.size());
        for (size_t i = 0; i < foliage.size(); ++i) {
            resolvedOpts.densityWeight[i] = foliage[i].mass;
        }
    }

    MeshData cluster = leafCluster(phyllotaxy, clusterOpts);
    if (cluster.empty()) return {};
    LeafPlacements pl = placeLeafClustersOnBranches(segs, resolvedOpts);
    return stampMeshAtPlacements(cluster, pl);
}

} // namespace broflora
