// Crown clumps from JS (broflora/clumps.h): the clump-mode branch of the leaf
// emitters (emitScatterSegments, emitFoliageMesh, emitPlantFoliageMesh,
// emitFoliageTransforms, emitFoliageVegetation) and world.emitClumps().

#include "native_flora_internal.h"
#include "broflora/forest_records.h"

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
// (twig dir, 0); per leaf its clump; 16 vegetation floats per clump; and
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
    vegetation.reserve(clumps.size() * broflora::kSegmentVegetationFloats);
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
    if (vegetation.size() == clumps.size() * broflora::kSegmentVegetationFloats) {
        ev::Persistent vegArr(makeFloat32Array(vegetation.empty() ? nullptr : vegetation.data(), vegetation.size()));
        obj.set(ev::setProperty(obj.get(), "vegetation", vegArr.get()));
    }
    ev::Persistent bminA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmin[i]); }));
    ev::Persistent bmaxA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmax[i]); }));
    obj.set(ev::setProperty(obj.get(), "boundsMin", bminA.get()));
    obj.set(ev::setProperty(obj.get(), "boundsMax", bmaxA.get()));
    return obj.get();
}

// world.emitForest(opts) -> every plant as a specimen for bro.scene's forest
// node (broflora/forest.h): typed arrays plus the leaf placement, and one
// instance per specimen where its plant stands. The clump options read as
// the foliage emits'; `minRadius` drops thin bark; `shuffleSeed` seeds the
// leaves' thinning order (and the blooms'). A flowering plant whose species
// bears blooms carries them too: `blooms` and `specimenBlooms`.
Value jsEmitForest(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::createObject();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::createObject();
    const Rooted optsV(args.size() >= 2 ? args[1] : ev::undefined());
    broflora::ForestOptions fo;
    if (!readClumpOptions(optsV, fo.clumps, fo.leaves)) return ev::undefined();
    const double g = getGlobalDensity();
    if (g >= 0.0) fo.leaves.perArea *= static_cast<float>(g);
    if (ev::isObject(optsV)) {
        readFloatField(optsV, "minRadius", fo.minRadius);
        uint64_t s = fo.seed;
        if (!seedField(optsV, "shuffleSeed", "emitForest opts.shuffleSeed", s)) return ev::undefined();
        fo.seed = s;
        // Which plants: `plants` (indices), and of them `leafForm`'s.
        Rooted plantsV(ev::getProperty(optsV, "plants"));
        if (ev::isObject(plantsV)) {
            uint32_t n = 0;
            if (!lengthOf(plantsV, "emitForest opts.plants.length", n)) return ev::undefined();
            fo.plants.reserve(std::min(n, kReserveCap));
            for (uint32_t i = 0; i < n; ++i) {
                int64_t v = 0;
                if (!intValue(ev::getElement(plantsV, i), "emitForest opts.plants[i]", 0.0, kMaxUint32, v))
                    return ev::undefined();
                fo.plants.push_back(static_cast<uint32_t>(v));
            }
            // An empty list emits nothing (not every plant).
            if (n == 0) fo.plants.push_back(UINT32_MAX);
        }
        Value lf = ev::getProperty(optsV, "leafForm");
        if (!ev::isUndefined(lf) && !leafFormOf(lf, fo.leafForm)) {
            ev::throwRangeError("emitForest opts.leafForm must be 'broad', 'needle', 'scale' or 0..2");
            return ev::undefined();
        }
    }
    const broflora::ForestEmit f = broflora::emitForest(*w->world, fo);

    // Per specimen: specimens, specimenLooks, specimenLeaves, specimenBlooms
    // and instances, laid out as broflora/forest_records.h says.
    const broflora::forest_records::Packed packed = broflora::forest_records::pack(f);
    ev::Persistent obj(ev::createObject());
    const auto put = [&](const char* name, const std::vector<float>& v) {
        ev::Persistent a(makeFloat32Array(v.empty() ? nullptr : v.data(), v.size()));
        obj.set(ev::setProperty(obj.get(), name, a.get()));
    };
    put("specimens", packed.specimens);
    put("specimenLooks", packed.looks);
    put("specimenLeaves", packed.leafLooks);
    put("instances", packed.instances);
    put("segments", f.segments);
    put("tangents", f.tangents);
    put("segmentVegetation", f.segmentVegetation);
    put("keepRadius", f.keepRadius);
    put("clumps", f.clumps);
    put("clumpVegetation", f.clumpVegetation);
    put("specimenBlooms", packed.blooms);
    put("blooms", f.blooms);
    {
        // Leaf (clump, key) pairs stay integers: a world's keys pass 2^24.
        ev::Persistent a(makeUint32Array(f.leaves.empty() ? nullptr : f.leaves.data(), f.leaves.size()));
        obj.set(ev::setProperty(obj.get(), "leaves", a.get()));
    }
    if (fo.leafForm >= 0) {
        // The form it was filtered to (bro.scene's createForest draws it).
        static const char* const kForms[] = {"broad", "needle", "scale"};
        Value lfName = ev::fromUtf8(kForms[std::clamp(fo.leafForm, 0, 2)]);
        obj.set(ev::setProperty(obj.get(), "leafForm", lfName));
    }
    obj.set(ev::setProperty(obj.get(), "specimenCount", ev::fromDouble(static_cast<double>(f.specimens.size()))));
    obj.set(ev::setProperty(obj.get(), "leafCount", ev::fromDouble(static_cast<double>(f.leaves.size() / broflora::forest_records::leaf::kValues))));
    obj.set(ev::setProperty(obj.get(), "bloomCount", ev::fromDouble(static_cast<double>(f.blooms.size() / broflora::forest_records::bloom::kFloats))));
    obj.set(ev::setProperty(obj.get(), "seed", ev::fromDouble(static_cast<double>(f.leafOptions.seed))));
    obj.set(ev::setProperty(obj.get(), "upBias", ev::fromDouble(f.leafOptions.upBias)));
    obj.set(ev::setProperty(obj.get(), "tiltJitter", ev::fromDouble(f.leafOptions.tiltJitter)));
    obj.set(ev::setProperty(obj.get(), "shell", ev::fromDouble(f.leafOptions.shell)));
    obj.set(ev::setProperty(obj.get(), "baseScale", ev::fromDouble(f.leafOptions.baseScale)));
    obj.set(ev::setProperty(obj.get(), "scaleJitter", ev::fromDouble(f.leafOptions.scaleJitter)));
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
