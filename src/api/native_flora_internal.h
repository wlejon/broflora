#pragma once

#include "embed/embed.h"
#include "broflora/broflora.h"
#include "broflora/leaf_cluster.h"
#include "broflora/mesh_emit.h"
#include "bromath/vec.h"
#include "bromesh/mesh_data.h"
#include "bromesh/procedural/leaf_scatter.h"
#include "bromesh/procedural/plants.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace broflora::api {

namespace ev = bronze::embed;
using Value = bronze::Value;

// ── Opaque wrapper ─────────────────────────────────────────────────────
struct FloraWorldWrapper {
    std::unique_ptr<broflora::WorldState> world;
};

inline void destroyFloraWorldWrapper(void* p) {
    delete static_cast<FloraWorldWrapper*>(p);
}

inline FloraWorldWrapper* getWrapper(Value v) {
    void* data = ev::handleData(v);
    return static_cast<FloraWorldWrapper*>(data);
}

// ── Global simulation parameters (wind, density) ───────────────────────
double getGlobalWindStrength();
double getGlobalWindDirX();
double getGlobalWindDirY();
double getGlobalWindTime();
double getGlobalDensity();
void setGlobalWind(double strength, double dirX, double dirY);
void setGlobalDensity(double density);
void updateGlobalWind(double dt);
void clearGlobalWind();

// ── Wind ───────────────────────────────────────────────────────────────
//
// One wind model serves every bent output. At a point p the wind gives a
// displacement (along the wind, growing with height above y = 0 and zero at
// the ground so a rooted trunk stays put, with a small dip so the bend reads
// as an arc) and a rigid rotation (about the horizontal axis perpendicular
// to the wind, tilting +Y toward it). An instance matrix moves its
// translation by the displacement and turns its basis by the rotation; a
// mesh vertex moves by the displacement and turns its normal by the
// rotation. So a leaf drawn from an instance matrix and the same leaf
// stamped into a mesh sway identically, and neither path shears.
struct WindSway {
    float offset[3];
    float rot[3][3];  // row-major, orthonormal
};
WindSway windSwayAt(float px, float py, float pz,
                    double windTime, double windStrength,
                    double dirX, double dirY);
void applyWindToTransforms(float* transforms, size_t count,
                           double windTime, double windStrength,
                           double dirX, double dirY);
void applyWindToMeshData(bromesh::MeshData& md,
                         double windTime, double windStrength,
                         double dirX, double dirY);

// ── GC rooting ─────────────────────────────────────────────────────────
//
// bronze's collector moves objects, so a Value copied out of an argument or
// a property read goes stale at the next allocating embed call (getProperty,
// getElement, createObject, ...). A reader that makes several reads from one
// object holds it in a Rooted, which re-reads the root at every use.
struct Rooted {
    ev::Persistent p;
    explicit Rooted(Value v) : p(v) {}
    operator Value() const { return p.get(); }
    Value get() const { return p.get(); }
};

// ── Integer arguments ──────────────────────────────────────────────────
//
// Counts, sizes and indices arrive as JS doubles. A static_cast of a
// negative, NaN or out-of-range double to an integer type is undefined
// behaviour and in practice wraps to ~4e9, which then sizes an allocation or
// indexes past a vector. Every such value goes through intValue: a
// non-number is a TypeError; NaN, a fraction or a value outside [lo, hi] is
// a RangeError. It returns false once it has raised, and the caller returns
// ev::undefined() (what the throw helpers return) so the exception reaches
// JS. `what` names the argument in the message, e.g.
// "bro.flora.FloraWorld.emitMesh: sides".

inline constexpr double kMaxUint32 = 4294967295.0;

inline std::string numberText(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == std::floor(d) && std::fabs(d) < 1e15) return std::to_string(static_cast<long long>(d));
    std::string s = std::to_string(d);
    while (!s.empty() && s.back() == '0') s.pop_back();
    return s;
}

