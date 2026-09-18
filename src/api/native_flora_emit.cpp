#include "native_flora_internal.h"

namespace broflora::api {

Value jsEmitMesh(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();
    uint32_t sides = 6u;
    if (args.size() >= 2 && ev::isNumber(args[1])) {
        uint32_t s = static_cast<uint32_t>(ev::toDouble(args[1]));
        if (s >= 3) sides = s;
    }
    auto md = std::make_unique<bromesh::MeshData>(broflora::emitWorldMesh(*w->world, sides));
    return wrapMeshData(std::move(md));
}

Value jsEmitSegments(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return createEmptyArray();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return createEmptyArray();
    auto segs = broflora::emitWorldSegments(*w->world);
    return hostArrayOf(segs.size(), [&](size_t i) {
        const auto& s = segs[i];
        ev::Persistent o(ev::createObject());
        ev::Persistent from(makeVec3(s.from));
        ev::Persistent to(makeVec3(s.to));
        o.set(ev::setProperty(o.get(), "from", from.get()));
        o.set(ev::setProperty(o.get(), "to", to.get()));
        o.set(ev::setProperty(o.get(), "radius", ev::fromDouble(s.radius)));
        o.set(ev::setProperty(o.get(), "depth", ev::fromDouble(static_cast<double>(s.depth))));
        o.set(ev::setProperty(o.get(), "parent", ev::fromDouble(static_cast<double>(s.parent))));
        return o.get();
    });
}

Value jsEmitFoliage(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return createEmptyArray();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return createEmptyArray();
    auto samples = broflora::emitWorldFoliage(*w->world);
    return hostArrayOf(samples.size(), [&](size_t i) {
        const auto& s = samples[i];
        ev::Persistent o(ev::createObject());
        o.set(ev::setProperty(o.get(), "mass", ev::fromDouble(s.mass)));
        o.set(ev::setProperty(o.get(), "age01", ev::fromDouble(s.age01)));
        o.set(ev::setProperty(o.get(), "vigor01", ev::fromDouble(s.vigor01)));
        o.set(ev::setProperty(o.get(), "light01", ev::fromDouble(s.light01)));
        o.set(ev::setProperty(o.get(), "lightExposure01", ev::fromDouble(s.lightExposure01)));
        o.set(ev::setProperty(o.get(), "senescence01", ev::fromDouble(s.senescence01)));
        o.set(ev::setProperty(o.get(), "isTerminal", ev::fromBool(s.isTerminal)));
        o.set(ev::setProperty(o.get(), "twigGrade01", ev::fromDouble(s.twigGrade01)));
        return o.get();
    });
}

Value jsEmitBloomAnchors(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return createEmptyArray();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return createEmptyArray();
    auto anchors = broflora::emitWorldBloomAnchors(*w->world);
    return hostArrayOf(anchors.size(), [&](size_t i) {
        const auto& a = anchors[i];
        ev::Persistent o(ev::createObject());
        ev::Persistent pos(makeVec3(a.position));
        ev::Persistent norm(makeVec3(a.normal));
        o.set(ev::setProperty(o.get(), "position", pos.get()));
        o.set(ev::setProperty(o.get(), "normal", norm.get()));
        o.set(ev::setProperty(o.get(), "age01", ev::fromDouble(a.age01)));
        o.set(ev::setProperty(o.get(), "vigor01", ev::fromDouble(a.vigor01)));
        o.set(ev::setProperty(o.get(), "lightExposure01", ev::fromDouble(a.lightExposure01)));
        o.set(ev::setProperty(o.get(), "senescence01", ev::fromDouble(a.senescence01)));
        return o.get();
    });
}

Value jsEmitPlantMesh(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();
    int plantIdx = static_cast<int>(ev::toDouble(args[1]));
    if (plantIdx < 0 || static_cast<size_t>(plantIdx) >= w->world->plants.size()) return ev::null();

    uint32_t sides = 6u;
    if (args.size() >= 3 && ev::isNumber(args[2])) {
        uint32_t s = static_cast<uint32_t>(ev::toDouble(args[2]));
        if (s >= 3) sides = s;
    }
    auto md = std::make_unique<bromesh::MeshData>(
        broflora::emitPlantMesh(w->world->plants[static_cast<size_t>(plantIdx)], sides));
    return wrapMeshData(std::move(md));
}

Value jsEmitPlantSegments(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return createEmptyArray();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return createEmptyArray();
    int plantIdx = static_cast<int>(ev::toDouble(args[1]));
    if (plantIdx < 0 || static_cast<size_t>(plantIdx) >= w->world->plants.size()) return createEmptyArray();

    auto segs = broflora::emitPlantSegments(w->world->plants[static_cast<size_t>(plantIdx)]);
    return hostArrayOf(segs.size(), [&](size_t i) {
        const auto& s = segs[i];
        ev::Persistent o(ev::createObject());
        ev::Persistent from(makeVec3(s.from));
        ev::Persistent to(makeVec3(s.to));
        o.set(ev::setProperty(o.get(), "from", from.get()));
        o.set(ev::setProperty(o.get(), "to", to.get()));
        o.set(ev::setProperty(o.get(), "radius", ev::fromDouble(s.radius)));
        o.set(ev::setProperty(o.get(), "depth", ev::fromDouble(static_cast<double>(s.depth))));
        o.set(ev::setProperty(o.get(), "parent", ev::fromDouble(static_cast<double>(s.parent))));
        return o.get();
    });
}

Value jsEmitPlantFoliage(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return createEmptyArray();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return createEmptyArray();
    int plantIdx = static_cast<int>(ev::toDouble(args[1]));
    if (plantIdx < 0 || static_cast<size_t>(plantIdx) >= w->world->plants.size()) return createEmptyArray();

    auto samples = broflora::emitPlantFoliage(w->world->plants[static_cast<size_t>(plantIdx)]);
    return hostArrayOf(samples.size(), [&](size_t i) {
        const auto& s = samples[i];
        ev::Persistent o(ev::createObject());
        o.set(ev::setProperty(o.get(), "mass", ev::fromDouble(s.mass)));
        o.set(ev::setProperty(o.get(), "age01", ev::fromDouble(s.age01)));
        o.set(ev::setProperty(o.get(), "vigor01", ev::fromDouble(s.vigor01)));
        o.set(ev::setProperty(o.get(), "light01", ev::fromDouble(s.light01)));
        o.set(ev::setProperty(o.get(), "lightExposure01", ev::fromDouble(s.lightExposure01)));
        o.set(ev::setProperty(o.get(), "senescence01", ev::fromDouble(s.senescence01)));
        o.set(ev::setProperty(o.get(), "isTerminal", ev::fromBool(s.isTerminal)));
        o.set(ev::setProperty(o.get(), "twigGrade01", ev::fromDouble(s.twigGrade01)));
        return o.get();
    });
}

Value jsEmitPlantBloomAnchors(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return createEmptyArray();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return createEmptyArray();
    int plantIdx = static_cast<int>(ev::toDouble(args[1]));
    if (plantIdx < 0 || static_cast<size_t>(plantIdx) >= w->world->plants.size()) return createEmptyArray();

    auto anchors = broflora::emitPlantBloomAnchors(w->world->plants[static_cast<size_t>(plantIdx)]);
    return hostArrayOf(anchors.size(), [&](size_t i) {
        const auto& a = anchors[i];
        ev::Persistent o(ev::createObject());
        ev::Persistent pos(makeVec3(a.position));
        ev::Persistent norm(makeVec3(a.normal));
        o.set(ev::setProperty(o.get(), "position", pos.get()));
        o.set(ev::setProperty(o.get(), "normal", norm.get()));
        o.set(ev::setProperty(o.get(), "age01", ev::fromDouble(a.age01)));
        o.set(ev::setProperty(o.get(), "vigor01", ev::fromDouble(a.vigor01)));
        o.set(ev::setProperty(o.get(), "lightExposure01", ev::fromDouble(a.lightExposure01)));
        o.set(ev::setProperty(o.get(), "senescence01", ev::fromDouble(a.senescence01)));
        return o.get();
    });
}

Value jsEmitFoliageTransforms(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return makeFloat32Array(nullptr, 0);
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return makeFloat32Array(nullptr, 0);

    auto segs = broflora::emitWorldSegments(*w->world);
    if (segs.empty()) return makeFloat32Array(nullptr, 0);
    auto samples = broflora::emitWorldFoliage(*w->world);

    bromesh::LeafPlacementOptions opts;
    if (args.size() >= 2 && ev::isObject(args[1])) {
        readLeafPlacementOptions(args[1], opts);
    }
    fillFoliageDensity(samples, segs.size(), opts);

    auto pl = bromesh::placeLeavesOnBranches(segs, opts);
    if (pl.transforms.empty()) return makeFloat32Array(nullptr, 0);
    return makeFloat32Array(pl.transforms.data(), pl.transforms.size());
}

Value jsEmitSegmentTransforms(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return makeFloat32Array(nullptr, 0);
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return makeFloat32Array(nullptr, 0);

    auto segs = broflora::emitWorldSegments(*w->world);
    if (segs.empty()) return makeFloat32Array(nullptr, 0);

    size_t count = segs.size();
    std::vector<float> transforms(count * 16);

    #pragma omp parallel for schedule(static) if(count > 64)
    for (int i = 0; i < static_cast<int>(count); ++i) {
        const auto& seg = segs[static_cast<size_t>(i)];
        bromath::Vec3 d = seg.to - seg.from;
        float len = bromath::vlen(d);
        if (len < 1e-6f) len = 1e-6f;
        bromath::Vec3 fwd = d * (1.0f / len);

        bromath::Vec3 worldUp{0, 1, 0};
        bromath::Vec3 side = bromath::vcross(fwd, worldUp);
        if (bromath::vdot(side, side) < 1e-8f) {
            side = bromath::vcross(fwd, bromath::Vec3{1, 0, 0});
        }
        side = bromath::vnorm(side);
        bromath::Vec3 up = bromath::vnorm(bromath::vcross(side, fwd));

        float r = seg.radius > 0.001f ? seg.radius : 0.001f;
        bromath::Vec3 origin = seg.from;

        float* o = transforms.data() + static_cast<size_t>(i) * 16;
        o[0] = side.x * r;  o[1] = up.x * r;  o[2] = fwd.x * len;  o[3] = origin.x;
        o[4] = side.y * r;  o[5] = up.y * r;  o[6] = fwd.y * len;  o[7] = origin.y;
        o[8] = side.z * r;  o[9] = up.z * r;  o[10] = fwd.z * len; o[11] = origin.z;
        o[12] = 1.0f;       o[13] = 1.0f;      o[14] = 1.0f;       o[15] = 1.0f;
    }

    return makeFloat32Array(transforms.data(), count * 16);
}

Value jsEmitScatterSegments(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::createObject();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::createObject();

    auto segs = broflora::emitWorldSegments(*w->world);
    auto samples = broflora::emitWorldFoliage(*w->world);

    bromesh::LeafPlacementOptions opts;
    if (args.size() >= 2 && ev::isObject(args[1])) {
        readLeafPlacementOptions(args[1], opts);
    }
    fillFoliageDensity(samples, segs.size(), opts);

    std::vector<int> childCount(segs.size(), 0);
    for (size_t i = 0; i < segs.size(); ++i) {
        int p = segs[i].parent;
        if (p >= 0 && static_cast<size_t>(p) < segs.size()) ++childCount[p];
    }

    std::vector<float> packed;
    std::vector<float> instSeg;
    packed.reserve(segs.size() * 8);
    float bmin[3] = { 1e30f, 1e30f, 1e30f };
    float bmax[3] = { -1e30f, -1e30f, -1e30f };

    for (size_t i = 0; i < segs.size(); ++i) {
        const auto& s = segs[i];
        bromath::Vec3 d = s.to - s.from;
        float len = bromath::vlen(d);
        if (len < 1e-6f) continue;
        if (s.depth < opts.minDepth) continue;
        if (s.radius > 0.0f && s.radius > opts.maxRadius) continue;
        if (opts.terminalOnly && childCount[i] > 0) continue;

        float weight = 1.0f;
        if (!opts.densityWeight.empty()) {
            weight = (i < opts.densityWeight.size()) ? std::max(0.0f, opts.densityWeight[i]) : 0.0f;
        }
        if (weight <= 0.0f) continue;

        float expected = len * opts.perUnitLength * weight;
        uint64_t h = opts.seed ^ (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL);
        h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL; h ^= h >> 27;
        float frac = static_cast<float>((h >> 40) * (1.0 / 16777216.0));
        int count = static_cast<int>(std::floor(expected + frac));
        if (count <= 0) continue;
        if (count > 4096) count = 4096;

        float segIdx = static_cast<float>(packed.size() / 8);
        for (int k = 0; k < count; ++k) instSeg.push_back(segIdx);

        packed.push_back(s.from.x); packed.push_back(s.from.y);
        packed.push_back(s.from.z); packed.push_back(s.radius);
        packed.push_back(d.x); packed.push_back(d.y);
        packed.push_back(d.z); packed.push_back(0.0f);

        float tox = s.to.x, toy = s.to.y, toz = s.to.z;
        bmin[0] = std::min({ bmin[0], s.from.x, tox });
        bmin[1] = std::min({ bmin[1], s.from.y, toy });
        bmin[2] = std::min({ bmin[2], s.from.z, toz });
        bmax[0] = std::max({ bmax[0], s.from.x, tox });
        bmax[1] = std::max({ bmax[1], s.from.y, toy });
        bmax[2] = std::max({ bmax[2], s.from.z, toz });
    }

    size_t segCount = packed.size() / 8;
    ev::Persistent obj(ev::createObject());
    ev::Persistent segsArr(makeFloat32Array(packed.empty() ? nullptr : packed.data(), packed.size()));
    ev::Persistent instSegArr(makeFloat32Array(instSeg.empty() ? nullptr : instSeg.data(), instSeg.size()));
    obj.set(ev::setProperty(obj.get(), "segments", segsArr.get()));
    obj.set(ev::setProperty(obj.get(), "instSeg", instSegArr.get()));
    obj.set(ev::setProperty(obj.get(), "segCount", ev::fromDouble(static_cast<double>(segCount))));
    obj.set(ev::setProperty(obj.get(), "instanceCount", ev::fromDouble(static_cast<double>(instSeg.size()))));

    if (segCount == 0) { bmin[0]=bmin[1]=bmin[2]=bmax[0]=bmax[1]=bmax[2]=0.0f; }
    ev::Persistent bminA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmin[i]); }));
    ev::Persistent bmaxA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmax[i]); }));
    obj.set(ev::setProperty(obj.get(), "boundsMin", bminA.get()));
    obj.set(ev::setProperty(obj.get(), "boundsMax", bmaxA.get()));
    return obj.get();
}

