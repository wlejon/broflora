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

// ── Property reading helpers ───────────────────────────────────────────

inline bool readFloatField(Value obj, std::string_view prop, float& out) {
    if (!ev::isObject(obj)) return false;
    Value v = ev::getProperty(obj, prop);
    if (ev::isNumber(v)) {
        double d = ev::toDouble(v);
        if (!std::isnan(d)) { out = static_cast<float>(d); return true; }
    }
    return false;
}

inline bool readIntField(Value obj, std::string_view prop, int& out) {
    if (!ev::isObject(obj)) return false;
    Value v = ev::getProperty(obj, prop);
    if (ev::isNumber(v)) {
        double d = ev::toDouble(v);
        if (!std::isnan(d)) { out = static_cast<int>(d); return true; }
    }
    return false;
}

inline bool readUint32Field(Value obj, std::string_view prop, uint32_t& out) {
    if (!ev::isObject(obj)) return false;
    Value v = ev::getProperty(obj, prop);
    if (ev::isNumber(v)) {
        double d = ev::toDouble(v);
        if (!std::isnan(d)) { out = static_cast<uint32_t>(d); return true; }
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
    Value v = ev::getProperty(obj, prop);
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

inline void readSdfMeshOptions(Value obj, broflora::SdfMeshOptions& opts) {
    if (!ev::isObject(obj)) return;
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
        ev::Persistent opts(ev::createObject());
        opts.set(ev::setProperty(opts.get(), "positions", p.get()));
        opts.set(ev::setProperty(opts.get(), "normals", n.get()));
        opts.set(ev::setProperty(opts.get(), "uvs", u.get()));
        opts.set(ev::setProperty(opts.get(), "colors", c.get()));
        opts.set(ev::setProperty(opts.get(), "indices", idx.get()));
        Value optVal = opts.get();
        ev::CallResult res = ev::construct(meshCtor.value, std::span<const Value>(&optVal, 1));
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

inline void readLeafClusterOptions(Value obj, broflora::LeafClusterOptions& opts) {
    if (!ev::isObject(obj)) return;
    readIntField  (obj, "count",            opts.count);
    readFloatField(obj, "twigLength",       opts.twigLength);
    readFloatField(obj, "twigRadius",       opts.twigRadius);
    readFloatField(obj, "petioleLength",    opts.petioleLength);
    readFloatField(obj, "leafWidth",        opts.leafWidth);
    readFloatField(obj, "leafLength",       opts.leafLength);

    Value sv = ev::getProperty(obj, "leafShape");
    if (ev::isUndefined(sv) || ev::isNull(sv)) {
        sv = ev::getProperty(obj, "shape");
    }
    if (!ev::isUndefined(sv) && !ev::isNull(sv)) {
        opts.leafShape = parseLeafShapeValue(sv);
        opts.shape = opts.leafShape;
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
}

inline void readLeafPlacementOptions(Value o, bromesh::LeafPlacementOptions& opts) {
    if (!ev::isObject(o)) return;
    readFloatField(o, "maxRadius",       opts.maxRadius);
    readIntField  (o, "minDepth",        opts.minDepth);
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
    if (ev::isNumber(seedV)) opts.seed = static_cast<uint64_t>(ev::toDouble(seedV));

    Value dw = ev::getProperty(o, "densityWeight");
    if (ev::isObject(dw)) {
        Value lenV = ev::getProperty(dw, "length");
        if (ev::isNumber(lenV)) {
            uint32_t n = static_cast<uint32_t>(ev::toDouble(lenV));
            opts.densityWeight.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                opts.densityWeight[i] = static_cast<float>(ev::toDouble(ev::getElement(dw, i)));
            }
        }
    }
}

// ── Species partial application ────────────────────────────────────────

inline void applySpeciesPartial(Value spec, broflora::Species& s) {
    if (!ev::isObject(spec)) return;
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

inline bool buildPrototype(Value spec,
                           broflora::BranchModulePrototype& out,
                           std::string& nameStorage) {
    if (!ev::isObject(spec)) return false;

    Value nameV = ev::getProperty(spec, "name");
    if (ev::isString(nameV)) {
        nameStorage = ev::toUtf8(nameV);
        out.name = nameStorage.c_str();
    }

    Value nodesV = ev::getProperty(spec, "nodes");
    if (ev::isObject(nodesV)) {
        Value lenV = ev::getProperty(nodesV, "length");
        if (ev::isNumber(lenV)) {
            uint32_t n = static_cast<uint32_t>(ev::toDouble(lenV));
            out.nodes.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                Value nv = ev::getElement(nodesV, i);
                broflora::ModuleNode mn;
                readVec3Prop  (nv, "position",    mn.position);
                readFloatField(nv, "ageAtBirth",  mn.ageAtBirth);
                readFloatField(nv, "lengthMax",   mn.lengthMax);
                readFloatField(nv, "thickening",  mn.thickening);
                out.nodes.push_back(mn);
            }
        }
    }

    Value edgesV = ev::getProperty(spec, "edges");
    if (ev::isObject(edgesV)) {
        Value lenV = ev::getProperty(edgesV, "length");
        if (ev::isNumber(lenV)) {
            uint32_t n = static_cast<uint32_t>(ev::toDouble(lenV));
            out.edges.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                Value evVal = ev::getElement(edgesV, i);
                broflora::ModuleEdge e{};
                if (ev::isObject(evVal)) {
                    Value lenSub = ev::getProperty(evVal, "length");
                    if (ev::isNumber(lenSub) && ev::toDouble(lenSub) >= 2.0) {
                        e.a = static_cast<uint32_t>(ev::toDouble(ev::getElement(evVal, 0)));
                        e.b = static_cast<uint32_t>(ev::toDouble(ev::getElement(evVal, 1)));
                    } else {
                        readUint32Field(evVal, "a", e.a);
                        readUint32Field(evVal, "b", e.b);
                    }
                }
                out.edges.push_back(e);
            }
        }
    }

    readUint32Field(spec, "rootNode", out.rootNode);

    Value termsV = ev::getProperty(spec, "terminalNodes");
    if (ev::isObject(termsV)) {
        Value lenV = ev::getProperty(termsV, "length");
        if (ev::isNumber(lenV)) {
            uint32_t n = static_cast<uint32_t>(ev::toDouble(lenV));
            out.terminalNodes.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                out.terminalNodes.push_back(static_cast<uint32_t>(ev::toDouble(ev::getElement(termsV, i))));
            }
        }
    }

    return !out.nodes.empty();
}

// ── Climate / shadow readers ───────────────────────────────────────────

inline void readClimateFields(Value obj, broflora::GlobalClimate& c) {
    if (!ev::isObject(obj)) return;
    readFloatField(obj, "annualTempBase",   c.annualTempBase);
    readFloatField(obj, "annualPrecip",     c.annualPrecip);
    readFloatField(obj, "tempLapsePerUnit", c.tempLapsePerUnit);
}

inline void readClimate(Value opts, broflora::GlobalClimate& c) {
    Value cv = ev::getProperty(opts, "climate");
    readClimateFields(cv, c);
}

inline void readShadow(Value opts, broflora::ShadowGrid& g) {
    Value sv = ev::getProperty(opts, "shadow");
    if (ev::isObject(sv)) {
        readVec3Prop  (sv, "origin",   g.origin);
        readFloatField(sv, "cellSize", g.cellSize);
        readUint32Field(sv, "width",   g.width);
        readUint32Field(sv, "height",  g.height);
        readUint32Field(sv, "depth",   g.depth);
        float fill = 1.0f;
        readFloatField(sv, "fill", fill);
        const size_t n = static_cast<size_t>(g.width) * g.height * g.depth;
        g.qg.assign(n, fill);
    }
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