inline bool intValue(Value v, std::string_view what, double lo, double hi, int64_t& out) {
    if (!ev::isNumber(v)) {
        ev::throwTypeError(std::string(what) + " must be a number");
        return false;
    }
    const double d = ev::toDouble(v);
    if (std::isnan(d) || d != std::floor(d) || d < lo || d > hi) {
        ev::throwRangeError(std::string(what) + " must be an integer in [" + numberText(lo) + ", " +
                            numberText(hi) + "], got " + numberText(d));
        return false;
    }
    out = static_cast<int64_t>(d);
    return true;
}

// intValue for an optional positional argument: absent or undefined leaves
// `out` alone (it holds the default).
template <typename T>
inline bool optIntArg(std::span<const Value> args, size_t i, std::string_view what,
                      double lo, double hi, T& out) {
    if (i >= args.size() || ev::isUndefined(args[i])) return true;
    int64_t v = 0;
    if (!intValue(args[i], what, lo, hi, v)) return false;
    out = static_cast<T>(v);
    return true;
}

// intValue for an option field: an undefined field leaves `out` alone.
template <typename T>
inline bool intField(Value obj, std::string_view prop, std::string_view what,
                     double lo, double hi, T& out) {
    if (!ev::isObject(obj)) return true;
    Value v = ev::getProperty(obj, prop);
    if (ev::isUndefined(v)) return true;
    int64_t n = 0;
    if (!intValue(v, what, lo, hi, n)) return false;
    out = static_cast<T>(n);
    return true;
}

// The element count of an array or array-like option value.
inline bool lengthOf(Value arr, std::string_view what, uint32_t& out) {
    out = 0;
    return intField(arr, "length", what, 0.0, kMaxUint32, out);
}

// A plant index. Not a throwing argument: an index that names no plant
// (negative, fractional, NaN, past the end) is the documented null / false
// / [] answer of every per-plant method, so this only keeps the conversion
// defined.
inline bool plantIndexOf(Value v, const broflora::WorldState& world, size_t& out) {
    if (!ev::isNumber(v)) return false;
    const double d = ev::toDouble(v);
    if (!(d >= 0.0) || d != std::floor(d) || d >= static_cast<double>(world.plants.size())) return false;
    out = static_cast<size_t>(d);
    return true;
}

// A seed: any number, converted without undefined behaviour. Non-negative
// integers keep their value; a negative one wraps as two's complement;
// NaN and infinities read as 0.
inline uint64_t seedOf(double d) {
    if (!std::isfinite(d)) return 0;
    d = std::trunc(d);
    if (d >= 18446744073709551615.0) return UINT64_MAX;
    if (d >= 0.0) return static_cast<uint64_t>(d);
    if (d <= -9223372036854775808.0) return static_cast<uint64_t>(INT64_MIN);
    return static_cast<uint64_t>(static_cast<int64_t>(d));
}

// ── Property reading helpers ───────────────────────────────────────────
// The single-read helpers below are safe on a plain Value; the multi-read
// ones root their input first.

inline bool readFloatField(Value obj, std::string_view prop, float& out) {
    if (!ev::isObject(obj)) return false;
    Value v = ev::getProperty(obj, prop);
    if (ev::isNumber(v)) {
        double d = ev::toDouble(v);
        if (!std::isnan(d)) { out = static_cast<float>(d); return true; }
    }
    return false;
}

inline bool readBoolField(Value obj, std::string_view prop, bool& out) {
    if (!ev::isObject(obj)) return false;
    Value v = ev::getProperty(obj, prop);
    if (ev::isBool(v)) {
        out = ev::toBool(v);
        return true;
    }
    return false;
}

inline bool readVec3Prop(Value obj, std::string_view prop, bromath::Vec3& out) {
    if (!ev::isObject(obj)) return false;
    Rooted v(ev::getProperty(obj, prop));
    if (!ev::isObject(v)) return false;
    Value lenV = ev::getProperty(v, "length");
    if (ev::isNumber(lenV) && ev::toDouble(lenV) >= 3.0) {
        out.x = static_cast<float>(ev::toDouble(ev::getElement(v, 0)));
        out.y = static_cast<float>(ev::toDouble(ev::getElement(v, 1)));
        out.z = static_cast<float>(ev::toDouble(ev::getElement(v, 2)));
        return true;
    }
    return false;
}