Value jsEmitBranchTubes(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::createObject();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::createObject();

    auto segs = broflora::emitWorldSegments(*w->world);
    float minRadius = 0.0f;
    if (args.size() >= 2 && ev::isObject(args[1])) {
        readFloatField(args[1], "minRadius", minRadius);
    }

    std::vector<float> packed;
    packed.reserve(segs.size() * 8);
    float bmin[3] = { 1e30f, 1e30f, 1e30f };
    float bmax[3] = { -1e30f, -1e30f, -1e30f };
    float maxR = 0.0f;

    for (size_t i = 0; i < segs.size(); ++i) {
        const auto& s = segs[i];
        bromath::Vec3 d = s.to - s.from;
        if (bromath::vlen(d) < 1e-6f) continue;
        if (s.radius < minRadius) continue;

        float rTo = s.radius;
        float rFrom = rTo;
        int p = s.parent;
        if (p >= 0 && static_cast<size_t>(p) < segs.size() && segs[p].radius > 0.0f)
            rFrom = segs[p].radius;

        packed.push_back(s.from.x); packed.push_back(s.from.y);
        packed.push_back(s.from.z); packed.push_back(rFrom);
        packed.push_back(s.to.x);   packed.push_back(s.to.y);
        packed.push_back(s.to.z);   packed.push_back(rTo);

        maxR = std::max({ maxR, rFrom, rTo });
        bmin[0] = std::min({ bmin[0], s.from.x, s.to.x });
        bmin[1] = std::min({ bmin[1], s.from.y, s.to.y });
        bmin[2] = std::min({ bmin[2], s.from.z, s.to.z });
        bmax[0] = std::max({ bmax[0], s.from.x, s.to.x });
        bmax[1] = std::max({ bmax[1], s.from.y, s.to.y });
        bmax[2] = std::max({ bmax[2], s.from.z, s.to.z });
    }

    size_t segCount = packed.size() / 8;
    if (segCount == 0) {
        bmin[0]=bmin[1]=bmin[2]=bmax[0]=bmax[1]=bmax[2]=0.0f;
    } else {
        for (int i = 0; i < 3; ++i) { bmin[i] -= maxR; bmax[i] += maxR; }
    }

    ev::Persistent obj(ev::createObject());
    ev::Persistent segsArr(makeFloat32Array(packed.empty() ? nullptr : packed.data(), packed.size()));
    obj.set(ev::setProperty(obj.get(), "segments", segsArr.get()));
    obj.set(ev::setProperty(obj.get(), "segCount", ev::fromDouble(static_cast<double>(segCount))));

    ev::Persistent bminA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmin[i]); }));
    ev::Persistent bmaxA(hostArrayOf(3, [&](size_t i) { return ev::fromDouble(bmax[i]); }));
    obj.set(ev::setProperty(obj.get(), "boundsMin", bminA.get()));
    obj.set(ev::setProperty(obj.get(), "boundsMax", bmaxA.get()));
    return obj.get();
}

