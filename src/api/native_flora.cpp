#include "native_flora_internal.h"

#include <mutex>
#include <unordered_set>

namespace broflora::api {

// ── FloraWorld brand ───────────────────────────────────────────────────────
// Every FloraWorldWrapper a handle carries is registered here while the
// handle lives; getWrapper reads a payload only when it is in the set. The
// set is process-wide (a Worker's worlds have unique payload addresses too)
// and never destroyed, so a sweep at exit can still unregister.
namespace {

std::mutex& worldBrandMutex() {
    static std::mutex m;
    return m;
}

std::unordered_set<const void*>& worldBrands() {
    static auto* s = new std::unordered_set<const void*>();
    return *s;
}

// Runs in the sweep (Finalize::InSweep): no heap access, only the set and
// the delete.
void destroyFloraWorldWrapper(void* p) {
    {
        std::lock_guard<std::mutex> lk(worldBrandMutex());
        worldBrands().erase(p);
    }
    delete static_cast<FloraWorldWrapper*>(p);
}

} // namespace

Value makeFloraWorldHandle(FloraWorldWrapper* wrap, Value proto) {
    {
        std::lock_guard<std::mutex> lk(worldBrandMutex());
        worldBrands().insert(wrap);
    }
    // The 4-argument form is fatal on a non-object prototype.
    if (!ev::isObject(proto)) {
        return ev::makeHandle(wrap, &destroyFloraWorldWrapper, ev::Finalize::InSweep);
    }
    return ev::makeHandle(wrap, &destroyFloraWorldWrapper, ev::Finalize::InSweep, proto);
}

FloraWorldWrapper* getWrapper(Value v) {
    void* data = ev::handleData(v);
    if (!data) return nullptr;
    std::lock_guard<std::mutex> lk(worldBrandMutex());
    return worldBrands().count(data) ? static_cast<FloraWorldWrapper*>(data) : nullptr;
}

// Forward declarations of emit methods from native_flora_emit.cpp
Value jsEmitMesh(Value thisVal, std::span<const Value> args);
Value jsEmitSegments(Value thisVal, std::span<const Value> args);
Value jsEmitFoliage(Value thisVal, std::span<const Value> args);
Value jsEmitBloomAnchors(Value thisVal, std::span<const Value> args);
Value jsEmitPlantMesh(Value thisVal, std::span<const Value> args);
Value jsEmitPlantSegments(Value thisVal, std::span<const Value> args);
Value jsEmitPlantFoliage(Value thisVal, std::span<const Value> args);
Value jsEmitPlantBloomAnchors(Value thisVal, std::span<const Value> args);
Value jsEmitFoliageTransforms(Value thisVal, std::span<const Value> args);
Value jsEmitSegmentTransforms(Value thisVal, std::span<const Value> args);
Value jsEmitScatterSegments(Value thisVal, std::span<const Value> args);
Value jsEmitBranchTubes(Value thisVal, std::span<const Value> args);
Value jsEmitFoliageMesh(Value thisVal, std::span<const Value> args);
Value jsEmitPlantFoliageMesh(Value thisVal, std::span<const Value> args);
Value jsEmitBloomMesh(Value thisVal, std::span<const Value> args);
Value jsLeafCluster(Value thisVal, std::span<const Value> args);
Value jsEmitPlantSdfMesh(Value thisVal, std::span<const Value> args);
Value jsEmitWorldSdfMesh(Value thisVal, std::span<const Value> args);

static double s_windStrength = 0.0;
static double s_windDirX = 0.0;
static double s_windDirY = 0.0;
static double s_density = 1.0;
static double s_windTime = 0.0;

double getGlobalWindStrength() { return s_windStrength; }
double getGlobalWindDirX() { return s_windDirX; }
double getGlobalWindDirY() { return s_windDirY; }
double getGlobalWindTime() { return s_windTime; }
double getGlobalDensity() { return s_density; }
void setGlobalWind(double strength, double dirX, double dirY) {
    s_windStrength = strength;
    s_windDirX = dirX;
    s_windDirY = dirY;
}
void setGlobalDensity(double density) {
    s_density = density;
}
void updateGlobalWind(double dt) {
    s_windTime += dt;
}
void clearGlobalWind() {
    s_windStrength = 0.0;
    s_windDirX = 0.0;
    s_windDirY = 0.0;
    s_density = 1.0;
    s_windTime = 0.0;
}

// The wind model (native_flora_internal.h describes the contract). The sway
// is zero at y = 0 and grows with height; the gust wave is phased by ground
// position so neighbours move out of step. The tilt is a Rodrigues rotation
// by `tilt` about u = (dz, 0, -dx), the horizontal axis perpendicular to
// the wind, which carries +Y toward the wind direction; it is clamped to
// +-0.35 rad in both directions so a negative strength (wind reversed) is
// bounded the same as a positive one.
WindSway windSwayAt(float px, float py, float pz,
                    double windTime, double windStrength,
                    double dirX, double dirY) {
    float dx = static_cast<float>(dirX);
    float dz = static_cast<float>(dirY);
    const float dlen = std::hypot(dx, dz);
    if (dlen > 1e-6f) { dx /= dlen; dz /= dlen; }
    else { dx = 1.0f; dz = 0.0f; }

    const float strength = static_cast<float>(windStrength);
    const float time = static_cast<float>(windTime);

    const float height = std::max(0.0f, py);
    const float heightFactor = 0.04f * height + 0.015f * height * height;
    const float phase = px * 0.4f + pz * 0.4f;
    const float wave = std::sin(time * 2.8f + phase) * 0.7f + std::sin(time * 5.2f + phase * 1.7f) * 0.3f;
    const float sway = strength * heightFactor * (1.0f + 0.6f * wave);

    WindSway w{};
    const float offX = dx * sway;
    const float offZ = dz * sway;
    w.offset[0] = offX;
    w.offset[1] = -0.05f * (offX * offX + offZ * offZ) / (height + 0.1f);
    w.offset[2] = offZ;

    const float tilt = std::clamp(sway * 0.2f, -0.35f, 0.35f);
    const float c = std::cos(tilt);
    const float s = std::sin(tilt);
    const float t = 1.0f - c;
    w.rot[0][0] = c + dz * dz * t;  w.rot[0][1] = dx * s;  w.rot[0][2] = -dx * dz * t;
    w.rot[1][0] = -dx * s;          w.rot[1][1] = c;       w.rot[1][2] = -dz * s;
    w.rot[2][0] = -dx * dz * t;     w.rot[2][1] = dz * s;  w.rot[2][2] = c + dx * dx * t;
    return w;
}

void applyWindToTransforms(float* transforms, size_t count,
                           double windTime, double windStrength,
                           double dirX, double dirY) {
    if (!transforms || count == 0 || windStrength == 0.0) return;
    for (size_t i = 0; i < count; ++i) {
        float* m = transforms + i * 16;
        // The origin (floats 3 / 7 / 11) moves as a mesh vertex there would,
        // so a rooted instance stays rooted. The basis turns by the wind at
        // the instance's tip, the end of its local +Y axis (column 1 of the
        // basis, which carries the instance's scale): an instance standing
        // on the ground still bends by its own height, where the wind at its
        // base would be zero.
        const WindSway base = windSwayAt(m[3], m[7], m[11], windTime, windStrength, dirX, dirY);
        const WindSway tip = windSwayAt(m[3] + m[1], m[7] + m[5], m[11] + m[9],
                                        windTime, windStrength, dirX, dirY);
        m[3]  += base.offset[0];
        m[7]  += base.offset[1];
        m[11] += base.offset[2];
        // R * B for the row-major 3x3 basis B (floats 0-2 / 4-6 / 8-10).
        for (int k = 0; k < 3; ++k) {
            const float b0 = m[k], b1 = m[4 + k], b2 = m[8 + k];
            m[k]     = tip.rot[0][0] * b0 + tip.rot[0][1] * b1 + tip.rot[0][2] * b2;
            m[4 + k] = tip.rot[1][0] * b0 + tip.rot[1][1] * b1 + tip.rot[1][2] * b2;
            m[8 + k] = tip.rot[2][0] * b0 + tip.rot[2][1] * b1 + tip.rot[2][2] * b2;
        }
    }
}

void applyWindToMeshData(bromesh::MeshData& md,
                         double windTime, double windStrength,
                         double dirX, double dirY) {
    if (md.positions.empty() || windStrength == 0.0) return;
    const size_t nv = md.positions.size() / 3;
    const bool hasNormals = (md.normals.size() == md.positions.size());
    for (size_t i = 0; i < nv; ++i) {
        float* p = &md.positions[i * 3];
        const WindSway w = windSwayAt(p[0], p[1], p[2], windTime, windStrength, dirX, dirY);
        p[0] += w.offset[0];
        p[1] += w.offset[1];
        p[2] += w.offset[2];
        if (hasNormals) {
            float* n = &md.normals[i * 3];
            const float n0 = n[0], n1 = n[1], n2 = n[2];
            n[0] = w.rot[0][0] * n0 + w.rot[0][1] * n1 + w.rot[0][2] * n2;
            n[1] = w.rot[1][0] * n0 + w.rot[1][1] * n1 + w.rot[1][2] * n2;
            n[2] = w.rot[2][0] * n0 + w.rot[2][1] * n1 + w.rot[2][2] * n2;
        }
    }
}

namespace {

Value jsCreateWorld(Value /*thisVal*/, std::span<const Value> args) {
    // Rooting (embed.h: a raw Value is stale after any allocating call).
    // args[0] is a live root the collector updates, so every option read
    // goes through it rather than a copy. Each Value read below is consumed
    // by the very next call (as that call's receiver or argument) before
    // anything else allocates, and the prototype, which is held across the
    // world's construction, is rooted.
    auto world = std::make_unique<broflora::WorldState>();
    if (!args.empty() && ev::isObject(args[0])) {
        if (!seedField(args[0], "rngSeed", "bro.flora.createWorld: rngSeed", world->rngState)) {
            return ev::undefined();
        }
        readClimate(args[0], world->climate);
        if (!readShadow(args[0], world->shadow)) return ev::undefined();
    }

    Rooted proto(ev::undefined());
    ev::GlobalValue fwGlobal = ev::globalValue("FloraWorld");
    if (fwGlobal.found && ev::isObject(fwGlobal.value)) {
        proto.p.set(ev::getProperty(fwGlobal.value, "prototype"));
    } else {
        ev::GlobalValue broGlobal = ev::globalValue("bro");
        if (broGlobal.found && ev::isObject(broGlobal.value)) {
            Value flora = ev::getProperty(broGlobal.value, "flora");
            if (ev::isObject(flora)) {
                Value fw = ev::getProperty(flora, "FloraWorld");
                if (ev::isObject(fw)) {
                    proto.p.set(ev::getProperty(fw, "prototype"));
                }
            }
        }
    }
    // The 4-argument form is fatal on a non-object prototype, so a world made
    // before flora.js mounted FloraWorld is a bare handle (createWorld's JS
    // side re-parents it). The wrapper is made last, after every call that
    // can run user code (a getter), so an early return cannot leak it.
    auto* wrap = new FloraWorldWrapper{std::move(world)};
    return makeFloraWorldHandle(wrap, proto.get());
}

Value jsAddPrototype(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::fromDouble(-1.0);
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::fromDouble(-1.0);

    broflora::BranchModulePrototype proto;
    std::string nameStorage;
    switch (buildPrototype(args[1], proto, nameStorage)) {
        case BuildResult::Ok: break;
        case BuildResult::NotAPrototype: return ev::fromDouble(-1.0);
        case BuildResult::Threw: return ev::undefined();
    }
    uint32_t idx = broflora::addPrototype(*w->world, std::move(proto));
    return ev::fromDouble(static_cast<double>(idx));
}

Value jsAddVoronoiSite(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return args.empty() ? ev::undefined() : args[0];
    auto* w = getWrapper(args[0]);
    if (w && w->world) {
        // An index past the prototype list is kept (validate() reports it and
        // spawning skips the site); one no uint32 can hold is an error.
        int64_t idx = 0;
        if (!intValue(args[1], "bro.flora.FloraWorld.addVoronoiSite: prototypeIndex", 0.0, kMaxUint32, idx)) {
            return ev::undefined();
        }
        const uint32_t protoIdx = static_cast<uint32_t>(idx);
        float det = args.size() >= 3 && ev::isNumber(args[2]) ? static_cast<float>(ev::toDouble(args[2])) : 1.0f;
        float ac = args.size() >= 4 && ev::isNumber(args[3]) ? static_cast<float>(ev::toDouble(args[3])) : 0.5f;
        broflora::addVoronoiSite(*w->world, protoIdx, det, ac);
    }
    return args[0];
}

Value jsAddPlant(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::fromDouble(-1.0);
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::fromDouble(-1.0);
    if (!ev::isObject(args[1])) return ev::fromDouble(-1.0);
    Rooted spec(args[1]);

    broflora::Plant p;
    p.species = {};
    applySpeciesPartial(ev::getProperty(spec, "species"), p.species);

    readVec3Prop(spec, "origin", p.origin);
    readFloatField(spec, "age", p.age);
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    int64_t protoIdx = -1;
    if (!intField(spec, "prototypeIndex", "bro.flora.FloraWorld.addPlant: spec.prototypeIndex",
                  0.0, kMaxUint32, protoIdx)) {
        return ev::undefined();
    }
    if (protoIdx >= 0) {
        const auto* proto = broflora::prototypeAt(*w->world, static_cast<uint32_t>(protoIdx));
        if (!proto) return ev::fromDouble(-1.0);
        broflora::BranchModuleInstance root;
        root.prototype = proto;
        root.parent = UINT32_MAX;
        root.age = 0.0f;
        float initialVigor = p.species.minVigor * 2.0f;
        readFloatField(spec, "initialVigor", initialVigor);
        root.vigor = initialVigor;
        root.light = 1.0f;
        p.modules.push_back(root);
    }
    broflora::addPlant(*w->world, std::move(p));
    return ev::fromDouble(static_cast<double>(w->world->plants.size() - 1));
}

Value jsRemovePlant(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::fromBool(false);
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::fromBool(false);
    size_t plantIdx = 0;
    if (!plantIndexOf(args[1], *w->world, plantIdx)) return ev::fromBool(false);
    bool ok = broflora::removePlant(*w->world, static_cast<uint32_t>(plantIdx));
    return ev::fromBool(ok);
}

Value jsStep(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::undefined();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return args[0];
    if (args.size() < 2 || !ev::isNumber(args[1]))
        return ev::throwTypeError("step: dt must be a number");
    // A NaN, infinite or negative dt would poison simTime and every plant's
    // age for the life of the world, so it is refused rather than applied.
    const double dt = ev::toDouble(args[1]);
    if (!(dt >= 0.0 && dt <= 3.4e38))
        return ev::throwRangeError("step: dt must be a finite number >= 0");
    broflora::step(*w->world, static_cast<float>(dt));
    return args[0];
}

Value jsPlantInfo(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();
    size_t plantIdx = 0;
    if (!plantIndexOf(args[1], *w->world, plantIdx)) return ev::null();
    const auto& p = w->world->plants[plantIdx];
    ev::Persistent o(ev::createObject());
    ev::Persistent orig(makeVec3(p.origin));
    o.set(ev::setProperty(o.get(), "origin", orig.get()));
    o.set(ev::setProperty(o.get(), "age", ev::fromDouble(p.age)));
    o.set(ev::setProperty(o.get(), "flowering", ev::fromBool(p.flowering)));
    o.set(ev::setProperty(o.get(), "senescing", ev::fromBool(p.senescing)));
    o.set(ev::setProperty(o.get(), "moduleCount", ev::fromDouble(static_cast<double>(p.modules.size()))));
    o.set(ev::setProperty(o.get(), "effectiveRootVigorMax", ev::fromDouble(p.effectiveRootVigorMax)));

    if (!p.modules.empty()) {
        o.set(ev::setProperty(o.get(), "rootVigor", ev::fromDouble(p.modules.front().vigor)));
        o.set(ev::setProperty(o.get(), "rootLight", ev::fromDouble(p.modules.front().light)));
    }

    const auto& s = p.species;
    ev::Persistent sp(ev::createObject());
    sp.set(ev::setProperty(sp.get(), "maxVigor", ev::fromDouble(s.maxVigor)));
    sp.set(ev::setProperty(sp.get(), "minVigor", ev::fromDouble(s.minVigor)));
    sp.set(ev::setProperty(sp.get(), "rootVigorMax", ev::fromDouble(s.rootVigorMax)));
    sp.set(ev::setProperty(sp.get(), "apicalControl", ev::fromDouble(s.apicalControl)));
    sp.set(ev::setProperty(sp.get(), "determinacy", ev::fromDouble(s.determinacy)));
    sp.set(ev::setProperty(sp.get(), "shadeTolerance", ev::fromDouble(s.shadeTolerance)));
    sp.set(ev::setProperty(sp.get(), "apicalControlMature", ev::fromDouble(s.apicalControlMature)));
    sp.set(ev::setProperty(sp.get(), "determinacyMature", ev::fromDouble(s.determinacyMature)));
    ev::Persistent tropDir(makeVec3(s.tropismDir));
    sp.set(ev::setProperty(sp.get(), "tropismDir", tropDir.get()));
    sp.set(ev::setProperty(sp.get(), "tropismG1", ev::fromDouble(s.tropismG1)));
    sp.set(ev::setProperty(sp.get(), "tropismG2", ev::fromDouble(s.tropismG2)));
    sp.set(ev::setProperty(sp.get(), "growthScale", ev::fromDouble(s.growthScale)));
    sp.set(ev::setProperty(sp.get(), "climateOptT", ev::fromDouble(s.climateOptT)));
    sp.set(ev::setProperty(sp.get(), "climateOptP", ev::fromDouble(s.climateOptP)));
    sp.set(ev::setProperty(sp.get(), "climateSigT", ev::fromDouble(s.climateSigT)));
    sp.set(ev::setProperty(sp.get(), "climateSigP", ev::fromDouble(s.climateSigP)));
    sp.set(ev::setProperty(sp.get(), "maxAge", ev::fromDouble(s.maxAge)));
    sp.set(ev::setProperty(sp.get(), "floweringAge", ev::fromDouble(s.floweringAge)));
    sp.set(ev::setProperty(sp.get(), "seedingRadius", ev::fromDouble(s.seedingRadius)));
    sp.set(ev::setProperty(sp.get(), "moduleMatureAge", ev::fromDouble(s.moduleMatureAge)));
    sp.set(ev::setProperty(sp.get(), "pipeExp", ev::fromDouble(s.pipeExp)));
    sp.set(ev::setProperty(sp.get(), "leafDiameter", ev::fromDouble(s.leafDiameter)));
    sp.set(ev::setProperty(sp.get(), "terrainAnchorWeight", ev::fromDouble(s.terrainAnchorWeight)));
    sp.set(ev::setProperty(sp.get(), "maxSeedingSlope", ev::fromDouble(s.maxSeedingSlope)));
    sp.set(ev::setProperty(sp.get(), "distributionWeightCollisions", ev::fromDouble(s.distributionWeightCollisions)));
    sp.set(ev::setProperty(sp.get(), "distributionWeightTropism", ev::fromDouble(s.distributionWeightTropism)));
    sp.set(ev::setProperty(sp.get(), "orthotropy", ev::fromDouble(s.orthotropy)));
    sp.set(ev::setProperty(sp.get(), "individualVariation", ev::fromDouble(s.individualVariation)));
    o.set(ev::setProperty(o.get(), "species", sp.get()));

    return o.get();
}

Value jsSetClimate(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::undefined();
    auto* w = getWrapper(args[0]);
    if (w && w->world && args.size() >= 2 && ev::isObject(args[1])) {
        readClimateFields(args[1], w->world->climate);
    }
    return args[0];
}

Value jsSampleShadow(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();
    const auto& g = w->world->shadow;
    if (g.qg.empty() || g.width == 0 || g.height == 0 || g.depth == 0) return ev::null();

    bromath::Vec3 p{};
    if (!ev::isObject(args[1])) return ev::null();
    Rooted posV(args[1]);
    Value lenV = ev::getProperty(posV, "length");
    if (!ev::isNumber(lenV) || ev::toDouble(lenV) < 3.0) return ev::null();
    p.x = static_cast<float>(ev::toDouble(ev::getElement(posV, 0)));
    p.y = static_cast<float>(ev::toDouble(ev::getElement(posV, 1)));
    p.z = static_cast<float>(ev::toDouble(ev::getElement(posV, 2)));

    // The same cell the simulation's shadowCellOf picks, but range-checked
    // before the integer conversion: a far-away or NaN position has no int.
    if (!(g.cellSize > 0.0f)) return ev::null();
    const float fx = (p.x - g.origin.x) / g.cellSize;
    const float fy = (p.y - g.origin.y) / g.cellSize;
    const float fz = (p.z - g.origin.z) / g.cellSize;
    if (!(fx >= 0.0f && fx < static_cast<float>(g.width) &&
          fy >= 0.0f && fy < static_cast<float>(g.height) &&
          fz >= 0.0f && fz < static_cast<float>(g.depth))) {
        return ev::null();
    }
    const uint32_t idx = broflora::shadowIndex(g, static_cast<uint32_t>(fx),
                                               static_cast<uint32_t>(fy),
                                               static_cast<uint32_t>(fz));
    return ev::fromDouble(g.qg[idx]);
}

Value jsValidate(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();
    std::string err;
    if (broflora::validate(*w->world, &err)) return ev::null();
    return ev::fromUtf8(err);
}

Value jsSimTime(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::fromDouble(0.0);
    auto* w = getWrapper(args[0]);
    return ev::fromDouble(w && w->world ? w->world->simTime : 0.0);
}

Value jsPlantCount(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::fromDouble(0.0);
    auto* w = getWrapper(args[0]);
    return ev::fromDouble(w && w->world ? static_cast<double>(w->world->plants.size()) : 0.0);
}

Value jsPrototypeCount(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::fromDouble(0.0);
    auto* w = getWrapper(args[0]);
    return ev::fromDouble(w && w->world ? static_cast<double>(w->world->prototypes.size()) : 0.0);
}

Value jsModuleCount(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::fromDouble(0.0);
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::fromDouble(0.0);
    size_t total = 0;
    for (const auto& p : w->world->plants) total += p.modules.size();
    return ev::fromDouble(static_cast<double>(total));
}

Value jsProtoStraight(Value /*thisVal*/, std::span<const Value> /*args*/) {
    return protoToSpec(broflora::straightModule());
}

Value jsProtoFork(Value /*thisVal*/, std::span<const Value> /*args*/) {
    return protoToSpec(broflora::forkModule());
}

Value jsProtoWhorl(Value /*thisVal*/, std::span<const Value> args) {
    uint32_t arms = 3;  // whorlModule clamps it to [2, 8]
    float spread = 0.55f;
    if (!optIntArg(args, 0, "bro.flora.prototypes.whorl: arms", 0.0, kMaxUint32, arms)) return ev::undefined();
    if (args.size() >= 2 && ev::isNumber(args[1])) spread = static_cast<float>(ev::toDouble(args[1]));
    return protoToSpec(broflora::whorlModule(arms, spread));
}

Value jsProtoMonopodial(Value /*thisVal*/, std::span<const Value> args) {
    uint32_t lateralBranches = 2;  // monopodialLeaderModule clamps it to [1, 4]
    float lateralSpread = 0.7f;
    if (!optIntArg(args, 0, "bro.flora.prototypes.monopodial: lateralBranches", 0.0, kMaxUint32, lateralBranches)) {
        return ev::undefined();
    }
    if (args.size() >= 2 && ev::isNumber(args[1])) lateralSpread = static_cast<float>(ev::toDouble(args[1]));
    return protoToSpec(broflora::monopodialLeaderModule(lateralBranches, lateralSpread));
}

Value jsProtoSympodial(Value /*thisVal*/, std::span<const Value> args) {
    float primarySpread = 0.3f;
    float lateralSpread = 0.7f;
    if (args.size() >= 1 && ev::isNumber(args[0])) primarySpread = static_cast<float>(ev::toDouble(args[0]));
    if (args.size() >= 2 && ev::isNumber(args[1])) lateralSpread = static_cast<float>(ev::toDouble(args[1]));
    return protoToSpec(broflora::sympodialForkModule(primarySpread, lateralSpread));
}

Value jsProtoHorizontalTier(Value /*thisVal*/, std::span<const Value> args) {
    uint32_t arms = 3;  // horizontalTierModule clamps it to [2, 8]
    float spread = 0.85f;
    if (!optIntArg(args, 0, "bro.flora.prototypes.horizontalTier: arms", 0.0, kMaxUint32, arms)) {
        return ev::undefined();
    }
    if (args.size() >= 2 && ev::isNumber(args[1])) spread = static_cast<float>(ev::toDouble(args[1]));
    return protoToSpec(broflora::horizontalTierModule(arms, spread));
}

Value jsProtoWeeping(Value /*thisVal*/, std::span<const Value> args) {
    float spread = 0.6f;
    float droop = 0.4f;
    if (args.size() >= 1 && ev::isNumber(args[0])) spread = static_cast<float>(ev::toDouble(args[0]));
    if (args.size() >= 2 && ev::isNumber(args[1])) droop = static_cast<float>(ev::toDouble(args[1]));
    return protoToSpec(broflora::weepingModule(spread, droop));
}

Value jsSetWind(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() >= 1 && ev::isNumber(args[0])) s_windStrength = ev::toDouble(args[0]);
    if (args.size() >= 2 && ev::isNumber(args[1])) s_windDirX = ev::toDouble(args[1]);
    if (args.size() >= 3 && ev::isNumber(args[2])) s_windDirY = ev::toDouble(args[2]);
    return ev::undefined();
}

Value jsWind(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) {
        ev::Persistent o(ev::createObject());
        o.set(ev::setProperty(o.get(), "strength", ev::fromDouble(s_windStrength)));
        o.set(ev::setProperty(o.get(), "dirX", ev::fromDouble(s_windDirX)));
        o.set(ev::setProperty(o.get(), "dirY", ev::fromDouble(s_windDirY)));
        return o.get();
    }
    if (args.size() >= 1 && ev::isNumber(args[0])) s_windStrength = ev::toDouble(args[0]);
    if (args.size() >= 2 && ev::isNumber(args[1])) s_windDirX = ev::toDouble(args[1]);
    if (args.size() >= 3 && ev::isNumber(args[2])) s_windDirY = ev::toDouble(args[2]);
    return ev::undefined();
}