inline void readSdfMeshOptions(Value in, broflora::SdfMeshOptions& opts) {
    if (!ev::isObject(in)) return;
    Rooted obj(in);
    readFloatField(obj, "voxelSize", opts.voxelSize);
    readFloatField(obj, "smoothK", opts.smoothK);
    readBoolField(obj, "useSurfaceNets", opts.useSurfaceNets);
    readFloatField(obj, "margin", opts.margin);
}

// ── Value building helpers ─────────────────────────────────────────────

inline Value createEmptyArray() {
    ev::CallResult parsed = ev::parseJson("[]");
    return parsed.thrown ? ev::undefined() : parsed.value;
}

template <typename F>
inline Value hostArrayOf(size_t count, F&& make) {
    ev::CallResult parsed = ev::parseJson("[]");
    if (parsed.thrown) return ev::undefined();
    ev::Persistent arr(parsed.value);
    if (count == 0) return arr.get();

    ev::Persistent push(ev::getProperty(arr.get(), "push"));
    if (!ev::isFunction(push.get())) {
        return arr.get();
    }
    for (size_t i = 0; i < count; ++i) {
        Value v = make(i);
        ev::CallResult r = ev::call(push.get(), arr.get(), std::span<const Value>(&v, 1));
        if (r.thrown) break;
    }
    return arr.get();
}

inline Value makeVec3(const bromath::Vec3& v) {
    return hostArrayOf(3, [&](size_t i) {
        if (i == 0) return ev::fromDouble(v.x);
        if (i == 1) return ev::fromDouble(v.y);
        return ev::fromDouble(v.z);
    });
}

inline Value makeFloat32Array(const float* data, size_t count) {
    Value arr = ev::createTypedArray(ev::elements::Float32, static_cast<uint32_t>(count));
    if (data && count > 0) {
        ev::fillTypedArray(arr, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data), count * sizeof(float)));
    }
    return arr;
}

inline Value makeUint32Array(const uint32_t* data, size_t count) {
    Value arr = ev::createTypedArray(ev::elements::Uint32, static_cast<uint32_t>(count));
    if (data && count > 0) {
        ev::fillTypedArray(arr, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data), count * sizeof(uint32_t)));
    }
    return arr;
}

inline Value wrapMeshData(std::unique_ptr<bromesh::MeshData> md) {
    if (!md) return ev::null();
    ev::Persistent p(makeFloat32Array(md->positions.data(), md->positions.size()));
    ev::Persistent n(makeFloat32Array(md->normals.data(), md->normals.size()));
    ev::Persistent u(makeFloat32Array(md->uvs.data(), md->uvs.size()));
    ev::Persistent c(makeFloat32Array(md->colors.data(), md->colors.size()));
    ev::Persistent idx(makeUint32Array(md->indices.data(), md->indices.size()));

    ev::GlobalValue meshCtor = ev::globalValue("Mesh");
    if (meshCtor.found && ev::isFunction(meshCtor.value)) {
        Rooted ctor(meshCtor.value);  // the option object below allocates
        ev::Persistent opts(ev::createObject());
        opts.set(ev::setProperty(opts.get(), "positions", p.get()));
        opts.set(ev::setProperty(opts.get(), "normals", n.get()));
        opts.set(ev::setProperty(opts.get(), "uvs", u.get()));
        opts.set(ev::setProperty(opts.get(), "colors", c.get()));
        opts.set(ev::setProperty(opts.get(), "indices", idx.get()));
        Value optVal = opts.get();
        ev::CallResult res = ev::construct(ctor.get(), std::span<const Value>(&optVal, 1));
        if (!res.thrown) return res.value;
    }

    ev::Persistent obj(ev::createObject());
    obj.set(ev::setProperty(obj.get(), "positions", p.get()));
    obj.set(ev::setProperty(obj.get(), "normals", n.get()));
    obj.set(ev::setProperty(obj.get(), "uvs", u.get()));
    obj.set(ev::setProperty(obj.get(), "colors", c.get()));
    obj.set(ev::setProperty(obj.get(), "indices", idx.get()));
    uint32_t vc = static_cast<uint32_t>(md->vertexCount());
    uint32_t tc = static_cast<uint32_t>(md->triangleCount());
    obj.set(ev::setProperty(obj.get(), "vertexCount", ev::fromDouble(vc)));
    obj.set(ev::setProperty(obj.get(), "triangleCount", ev::fromDouble(tc)));
    return obj.get();
}