Value jsEmitFoliageMesh(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();

    bromesh::MeshData leaf;
    if (!getMeshData(args[1], leaf) || leaf.empty()) return ev::null();

    auto segs = broflora::emitWorldSegments(*w->world);
    if (segs.empty()) {
        return wrapMeshData(std::make_unique<bromesh::MeshData>());
    }
    auto samples = broflora::emitWorldFoliage(*w->world);

    bromesh::LeafPlacementOptions opts;
    if (args.size() >= 3 && ev::isObject(args[2])) {
        readLeafPlacementOptions(args[2], opts);
    }
    fillFoliageDensity(samples, segs.size(), opts);

    auto md = std::make_unique<bromesh::MeshData>(
        bromesh::scatterLeaves(segs, leaf, opts));
    return wrapMeshData(std::move(md));
}

namespace {

// Append `src` to `target` placed at `pos`, its +Y turned onto `norm`, scaled
// by `scale`: the stamp emitBloomMesh makes per anchor. Normals rotate with
// the geometry; a source without normals gets the up vector.
void appendStamped(bromesh::MeshData& target, const bromesh::MeshData& src,
                   const bromath::Vec3& pos, const bromath::Vec3& norm, float scale) {
    if (src.empty()) return;
    const uint32_t baseIndex = static_cast<uint32_t>(target.vertexCount());
    const size_t nv = src.vertexCount();

    bromath::Vec3 n = norm;
    const float len = std::hypot(n.x, std::hypot(n.y, n.z));
    if (len > 1e-6f) { n.x /= len; n.y /= len; n.z /= len; }
    else { n = {0.0f, 1.0f, 0.0f}; }

    const float ny = std::max(-1.0f, std::min(1.0f, n.y));
    const float ang = std::acos(ny);
    bromath::Vec3 axis{1.0f, 0.0f, 0.0f};
    if (ang >= 1e-4f && ang <= 3.14159265f - 1e-4f) {
        axis = {n.z, 0.0f, -n.x};
        const float al = std::hypot(axis.x, axis.z);
        if (al > 1e-6f) { axis.x /= al; axis.z /= al; }
        else { axis = {1.0f, 0.0f, 0.0f}; }
    }

    const float c = std::cos(ang), s = std::sin(ang);
    const float omc = 1.0f - c;
    const float R[3][3] = {
        { c + axis.x*axis.x*omc,          axis.x*axis.y*omc - axis.z*s, axis.x*axis.z*omc + axis.y*s },
        { axis.y*axis.x*omc + axis.z*s,  c + axis.y*axis.y*omc,          axis.y*axis.z*omc - axis.x*s },
        { axis.z*axis.x*omc - axis.y*s,  axis.z*axis.y*omc + axis.x*s,  c + axis.z*axis.z*omc          }
    };

    target.positions.reserve(target.positions.size() + nv * 3);
    target.normals.reserve(target.normals.size() + nv * 3);
    if (!src.uvs.empty()) target.uvs.reserve(target.uvs.size() + src.uvs.size());
    target.indices.reserve(target.indices.size() + src.indices.size());

    for (size_t i = 0; i < nv; ++i) {
        const float px = src.positions[i * 3] * scale;
        const float py = src.positions[i * 3 + 1] * scale;
        const float pz = src.positions[i * 3 + 2] * scale;
        target.positions.push_back(R[0][0]*px + R[0][1]*py + R[0][2]*pz + pos.x);
        target.positions.push_back(R[1][0]*px + R[1][1]*py + R[1][2]*pz + pos.y);
        target.positions.push_back(R[2][0]*px + R[2][1]*py + R[2][2]*pz + pos.z);

        if (src.hasNormals()) {
            const float nx = src.normals[i * 3];
            const float nyy = src.normals[i * 3 + 1];
            const float nz = src.normals[i * 3 + 2];
            target.normals.push_back(R[0][0]*nx + R[0][1]*nyy + R[0][2]*nz);
            target.normals.push_back(R[1][0]*nx + R[1][1]*nyy + R[1][2]*nz);
            target.normals.push_back(R[2][0]*nx + R[2][1]*nyy + R[2][2]*nz);
        } else {
            target.normals.push_back(0.0f);
            target.normals.push_back(1.0f);
            target.normals.push_back(0.0f);
        }

        if (src.hasUVs()) {
            target.uvs.push_back(src.uvs[i * 2]);
            target.uvs.push_back(src.uvs[i * 2 + 1]);
        }
    }
    for (size_t i = 0; i < src.indices.size(); ++i) {
        target.indices.push_back(baseIndex + src.indices[i]);
    }
}

}  // namespace