Value jsSetDensity(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() >= 1 && ev::isNumber(args[0])) s_density = ev::toDouble(args[0]);
    return ev::undefined();
}

Value jsDensity(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) {
        return ev::fromDouble(s_density);
    }
    if (args.size() >= 1 && ev::isNumber(args[0])) s_density = ev::toDouble(args[0]);
    return ev::undefined();
}

// swayTransforms(base, out, windFactor = 1): write `base` (a Float32Array of
// 16-float instance matrices) into `out`, bent by the global wind scaled by
// windFactor. The placement batches' per-update pass, so an instanced batch
// sways exactly as emitFoliageTransforms / emitSegmentTransforms do.
Value jsSwayTransforms(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::throwTypeError("swayTransforms: base and out are required");
    std::vector<float> buf;
    {
        // Copied out before anything allocates: the pointer is a snapshot.
        const ev::TypedArrayInfo bi = ev::typedArrayInfo(args[0]);
        if (!bi || bi.elementKind != ev::elements::Float32) {
            return ev::throwTypeError("swayTransforms: base must be a Float32Array");
        }
        const float* src = reinterpret_cast<const float*>(bi.data);
        buf.assign(src, src + bi.elementCount);
    }
    double factor = 1.0;
    if (args.size() >= 3 && ev::isNumber(args[2])) factor = ev::toDouble(args[2]);
    const size_t count = buf.size() / 16;
    applyWindToTransforms(buf.data(), count, s_windTime, s_windStrength * factor, s_windDirX, s_windDirY);

    const ev::TypedArrayInfo oi = ev::typedArrayInfo(args[1]);
    if (!oi || oi.elementKind != ev::elements::Float32) {
        return ev::throwTypeError("swayTransforms: out must be a Float32Array");
    }
    if (oi.elementCount < buf.size()) {
        return ev::throwRangeError("swayTransforms: out is shorter than base");
    }
    if (!buf.empty()) std::memcpy(oi.data, buf.data(), buf.size() * sizeof(float));
    return ev::undefined();
}