inline bool getMeshData(Value val, bromesh::MeshData& out) {
    if (!ev::isObject(val)) return false;
    ev::Persistent root(val);
    Value p = ev::getProperty(root.get(), "positions");
    auto pInfo = ev::typedArrayInfo(p);
    if (!pInfo || pInfo.elementCount % 3 != 0) return false;
    out.positions.assign(reinterpret_cast<const float*>(pInfo.data),
                        reinterpret_cast<const float*>(pInfo.data) + pInfo.elementCount);

    Value n = ev::getProperty(root.get(), "normals");
    auto nInfo = ev::typedArrayInfo(n);
    if (nInfo) {
        out.normals.assign(reinterpret_cast<const float*>(nInfo.data),
                           reinterpret_cast<const float*>(nInfo.data) + nInfo.elementCount);
    }

    Value u = ev::getProperty(root.get(), "uvs");
    auto uInfo = ev::typedArrayInfo(u);
    if (uInfo) {
        out.uvs.assign(reinterpret_cast<const float*>(uInfo.data),
                       reinterpret_cast<const float*>(uInfo.data) + uInfo.elementCount);
    }

    Value c = ev::getProperty(root.get(), "colors");
    auto cInfo = ev::typedArrayInfo(c);
    if (cInfo) {
        out.colors.assign(reinterpret_cast<const float*>(cInfo.data),
                          reinterpret_cast<const float*>(cInfo.data) + cInfo.elementCount);
    }

    Value idx = ev::getProperty(root.get(), "indices");
    auto idxInfo = ev::typedArrayInfo(idx);
    if (idxInfo) {
        if (idxInfo.bytesPerElement == 4) {
            out.indices.assign(reinterpret_cast<const uint32_t*>(idxInfo.data),
                               reinterpret_cast<const uint32_t*>(idxInfo.data) + idxInfo.elementCount);
        } else if (idxInfo.bytesPerElement == 2) {
            const auto* u16 = reinterpret_cast<const uint16_t*>(idxInfo.data);
            out.indices.assign(u16, u16 + idxInfo.elementCount);
        }
    }
    return true;
}

inline void fillFoliageDensity(const std::vector<broflora::FoliageSample>& samples,
                               size_t segCount,
                               bromesh::LeafPlacementOptions& opts) {
    if (!opts.densityWeight.empty() || samples.size() != segCount) return;
    opts.densityWeight.resize(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& f = samples[i];
        float exposure = 0.12f + 0.88f * f.lightExposure01;
        float maturity = std::min(1.0f, f.age01);
        float alive    = 1.0f - f.senescence01;
        float stem     = f.twigGrade01 * f.twigGrade01;
        if (!f.isTerminal) stem *= 0.10f;
        opts.densityWeight[i] = exposure * maturity * alive * stem;
    }
}

// ── Leaf shape & phyllotaxy parsing ────────────────────────────────────