// world.emitBloomMesh(petalMesh, centerMesh?, {bloomCap, bloomLightMin}) →
// [petals, centers]: one merged mesh per part, a petal stamp at every
// flowering anchor (strided down to bloomCap of them, the dim ones skipped)
// and, when a center mesh is given, a center stamp lifted a little along the
// anchor normal. Scale grows with the anchor's age.
Value jsEmitBloomMesh(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();

    bromesh::MeshData petal;
    if (!getMeshData(args[1], petal) || petal.empty()) return ev::null();
    bromesh::MeshData center;
    const bool hasCenter = args.size() >= 3 && ev::isObject(args[2]) && getMeshData(args[2], center) && !center.empty();

    uint32_t bloomCap = 500;
    float bloomLightMin = 0.18f;
    const size_t optsAt = 3;
    if (args.size() > optsAt && ev::isObject(args[optsAt])) {
        readUint32Field(args[optsAt], "bloomCap", bloomCap);
        readFloatField(args[optsAt], "bloomLightMin", bloomLightMin);
    }
    if (bloomCap == 0) bloomCap = 1;

    auto petals = std::make_unique<bromesh::MeshData>();
    auto centers = std::make_unique<bromesh::MeshData>();

    auto anchors = broflora::emitWorldBloomAnchors(*w->world);
    const size_t stride = (anchors.size() > bloomCap) ? (anchors.size() + bloomCap - 1) / bloomCap : 1;
    for (size_t i = 0; i < anchors.size(); i += stride) {
        const auto& a = anchors[i];
        if (a.lightExposure01 < bloomLightMin) continue;
        const float s = 0.8f + 0.5f * std::min(1.0f, a.age01);
        appendStamped(*petals, petal, a.position, a.normal, s);
        if (hasCenter) {
            const float lift = 0.012f * s;
            const bromath::Vec3 cPos = {
                a.position.x + a.normal.x * lift,
                a.position.y + a.normal.y * lift,
                a.position.z + a.normal.z * lift
            };
            appendStamped(*centers, center, cPos, a.normal, s);
        }
    }

    ev::Persistent petalsVal(wrapMeshData(std::move(petals)));
    ev::Persistent centersVal(wrapMeshData(std::move(centers)));
    return hostArrayOf(2, [&](size_t i) { return i == 0 ? petalsVal.get() : centersVal.get(); });
}

