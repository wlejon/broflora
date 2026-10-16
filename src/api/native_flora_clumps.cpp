// Crown clumps from JS (broflora/clumps.h): the clump-mode branch of the leaf
// emitters (emitScatterSegments, emitFoliageMesh, emitPlantFoliageMesh,
// emitFoliageTransforms, emitFoliageVegetation) and world.emitClumps().

#include "native_flora_internal.h"

namespace broflora::api {

bool clumpModeOf(Value opts) {
    bool clumps = true;
    if (ev::isObject(opts)) readBoolField(opts, "clumps", clumps);
    return clumps;
}

bool readClumpOptions(Value in, broflora::ClumpOptions& c, bromesh::LeafClumpOptions& l) {
    if (!ev::isObject(in)) return true;
    Rooted o(in);
    readFloatField(o, "clumpRadius", c.radius);
    readFloatField(o, "clumpRadiusOfCrown", c.radiusOfCrown);
    readFloatField(o, "clumpJitter", c.radiusJitter);
    readFloatField(o, "clumpSpacing", c.spacing);
    readFloatField(o, "maxRadius", c.maxRadius);
    if (!intField(o, "minDepth", "clump opts.minDepth", -2147483648.0, 2147483647.0, c.minDepth)) return false;
    readFloatField(o, "minDensity", c.minDensity);
    {
        // densityWeight reads as the spray's does.
        bromesh::LeafPlacementOptions spray;
        if (!readLeafPlacementOptions(o, spray)) return false;
        c.densityWeight = std::move(spray.densityWeight);
    }
    readFloatField(o, "perArea", l.perArea);
    readFloatField(o, "upBias", l.upBias);
    readFloatField(o, "tiltJitter", l.tiltJitter);
    readFloatField(o, "shell", l.shell);
    readFloatField(o, "baseScale", l.baseScale);
    readFloatField(o, "scaleJitter", l.scaleJitter);
    uint64_t seed = 0;
    if (!seedField(o, "seed", "clump opts.seed", seed)) return false;
    c.seed = seed;
    // The GPU draws from a 32-bit hash: keep the seed it can carry.
    l.seed = static_cast<uint32_t>(seed);
    return true;
}

bool clumpLeaves(const broflora::WorldState& world, const broflora::Plant* plant, Value opts,
                 std::vector<bromesh::LeafClump>& clumps, bromesh::LeafClumpOptions& leafOpts,
                 bromesh::LeafPlacements& pl, std::vector<float>* veg) {
    broflora::ClumpOptions co;
    if (!readClumpOptions(opts, co, leafOpts)) return false;
    const double g = getGlobalDensity();
    if (g >= 0.0) leafOpts.perArea *= static_cast<float>(g);
    clumps = plant ? broflora::emitPlantClumps(*plant, co) : broflora::emitWorldClumps(world, co);
    pl = bromesh::placeLeavesInClumps(clumps, leafOpts);
    if (veg) {
        *veg = broflora::clumpLeafVegetation(pl, clumps, plant ? broflora::emitPlantSway(*plant)
                                                               : broflora::emitWorldSway(world));
    }
    return true;
}

// The clump-mode emitScatterSegments object: per clump (centre, radius),
// (twig dir, 0); per leaf its clump; 12 vegetation floats per clump; and
// the placement parameters the scene's scatter node reads, so the object
// alone configures it.
Value scatterClumps(const broflora::WorldState& world, Value opts) {
    bromesh::LeafClumpOptions lo;
    broflora::ClumpOptions co;
    if (!readClumpOptions(opts, co, lo)) return ev::undefined();
    const double g = getGlobalDensity();
    if (g >= 0.0) lo.perArea *= static_cast<float>(g);
    const auto clumps = broflora::emitWorldClumps(world, co);
    const auto sway = broflora::emitWorldSway(world);

    std::vector<float> packed, instSeg, vegetation;
    packed.reserve(clumps.size() * 8);
    vegetation.reserve(clumps.size() * 12);
    float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < clumps.size(); ++i) {
        const auto& c = clumps[i];
        const int n = bromesh::leafClumpCount(c, static_cast<uint32_t>(i), lo);
        for (int k = 0; k < n; ++k) instSeg.push_back(static_cast<float>(i));
        packed.insert(packed.end(), {c.center.x, c.center.y, c.center.z, c.radius, c.dir.x, c.dir.y, c.dir.z, 0.0f});
        if (c.segment >= 0 && static_cast<size_t>(c.segment) < sway.size()) {
            broflora::appendClumpVegetation(vegetation, sway[static_cast<size_t>(c.segment)]);
        }
        const float r = c.radius * 1.15f;
        const float p[3] = {c.center.x, c.center.y, c.center.z};
        for (int a = 0; a < 3; ++a) {
            bmin[a] = std::min(bmin[a], p[a] - r);
            bmax[a] = std::max(bmax[a], p[a] + r);
        }
    }
    // A leaf reaches past its origin by its own length.
    const float leafPad = lo.baseScale * (1.0f + lo.scaleJitter);
    for (int a = 0; a < 3; ++a) { bmin[a] -= leafPad; bmax[a] += leafPad; }
    if (clumps.empty()) { bmin[0] = bmin[1] = bmin[2] = bmax[0] = bmax[1] = bmax[2] = 0.0f; }