inline bromesh::LeafShape parseLeafShapeValue(Value v) {
    if (ev::isString(v)) {
        std::string s = ev::toUtf8(v);
        if (s == "oval") return bromesh::LeafShape::Oval;
        if (s == "pointed") return bromesh::LeafShape::Pointed;
        if (s == "lobed") return bromesh::LeafShape::Lobed;
        if (s == "needle") return bromesh::LeafShape::Needle;
        if (s == "frond") return bromesh::LeafShape::Frond;
        if (s == "petal") return bromesh::LeafShape::Petal;
    } else if (ev::isNumber(v)) {
        int val = static_cast<int>(ev::toDouble(v));
        if (val >= 0 && val <= 5) return static_cast<bromesh::LeafShape>(val);
    }
    return bromesh::LeafShape::Oval;
}

inline broflora::Phyllotaxy parsePhyllotaxy(Value v) {
    if (ev::isNumber(v)) {
        int val = static_cast<int>(ev::toDouble(v));
        if (val >= 0 && val <= 4) return static_cast<broflora::Phyllotaxy>(val);
    } else if (ev::isString(v)) {
        std::string s = ev::toUtf8(v);
        if (s == "alternate" || s == "Alternate") return broflora::Phyllotaxy::Alternate;
        if (s == "opposite" || s == "Opposite") return broflora::Phyllotaxy::Opposite;
        if (s == "spiral" || s == "Spiral") return broflora::Phyllotaxy::Spiral;
        if (s == "fascicle" || s == "Fascicle") return broflora::Phyllotaxy::Fascicle;
        if (s == "compoundPinnate" || s == "CompoundPinnate" ||
            s == "compound_pinnate" || s == "pinnate") return broflora::Phyllotaxy::CompoundPinnate;
    }
    return broflora::Phyllotaxy::Alternate;
}

// Returns false once it has thrown (a bad count).
inline bool readLeafClusterOptions(Value in, broflora::LeafClusterOptions& opts) {
    if (!ev::isObject(in)) return true;
    Rooted obj(in);
    if (!intField(obj, "count", "bro.flora.leafCluster: opts.count", 0, 4096, opts.count)) return false;
    readFloatField(obj, "twigLength",       opts.twigLength);
    readFloatField(obj, "twigRadius",       opts.twigRadius);
    readFloatField(obj, "petioleLength",    opts.petioleLength);
    readFloatField(obj, "leafWidth",        opts.leafWidth);
    readFloatField(obj, "leafLength",       opts.leafLength);

    {
        Value sv = ev::getProperty(obj, "leafShape");
        if (ev::isUndefined(sv) || ev::isNull(sv)) {
            sv = ev::getProperty(obj, "shape");
        }
        // parseLeafShapeValue reads sv before anything allocates.
        if (!ev::isUndefined(sv) && !ev::isNull(sv)) {
            opts.leafShape = parseLeafShapeValue(sv);
            opts.shape = opts.leafShape;
        }
    }

    readFloatField(obj, "leafBend",         opts.leafBend);
    readFloatField(obj, "leafCurl",         opts.leafCurl);
    readFloatField(obj, "leafCup",          opts.leafCup);
    readFloatField(obj, "droop",            opts.droop);
    readFloatField(obj, "upBias",           opts.upBias);
    readFloatField(obj, "spread",           opts.spread);
    readBoolField (obj, "includeTwigMesh",  opts.includeTwigMesh);
    readBoolField (obj, "shapedSilhouette", opts.shapedSilhouette);
    readBoolField (obj, "fullUV",           opts.fullUV);
    return true;
}