Value jsEmitPlantFoliageMesh(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 3) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();
    int plantIdx = static_cast<int>(ev::toDouble(args[1]));
    if (plantIdx < 0 || static_cast<size_t>(plantIdx) >= w->world->plants.size()) return ev::null();

    bromesh::MeshData leaf;
    if (!getMeshData(args[2], leaf) || leaf.empty()) return ev::null();

    const auto& plant = w->world->plants[static_cast<size_t>(plantIdx)];
    auto segs = broflora::emitPlantSegments(plant);
    if (segs.empty()) {
        return wrapMeshData(std::make_unique<bromesh::MeshData>());
    }
    auto samples = broflora::emitPlantFoliage(plant);

    bromesh::LeafPlacementOptions opts;
    if (args.size() >= 4 && ev::isObject(args[3])) {
        readLeafPlacementOptions(args[3], opts);
    }
    fillFoliageDensity(samples, segs.size(), opts);

    auto md = std::make_unique<bromesh::MeshData>(
        bromesh::scatterLeaves(segs, leaf, opts));
    return wrapMeshData(std::move(md));
}

Value jsLeafCluster(Value /*thisVal*/, std::span<const Value> args) {
    broflora::Phyllotaxy phyl = broflora::Phyllotaxy::Alternate;
    broflora::LeafClusterOptions opts;
    if (!args.empty()) {
        if (ev::isObject(args[0]) && !ev::isNumber(args[0]) && !ev::isString(args[0])) {
            readLeafClusterOptions(args[0], opts);
            Value pv = ev::getProperty(args[0], "phyllotaxy");
            if (!ev::isUndefined(pv) && !ev::isNull(pv)) {
                phyl = parsePhyllotaxy(pv);
            }
        } else {
            phyl = parsePhyllotaxy(args[0]);
            if (args.size() >= 2 && ev::isObject(args[1])) {
                readLeafClusterOptions(args[1], opts);
            }
        }
    }
    auto md = std::make_unique<bromesh::MeshData>(broflora::leafCluster(phyl, opts));
    return wrapMeshData(std::move(md));
}

Value jsEmitPlantSdfMesh(Value /*thisVal*/, std::span<const Value> args) {
    if (args.size() < 2) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();
    int plantIdx = static_cast<int>(ev::toDouble(args[1]));
    if (plantIdx < 0 || static_cast<size_t>(plantIdx) >= w->world->plants.size()) return ev::null();

    broflora::SdfMeshOptions opts;
    if (args.size() >= 3 && ev::isObject(args[2])) {
        readSdfMeshOptions(args[2], opts);
    }
    auto md = std::make_unique<bromesh::MeshData>(
        broflora::emitPlantSdfMesh(w->world->plants[static_cast<size_t>(plantIdx)], opts));
    return wrapMeshData(std::move(md));
}

Value jsEmitWorldSdfMesh(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::null();
    auto* w = getWrapper(args[0]);
    if (!w || !w->world) return ev::null();

    broflora::SdfMeshOptions opts;
    if (args.size() >= 2 && ev::isObject(args[1])) {
        readSdfMeshOptions(args[1], opts);
    }
    auto md = std::make_unique<bromesh::MeshData>(
        broflora::emitWorldSdfMesh(*w->world, opts));
    return wrapMeshData(std::move(md));
}

} // namespace broflora::api