    ev::Persistent obj(ev::createObject());
    ev::Persistent segsArr(makeFloat32Array(packed.empty() ? nullptr : packed.data(), packed.size()));
    ev::Persistent instArr(makeFloat32Array(instSeg.empty() ? nullptr : instSeg.data(), instSeg.size()));
    obj.set(ev::setProperty(obj.get(), "segments", segsArr.get()));
    obj.set(ev::setProperty(obj.get(), "instSeg", instArr.get()));
    obj.set(ev::setProperty(obj.get(), "segCount", ev::fromDouble(static_cast<double>(clumps.size()))));
    obj.set(ev::setProperty(obj.get(), "instanceCount", ev::fromDouble(static_cast<double>(instSeg.size()))));
    obj.set(ev::setProperty(obj.get(), "clumps", ev::fromBool(true)));
    obj.set(ev::setProperty(obj.get(), "seed", ev::fromDouble(static_cast<double>(lo.seed))));
    obj.set(ev::setProperty(obj.get(), "upBias", ev::fromDouble(lo.upBias)));
    obj.set(ev::setProperty(obj.get(), "tiltJitter", ev::fromDouble(lo.tiltJitter)));
    obj.set(ev::setProperty(obj.get(), "shell", ev::fromDouble(lo.shell)));
    obj.set(ev::setProperty(obj.get(), "baseScale", ev::fromDouble(lo.baseScale)));
    obj.set(ev::setProperty(obj.get(), "scaleJitter", ev::fromDouble(lo.scaleJitter)));
    if (vegetation.size() == clumps.size() * 12) {
        ev::Persistent vegArr(makeFloat32Array(vegetation.empty() ? nullptr : vegetation.data(), vegetation.size()));
        obj.set(ev::setProperty(obj.get(), "vegetation", vegArr.get()));
    }
    ev::Persistent bminA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmin[i]); }));
    ev::Persistent bmaxA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmax[i]); }));
    obj.set(ev::setProperty(obj.get(), "boundsMin", bminA.get()));
    obj.set(ev::setProperty(obj.get(), "boundsMax", bmaxA.get()));
    return obj.get();
}

// world.emitClumps(opts) -> [{ center, radius, dir, density, segment }]
Value jsEmitClumps(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return createEmptyArray();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return createEmptyArray();
    broflora::ClumpOptions co;
    bromesh::LeafClumpOptions lo;
    if (!readClumpOptions(args.size() >= 2 ? args[1] : ev::undefined(), co, lo)) return ev::undefined();
    const auto clumps = broflora::emitWorldClumps(*w->world, co);
    return hostArrayOf(clumps.size(), [&](size_t i) {
        const auto& c = clumps[i];
        ev::Persistent o(ev::createObject());
        ev::Persistent center(makeVec3(c.center));
        ev::Persistent dir(makeVec3(c.dir));
        o.set(ev::setProperty(o.get(), "center", center.get()));
        o.set(ev::setProperty(o.get(), "radius", ev::fromDouble(c.radius)));
        o.set(ev::setProperty(o.get(), "dir", dir.get()));
        o.set(ev::setProperty(o.get(), "density", ev::fromDouble(c.density)));
        o.set(ev::setProperty(o.get(), "segment", ev::fromDouble(static_cast<double>(c.segment))));
        return o.get();
    });
}

} // namespace broflora::api