// Returns false once it has thrown (a bad minDepth or densityWeight length).
inline bool readLeafPlacementOptions(Value in, bromesh::LeafPlacementOptions& opts) {
    if (!ev::isObject(in)) return true;
    Rooted o(in);
    readFloatField(o, "maxRadius",       opts.maxRadius);
    if (!intField(o, "minDepth", "leaf placement opts.minDepth", -2147483648.0, 2147483647.0, opts.minDepth)) {
        return false;
    }
    readBoolField (o, "terminalOnly",    opts.terminalOnly);
    readFloatField(o, "perUnitLength",   opts.perUnitLength);
    readFloatField(o, "densityFalloff",  opts.densityFalloff);
    readFloatField(o, "upBias",          opts.upBias);
    readFloatField(o, "tiltJitter",      opts.tiltJitter);
    readFloatField(o, "rollJitter",      opts.rollJitter);
    readFloatField(o, "baseScale",       opts.baseScale);
    readFloatField(o, "scaleJitter",     opts.scaleJitter);
    readFloatField(o, "scaleByRadius",   opts.scaleByRadius);
    readFloatField(o, "dedupRadius",     opts.dedupRadius);

    Value seedV = ev::getProperty(o, "seed");
    if (ev::isNumber(seedV)) opts.seed = seedOf(ev::toDouble(seedV));

    Rooted dw(ev::getProperty(o, "densityWeight"));
    if (ev::isObject(dw)) {
        uint32_t n = 0;
        if (!lengthOf(dw, "leaf placement opts.densityWeight.length", n)) return false;
        opts.densityWeight.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            opts.densityWeight[i] = static_cast<float>(ev::toDouble(ev::getElement(dw, i)));
        }
    }
    return true;
}

// ── Species partial application ────────────────────────────────────────

inline void applySpeciesPartial(Value in, broflora::Species& s) {
    if (!ev::isObject(in)) return;
    Rooted spec(in);
    readFloatField(spec, "maxVigor",                    s.maxVigor);
    readFloatField(spec, "minVigor",                    s.minVigor);
    readFloatField(spec, "rootVigorMax",                s.rootVigorMax);
    readFloatField(spec, "apicalControl",               s.apicalControl);
    readFloatField(spec, "determinacy",                 s.determinacy);
    readFloatField(spec, "shadeTolerance",              s.shadeTolerance);
    readFloatField(spec, "apicalControlMature",         s.apicalControlMature);
    readFloatField(spec, "determinacyMature",           s.determinacyMature);
    readVec3Prop  (spec, "tropismDir",                  s.tropismDir);
    readFloatField(spec, "tropismG1",                   s.tropismG1);
    readFloatField(spec, "tropismG2",                   s.tropismG2);
    readFloatField(spec, "growthScale",                 s.growthScale);
    readFloatField(spec, "climateOptT",                 s.climateOptT);
    readFloatField(spec, "climateOptP",                 s.climateOptP);
    readFloatField(spec, "climateSigT",                 s.climateSigT);
    readFloatField(spec, "climateSigP",                 s.climateSigP);
    readFloatField(spec, "maxAge",                      s.maxAge);
    readFloatField(spec, "floweringAge",                s.floweringAge);
    readFloatField(spec, "seedingRadius",               s.seedingRadius);
    readFloatField(spec, "moduleMatureAge",             s.moduleMatureAge);
    readFloatField(spec, "pipeExp",                     s.pipeExp);
    readFloatField(spec, "leafDiameter",                s.leafDiameter);
    readFloatField(spec, "terrainAnchorWeight",         s.terrainAnchorWeight);
    readFloatField(spec, "maxSeedingSlope",             s.maxSeedingSlope);
    readFloatField(spec, "distributionWeightCollisions",s.distributionWeightCollisions);
    readFloatField(spec, "distributionWeightTropism",   s.distributionWeightTropism);
    readFloatField(spec, "orthotropy",                  s.orthotropy);
    readFloatField(spec, "individualVariation",         s.individualVariation);
}

// ── Prototype builder ──────────────────────────────────────────────────

enum class BuildResult { Ok, NotAPrototype, Threw };