Value jsUpdate(Value /*thisVal*/, std::span<const Value> args) {
    if (!args.empty() && ev::isNumber(args[0])) {
        s_windTime += ev::toDouble(args[0]);
    }
    return ev::undefined();
}

Value jsClear(Value /*thisVal*/, std::span<const Value> /*args*/) {
    clearGlobalWind();
    return ev::undefined();
}

} // namespace

void registerFloraNativeHelpers() {
    ev::Persistent nativeObj(ev::createObject());

    // makeFunction allocates, so it runs before the receiver is read: in one
    // argument list the evaluation order is unspecified.
    #define REG_FN(name, arity, fn) \
        do { \
            Value f_ = ev::makeFunction(fn, arity, #name); \
            nativeObj.set(ev::setProperty(nativeObj.get(), #name, f_)); \
        } while (0)

    REG_FN(createWorld, 1, jsCreateWorld);
    REG_FN(addPrototype, 2, jsAddPrototype);
    REG_FN(addVoronoiSite, 4, jsAddVoronoiSite);
    REG_FN(addPlant, 2, jsAddPlant);
    REG_FN(removePlant, 2, jsRemovePlant);
    REG_FN(step, 2, jsStep);
    REG_FN(plantInfo, 2, jsPlantInfo);
    REG_FN(setClimate, 2, jsSetClimate);
    REG_FN(sampleShadow, 2, jsSampleShadow);
    REG_FN(validate, 1, jsValidate);
    REG_FN(simTime, 1, jsSimTime);
    REG_FN(plantCount, 1, jsPlantCount);
    REG_FN(prototypeCount, 1, jsPrototypeCount);
    REG_FN(moduleCount, 1, jsModuleCount);

    REG_FN(protoStraight, 0, jsProtoStraight);
    REG_FN(protoFork, 0, jsProtoFork);
    REG_FN(protoWhorl, 2, jsProtoWhorl);
    REG_FN(protoMonopodial, 2, jsProtoMonopodial);
    REG_FN(protoSympodial, 2, jsProtoSympodial);
    REG_FN(protoHorizontalTier, 2, jsProtoHorizontalTier);
    REG_FN(protoWeeping, 2, jsProtoWeeping);

    REG_FN(emitMesh, 2, jsEmitMesh);
    REG_FN(emitSegments, 1, jsEmitSegments);
    REG_FN(emitFoliage, 1, jsEmitFoliage);
    REG_FN(emitBloomAnchors, 1, jsEmitBloomAnchors);
    REG_FN(emitPlantMesh, 3, jsEmitPlantMesh);
    REG_FN(emitPlantSegments, 2, jsEmitPlantSegments);
    REG_FN(emitPlantFoliage, 2, jsEmitPlantFoliage);
    REG_FN(emitPlantBloomAnchors, 2, jsEmitPlantBloomAnchors);
    REG_FN(emitFoliageTransforms, 2, jsEmitFoliageTransforms);
    REG_FN(emitSegmentTransforms, 1, jsEmitSegmentTransforms);
    REG_FN(emitScatterSegments, 2, jsEmitScatterSegments);
    REG_FN(emitBranchTubes, 2, jsEmitBranchTubes);
    REG_FN(emitFoliageMesh, 3, jsEmitFoliageMesh);
    REG_FN(emitBloomMesh, 4, jsEmitBloomMesh);
    REG_FN(emitPlantFoliageMesh, 4, jsEmitPlantFoliageMesh);
    REG_FN(leafCluster, 2, jsLeafCluster);
    REG_FN(emitPlantSdfMesh, 3, jsEmitPlantSdfMesh);
    REG_FN(emitWorldSdfMesh, 2, jsEmitWorldSdfMesh);

    REG_FN(setWind, 3, jsSetWind);
    REG_FN(wind, 3, jsWind);
    REG_FN(setDensity, 1, jsSetDensity);
    REG_FN(density, 1, jsDensity);
    REG_FN(swayTransforms, 3, jsSwayTransforms);
    REG_FN(update, 1, jsUpdate);
    REG_FN(clear, 0, jsClear);

    #undef REG_FN

    auto g = ev::globalValue("globalThis");
    if (g.found && ev::isObject(g.value)) {
        ev::setProperty(g.value, "__bro_flora_native", nativeObj.get());
    }
    ev::registerGlobal("__bro_flora_native", nativeObj.get());
}

} // namespace broflora::api