// A spec with no nodes is NotAPrototype (addPrototype answers -1). Node
// references -- rootNode, both ends of every edge, every terminal -- must be
// integers naming a node, or it throws a RangeError: the simulation indexes
// `nodes` with them unchecked.
inline BuildResult buildPrototype(Value in,
                                  broflora::BranchModulePrototype& out,
                                  std::string& nameStorage) {
    if (!ev::isObject(in)) return BuildResult::NotAPrototype;
    Rooted spec(in);
    constexpr std::string_view kWhat = "bro.flora.FloraWorld.addPrototype: ";

    Value nameV = ev::getProperty(spec, "name");
    if (ev::isString(nameV)) {
        nameStorage = ev::toUtf8(nameV);
        out.name = nameStorage.c_str();
    }

    Rooted nodesV(ev::getProperty(spec, "nodes"));
    if (ev::isObject(nodesV)) {
        uint32_t n = 0;
        if (!lengthOf(nodesV, std::string(kWhat) + "spec.nodes.length", n)) return BuildResult::Threw;
        out.nodes.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            Rooted nv(ev::getElement(nodesV, i));
            broflora::ModuleNode mn;
            readVec3Prop  (nv, "position",    mn.position);
            readFloatField(nv, "ageAtBirth",  mn.ageAtBirth);
            readFloatField(nv, "lengthMax",   mn.lengthMax);
            readFloatField(nv, "thickening",  mn.thickening);
            out.nodes.push_back(mn);
        }
    }
    if (out.nodes.empty()) return BuildResult::NotAPrototype;
    const double lastNode = static_cast<double>(out.nodes.size() - 1);
    auto nodeRef = [&](Value v, const std::string& what, uint32_t& ref) {
        int64_t r = 0;
        if (!intValue(v, what, 0.0, lastNode, r)) return false;
        ref = static_cast<uint32_t>(r);
        return true;
    };

    Rooted edgesV(ev::getProperty(spec, "edges"));
    if (ev::isObject(edgesV)) {
        uint32_t n = 0;
        if (!lengthOf(edgesV, std::string(kWhat) + "spec.edges.length", n)) return BuildResult::Threw;
        out.edges.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            Rooted evVal(ev::getElement(edgesV, i));
            const std::string at = std::string(kWhat) + "spec.edges[" + std::to_string(i) + "]";
            if (!ev::isObject(evVal)) {
                ev::throwTypeError(at + " must be [a, b] or {a, b}");
                return BuildResult::Threw;
            }
            broflora::ModuleEdge e{};
            Value lenSub = ev::getProperty(evVal, "length");
            if (ev::isNumber(lenSub)) {
                if (!(ev::toDouble(lenSub) >= 2.0)) {
                    ev::throwTypeError(at + " must be [a, b] or {a, b}");
                    return BuildResult::Threw;
                }
                if (!nodeRef(ev::getElement(evVal, 0), at + "[0]", e.a)) return BuildResult::Threw;
                if (!nodeRef(ev::getElement(evVal, 1), at + "[1]", e.b)) return BuildResult::Threw;
            } else {
                if (!nodeRef(ev::getProperty(evVal, "a"), at + ".a", e.a)) return BuildResult::Threw;
                if (!nodeRef(ev::getProperty(evVal, "b"), at + ".b", e.b)) return BuildResult::Threw;
            }
            out.edges.push_back(e);
        }
    }

    {
        Value rv = ev::getProperty(spec, "rootNode");
        if (!ev::isUndefined(rv) && !nodeRef(rv, std::string(kWhat) + "spec.rootNode", out.rootNode)) {
            return BuildResult::Threw;
        }
    }
    if (out.rootNode >= out.nodes.size()) {
        ev::throwRangeError(std::string(kWhat) + "spec.rootNode defaults to " + std::to_string(out.rootNode) +
                            ", which names no node");
        return BuildResult::Threw;
    }

    Rooted termsV(ev::getProperty(spec, "terminalNodes"));
    if (ev::isObject(termsV)) {
        uint32_t n = 0;
        if (!lengthOf(termsV, std::string(kWhat) + "spec.terminalNodes.length", n)) return BuildResult::Threw;
        out.terminalNodes.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t t = 0;
            if (!nodeRef(ev::getElement(termsV, i),
                         std::string(kWhat) + "spec.terminalNodes[" + std::to_string(i) + "]", t)) {
                return BuildResult::Threw;
            }
            out.terminalNodes.push_back(t);
        }
    }

    return BuildResult::Ok;
}

// ── Climate / shadow readers ───────────────────────────────────────────

inline void readClimateFields(Value in, broflora::GlobalClimate& c) {
    if (!ev::isObject(in)) return;
    Rooted obj(in);
    readFloatField(obj, "annualTempBase",   c.annualTempBase);
    readFloatField(obj, "annualPrecip",     c.annualPrecip);
    readFloatField(obj, "tempLapsePerUnit", c.tempLapsePerUnit);
}

inline void readClimate(Value opts, broflora::GlobalClimate& c) {
    Value cv = ev::getProperty(opts, "climate");
    readClimateFields(cv, c);
}

// The shadow grid's cell budget: 2^26 cells is 256 MB of floats.
inline constexpr double kMaxShadowCells = 67108864.0;

// Returns false once it has thrown (a bad grid dimension).
inline bool readShadow(Value opts, broflora::ShadowGrid& g) {
    Rooted sv(ev::getProperty(opts, "shadow"));
    if (ev::isObject(sv)) {
        readVec3Prop  (sv, "origin",   g.origin);
        readFloatField(sv, "cellSize", g.cellSize);
        constexpr std::string_view kWhat = "bro.flora.createWorld: shadow.";
        if (!intField(sv, "width",  std::string(kWhat) + "width",  0.0, kMaxShadowCells, g.width))  return false;
        if (!intField(sv, "height", std::string(kWhat) + "height", 0.0, kMaxShadowCells, g.height)) return false;
        if (!intField(sv, "depth",  std::string(kWhat) + "depth",  0.0, kMaxShadowCells, g.depth))  return false;
        const double cells = static_cast<double>(g.width) * g.height * g.depth;
        if (cells > kMaxShadowCells) {
            ev::throwRangeError(std::string(kWhat) + "width * height * depth is " + numberText(cells) +
                                " cells, over the " + numberText(kMaxShadowCells) + " limit");
            return false;
        }
        float fill = 1.0f;
        readFloatField(sv, "fill", fill);
        g.qg.assign(static_cast<size_t>(cells), fill);
    }
    return true;
}

inline Value protoToSpec(const broflora::BranchModulePrototype& p) {
    ev::Persistent o(ev::createObject());
    if (p.name) {
        ev::Persistent nameVal(ev::fromUtf8(p.name));
        o.set(ev::setProperty(o.get(), "name", nameVal.get()));
    }

    ev::Persistent nodes(hostArrayOf(p.nodes.size(), [&](size_t i) {
        const auto& nd = p.nodes[i];
        ev::Persistent nv(ev::createObject());
        ev::Persistent pos(makeVec3(nd.position));
        nv.set(ev::setProperty(nv.get(), "position", pos.get()));
        nv.set(ev::setProperty(nv.get(), "ageAtBirth", ev::fromDouble(nd.ageAtBirth)));
        nv.set(ev::setProperty(nv.get(), "lengthMax", ev::fromDouble(nd.lengthMax)));
        nv.set(ev::setProperty(nv.get(), "thickening", ev::fromDouble(nd.thickening)));
        return nv.get();
    }));
    o.set(ev::setProperty(o.get(), "nodes", nodes.get()));

    ev::Persistent edges(hostArrayOf(p.edges.size(), [&](size_t i) {
        return hostArrayOf(2, [&](size_t j) {
            return ev::fromDouble(j == 0 ? p.edges[i].a : p.edges[i].b);
        });
    }));
    o.set(ev::setProperty(o.get(), "edges", edges.get()));

    o.set(ev::setProperty(o.get(), "rootNode", ev::fromDouble(p.rootNode)));

    ev::Persistent terms(hostArrayOf(p.terminalNodes.size(), [&](size_t i) {
        return ev::fromDouble(p.terminalNodes[i]);
    }));
    o.set(ev::setProperty(o.get(), "terminalNodes", terms.get()));
    return o.get();
}

} // namespace broflora::api
