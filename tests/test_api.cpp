#include "test_framework.h"
#include "broflora/api/api.h"
#include "embed/embed.h"
#include "eval/eval.h"
#include "native_flora_internal.h"

#include <cmath>
#include <iostream>

namespace ev = bronze::embed;
using Value = bronze::Value;

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

// Runs `script` in the current realm; it must evaluate to "SUCCESS".
static void runScript(const char* what, const char* script) {
    ev::CallResult res = bronze::eval::evalScript(script);
    if (res.thrown || ev::toUtf8(res.value) != "SUCCESS") {
        std::cerr << what << " script: " << ev::toUtf8(res.value) << std::endl;
        std::exit(1);
    }
}

// The embed-level half: bro.flora is mounted and callable from C++. Values
// read here are rooted, since every getProperty/call may move the heap.
static void test_api_embed_surface() {
    ev::GlobalValue broG = ev::globalValue("bro");
    TEST_CHECK(broG.found);
    TEST_CHECK(ev::isObject(broG.value));
    ev::Persistent flora(ev::getProperty(broG.value, "flora"));
    TEST_CHECK(ev::isObject(flora.get()));
    TEST_CHECK(ev::toBool(ev::getProperty(flora.get(), "available")) == true);

    ev::Persistent protos(ev::getProperty(flora.get(), "prototypes"));
    TEST_CHECK(ev::isObject(protos.get()));
    ev::Persistent straightFn(ev::getProperty(protos.get(), "straight"));
    TEST_CHECK(ev::isFunction(straightFn.get()));
    ev::CallResult straightRes = ev::call(straightFn.get(), protos.get(), {});
    TEST_CHECK(!straightRes.thrown);
    ev::Persistent straight(straightRes.value);
    ev::Persistent nodes(ev::getProperty(straight.get(), "nodes"));
    TEST_CHECK(ev::isObject(nodes.get()));
    TEST_CHECK(ev::toDouble(ev::getProperty(nodes.get(), "length")) >= 2.0);

    ev::Persistent createWorldFn(ev::getProperty(flora.get(), "createWorld"));
    TEST_CHECK(ev::isFunction(createWorldFn.get()));
    ev::Persistent opts(ev::createObject());
    opts.set(ev::setProperty(opts.get(), "rngSeed", ev::fromDouble(12345.0)));
    Value optVal = opts.get();
    ev::CallResult worldRes = ev::call(createWorldFn.get(), flora.get(), std::span<const Value>(&optVal, 1));
    TEST_CHECK(!worldRes.thrown);
    ev::Persistent world(worldRes.value);
    TEST_CHECK(ev::isObject(world.get()));
    TEST_CHECK(ev::toDouble(ev::getProperty(world.get(), "prototypeCount")) == 0.0);

    ev::Persistent addProtoFn(ev::getProperty(world.get(), "addPrototype"));
    TEST_CHECK(ev::isFunction(addProtoFn.get()));
    Value protoArg = straight.get();
    ev::CallResult addProtoRes = ev::call(addProtoFn.get(), world.get(), std::span<const Value>(&protoArg, 1));
    TEST_CHECK(!addProtoRes.thrown);
    TEST_CHECK(ev::toDouble(addProtoRes.value) == 0.0);
    TEST_CHECK(ev::toDouble(ev::getProperty(world.get(), "prototypeCount")) == 1.0);
}

// The world lifecycle, driven from JS.
static void test_api_world_lifecycle() {
    runScript("world lifecycle", R"JS(
        const F = bro.flora;
        const fail = (m) => { throw new Error(m); };
        const w = F.createWorld({ rngSeed: 12345 });
        const pi = w.addPrototype(F.prototypes.straight());
        if (pi !== 0) fail("addPrototype returned " + pi);
        if (w.prototypeCount !== 1) fail("prototypeCount");
        w.addVoronoiSite(pi, 0.5, 0.5);

        const idx = w.addPlant({ origin: [0, 0, 0], prototypeIndex: pi,
                                 species: { apicalControl: 0.8, determinacy: 0.2 } });
        if (idx !== 0) fail("addPlant returned " + idx);
        if (w.plantCount !== 1) fail("plantCount");
        if (w.validate() !== null) fail("validate: " + w.validate());
        for (let i = 0; i < 20; i++) {
            if (w.step(0.1) !== w) fail("step should return this");
        }
        if (!(w.simTime > 1.9)) fail("simTime " + w.simTime);

        const info = w.plantInfo(0);
        if (!info || !(info.moduleCount > 0)) fail("plantInfo.moduleCount");
        if (w.plantInfo(99) !== null) fail("plantInfo out of range should be null");

        const mesh = w.emitMesh(6);
        if (!(mesh.vertexCount > 0)) fail("emitMesh produced nothing");
        const psdf = w.emitPlantSdfMesh(0, { voxelSize: 0.01 });
        if (!(psdf.vertexCount > 0)) fail("emitPlantSdfMesh produced nothing");
        const wsdf = w.emitWorldSdfMesh({ voxelSize: 0.01 });
        if (!(wsdf.vertexCount > 0)) fail("emitWorldSdfMesh produced nothing");
        const lc = F.leafCluster();
        if (!(lc.vertexCount > 0)) fail("leafCluster() produced nothing");

        if (w.removePlant(0) !== true) fail("removePlant(0)");
        if (w.plantCount !== 0) fail("plantCount after removePlant");
        if (w.removePlant(0) !== false) fail("removePlant of a missing plant should be false");
        "SUCCESS";
    )JS");
}

// Every option reader in one script: under BRONZE_GC_STRESS=1 a reader that
// held its option object as a plain Value across the reads would misread or
// crash here.
static void test_api_option_readers() {
    runScript("option-reader", R"JS(
        const F = bro.flora;
        const fail = (m) => { throw new Error(m); };

        // createWorld reads rngSeed, climate and the shadow grid.
        const w = F.createWorld({
            rngSeed: 7,
            climate: { annualTempBase: 14, annualPrecip: 900, tempLapsePerUnit: 0.01 },
            shadow: { origin: [-4, 0, -4], cellSize: 0.5, width: 16, height: 16, depth: 16, fill: 0.75 }
        });
        if (!(w instanceof F.FloraWorld)) fail("createWorld did not return a FloraWorld");
        const q = w.sampleShadow([0.1, 0.1, 0.1]);
        if (q !== 0.75) fail("shadow fill/origin/dims not read: sampleShadow = " + q);
        if (w.sampleShadow([100, 0, 0]) !== null) fail("sampleShadow outside the grid should be null");
        if (w.setClimate({ annualTempBase: 10 }) !== w) fail("setClimate should return this");

        // A hand-written prototype: nodes, edges in both forms, terminals.
        const pi = w.addPrototype({
            name: "custom",
            nodes: [
                { position: [0, 0, 0], ageAtBirth: 0, lengthMax: 1, thickening: 1 },
                { position: [0, 0.5, 0], ageAtBirth: 0.2, lengthMax: 1, thickening: 1 },
                { position: [0.2, 1, 0], ageAtBirth: 0.4, lengthMax: 1, thickening: 1 }
            ],
            edges: [[0, 1], { a: 1, b: 2 }],
            rootNode: 0,
            terminalNodes: [2]
        });
        if (pi !== 0) fail("addPrototype returned " + pi);
        const straight = w.addPrototype(F.prototypes.straight());
        if (straight !== 1) fail("second addPrototype returned " + straight);
        w.addVoronoiSite(pi, 0.5, 0.5);
        w.addVoronoiSite(straight, 0.8, 0.3);

        // addPlant reads origin, age, species (incl. the tropismDir vector).
        const idx = w.addPlant({
            origin: [1, 0, 2], age: 0, prototypeIndex: straight, initialVigor: 3,
            species: { apicalControl: 0.8, determinacy: 0.2, tropismDir: [0, 1, 0.5], maxAge: 90 }
        });
        if (idx !== 0) fail("addPlant returned " + idx);
        const info0 = w.plantInfo(0);
        if (info0.origin[0] !== 1 || info0.origin[2] !== 2) fail("addPlant origin not read: " + info0.origin);
        if (info0.species.tropismDir[2] !== 0.5) fail("species.tropismDir not read: " + info0.species.tropismDir);
        if (info0.species.maxAge !== 90) fail("species.maxAge not read");
        if (Math.abs(info0.rootVigor - 3) > 1e-6) fail("initialVigor not read: " + info0.rootVigor);
        for (let i = 0; i < 40; i++) w.step(0.25);
        if (w.validate() !== null) fail("validate: " + w.validate());

        // Placement options (densityWeight is an array read element-wise).
        const segs = w.emitSegments();
        if (segs.length === 0) fail("no segments after 10 s of growth");
        const dense = w.emitFoliageTransforms({ perUnitLength: 40, seed: 3, terminalOnly: false, maxRadius: 10 });
        const weights = new Array(segs.length).fill(0);
        const none = w.emitFoliageTransforms({ perUnitLength: 40, seed: 3, terminalOnly: false, maxRadius: 10, densityWeight: weights });
        if (!(dense instanceof Float32Array) || dense.length === 0) fail("emitFoliageTransforms produced nothing");
        if (none.length !== 0) fail("an all-zero densityWeight should place no leaves, got " + none.length / 16);
        const sc = w.emitScatterSegments({ perUnitLength: 40, seed: 3, terminalOnly: false, maxRadius: 10 });
        if (sc.segCount === 0 || sc.boundsMin.length !== 3) fail("emitScatterSegments shape");
        const tubes = w.emitBranchTubes({ minRadius: 1e9 });
        if (tubes.segCount !== 0) fail("emitBranchTubes ignored minRadius");

        // leafCluster: options object with phyllotaxy + shape, and the
        // (phyllotaxy, opts) form.
        const lcA = F.leafCluster({ phyllotaxy: "opposite", count: 6, leafShape: "needle", includeTwigMesh: true });
        const lcB = F.leafCluster(F.phyllotaxy.spiral, { count: 3, shape: "lobed" });
        if (!(lcA.vertexCount > 0) || !(lcB.vertexCount > 0)) fail("leafCluster produced no geometry");
        const lc6 = F.leafCluster(F.phyllotaxy.spiral, { count: 6, includeTwigMesh: false });
        const lc2 = F.leafCluster(F.phyllotaxy.spiral, { count: 2, includeTwigMesh: false });
        if (!(lc6.vertexCount > lc2.vertexCount)) fail("leafCluster ignored count");

        // Mesh-object inputs: a leaf and petal read field by field.
        const leaf = F.leafCluster(F.phyllotaxy.alternate, { count: 1, includeTwigMesh: false });
        const fm = w.emitFoliageMesh(leaf, { perUnitLength: 10, seed: 1, terminalOnly: false, maxRadius: 10 });
        if (!fm || !(fm.vertexCount > 0)) fail("emitFoliageMesh produced nothing");
        const bloom = w.emitBloomMesh(leaf, null, { bloomCap: 4, bloomLightMin: 0 });
        if (!Array.isArray(bloom) || bloom.length !== 2) fail("emitBloomMesh should return [petals, centers]");

        const sdf = w.emitWorldSdfMesh({ voxelSize: 0.02, smoothK: 0.01, useSurfaceNets: true, margin: 0.05 });
        if (!(sdf.vertexCount > 0)) fail("emitWorldSdfMesh produced nothing");
        "SUCCESS";
    )JS");
}

// Every wind-bent instance path shares one model: a placement batch built
// from the wind-free emitSegmentTransforms / emitFoliageTransforms output and
// swayed by update() must equal what the emitter itself returns under the
// same wind, float for float.
static void test_api_wind_paths_agree() {
    runScript("wind paths", R"JS(
        const F = bro.flora;
        const fail = (m) => { throw new Error(m); };
        F.clear();
        const w = F.createWorld({ rngSeed: 99 });
        const pi = w.addPrototype(F.prototypes.whorl(3, 0.6));
        w.addVoronoiSite(pi, 0.5, 0.5);
        w.addPlant({ origin: [0.5, 0, -0.25], prototypeIndex: pi });
        for (let i = 0; i < 30; i++) w.step(0.25);
        const leafOpts = { perUnitLength: 20, seed: 5, terminalOnly: false, maxRadius: 10 };

        const segRest = w.emitSegmentTransforms();
        const leafRest = w.emitFoliageTransforms(leafOpts);
        if (segRest.length === 0 || leafRest.length === 0) fail("nothing emitted");

        F.setWind(1.7, 0.6, -0.8);
        F.update(0.37);
        const segWind = w.emitSegmentTransforms();
        const leafWind = w.emitFoliageTransforms(leafOpts);

        const segBatch = F.addPlacement({ transforms: segRest });
        const leafBatch = F.addPlacement({ transforms: leafRest, windFactor: 1 });
        F.update(0);  // re-sway at the same clock
        const same = (a, b, what) => {
            if (a.length !== b.length) fail(what + ": length " + a.length + " vs " + b.length);
            for (let i = 0; i < a.length; i++) {
                if (a[i] !== b[i]) fail(what + ": float " + i + " is " + a[i] + ", emitter gave " + b[i]);
            }
        };
        same(segBatch.transforms, segWind, "segment batch");
        same(leafBatch.transforms, leafWind, "leaf batch");
        let moved = false;
        for (let i = 0; i < segRest.length; i++) if (segRest[i] !== segWind[i]) moved = true;
        if (!moved) fail("the wind bent nothing");

        // windFactor scales the strength; 0 restores the rest pose.
        const half = F.addPlacement({ transforms: segRest, windFactor: 0.5 });
        F.setWind(3.4, 0.6, -0.8);
        F.update(0);
        same(half.transforms, segWind, "windFactor 0.5 of 3.4");
        F.setWind(0, 0, 0);
        F.update(0);
        same(segBatch.transforms, segRest, "calm batch");
        F.clear();
        "SUCCESS";
    )JS");
}

// Every 16-float instance the API hands out is bro's InstancedMeshNode
// layout: a row-major 3x4 affine plus an RGBA tint that defaults to white.
// The batch builder used to write 0, 0, 0, 1 there (a 4x4 bottom row),
// which InstancedMeshNode draws black.
static void test_api_instance_layout() {
    runScript("instance layout", R"JS(
        const F = bro.flora;
        const fail = (m) => { throw new Error(m); };
        F.clear();
        const white = (t, what) => {
            if (t.length % 16 !== 0 || t.length === 0) fail(what + ": length " + t.length);
            for (let o = 0; o < t.length; o += 16) {
                for (let k = 12; k < 16; k++) {
                    if (t[o + k] !== 1) fail(what + ": instance " + o / 16 + " float " + k + " is " + t[o + k]);
                }
            }
        };
        const fromPoints = F.addPlacement({ transforms: [[1, 2, 3], [4, 5, 6]] });
        white(fromPoints.transforms, "points batch");
        white(fromPoints.baseTransforms, "points batch rest pose");
        const t = fromPoints.transforms;
        if (t[3] !== 1 || t[7] !== 2 || t[11] !== 3 || t[16 + 3] !== 4) fail("translation not at 3/7/11");
        if (t[0] !== 1 || t[5] !== 1 || t[10] !== 1 || t[1] !== 0 || t[4] !== 0) fail("basis not identity");
        white(F.addPlacement({ count: 3 }).transforms, "count batch");

        const w = F.createWorld({ rngSeed: 3 });
        const pi = w.addPrototype(F.prototypes.straight());
        w.addPlant({ origin: [0, 0, 0], prototypeIndex: pi });
        for (let i = 0; i < 10; i++) w.step(0.25);
        white(w.emitSegmentTransforms(), "emitSegmentTransforms");
        const ones = new Array(w.emitSegments().length).fill(1);
        white(w.emitFoliageTransforms({ perUnitLength: 20, terminalOnly: false, maxRadius: 10, minDepth: 0,
                                        densityWeight: ones }), "emitFoliageTransforms");
        F.clear();
        "SUCCESS";
    )JS");
}

// Counts, sizes and node references are validated, not cast: a negative,
// NaN or fractional one used to wrap to ~4e9 in a static_cast (a 4-billion
// sided tube, a 4-billion-cell shadow grid, an out-of-range node index the
// simulation reads unchecked). Plant indices keep their documented
// null / false / [] answer but no longer go through an int cast.
static void test_api_count_validation() {
    runScript("count validation", R"JS(
        const F = bro.flora;
        const fail = (m) => { throw new Error(m); };
        const throwsKind = (fn, Kind, what) => {
            try { fn(); } catch (e) {
                if (!(e instanceof Kind)) fail(what + ": threw the wrong kind: " + e);
                return;
            }
            fail(what + ": did not throw");
        };
        F.clear();
        const w = F.createWorld({ rngSeed: 1 });
        const pi = w.addPrototype(F.prototypes.straight());
        w.addPlant({ origin: [0, 0, 0], prototypeIndex: pi });
        for (let i = 0; i < 8; i++) w.step(0.25);

        for (const bad of [-1, NaN, 2.5, 2, Infinity, 1e10]) {
            throwsKind(() => w.emitMesh(bad), RangeError, "emitMesh(" + bad + ")");
            throwsKind(() => w.emitPlantMesh(0, bad), RangeError, "emitPlantMesh(0, " + bad + ")");
        }
        throwsKind(() => w.emitMesh("6"), TypeError, "emitMesh('6')");
        if (!(w.emitMesh(3).vertexCount > 0) || !(w.emitMesh().vertexCount > 0)) fail("valid sides refused");

        throwsKind(() => F.prototypes.whorl(-1), RangeError, "whorl(-1)");
        throwsKind(() => F.prototypes.whorl(2.5), RangeError, "whorl(2.5)");
        throwsKind(() => F.prototypes.monopodial(NaN), RangeError, "monopodial(NaN)");
        throwsKind(() => F.prototypes.horizontalTier(-3), RangeError, "horizontalTier(-3)");
        throwsKind(() => F.prototypes.tier(-3), RangeError, "tier(-3)");
        if (F.prototypes.whorl(0).nodes.length < 3) fail("whorl(0) should clamp to 2 arms");

        throwsKind(() => F.leafCluster("spiral", { count: -1 }), RangeError, "leafCluster count -1");
        throwsKind(() => F.leafCluster({ count: 1.5 }), RangeError, "leafCluster count 1.5");
        throwsKind(() => w.emitFoliageTransforms({ minDepth: 0.5 }), RangeError, "minDepth 0.5");
        throwsKind(() => w.emitFoliageMesh(F.leafCluster(), { densityWeight: { length: -1 } }),
                   RangeError, "densityWeight length -1");
        throwsKind(() => w.emitBloomMesh(F.leafCluster(), null, { bloomCap: -1 }), RangeError, "bloomCap -1");

        throwsKind(() => F.createWorld({ shadow: { width: -1, height: 4, depth: 4 } }), RangeError, "shadow width -1");
        throwsKind(() => F.createWorld({ shadow: { width: 4, height: 4.5, depth: 4 } }), RangeError, "shadow height 4.5");
        throwsKind(() => F.createWorld({ shadow: { width: 5000, height: 5000, depth: 5000 } }), RangeError, "shadow too big");

        throwsKind(() => w.addVoronoiSite(-1), RangeError, "addVoronoiSite(-1)");
        throwsKind(() => w.addPlant({ prototypeIndex: -1 }), RangeError, "addPlant prototypeIndex -1");
        const node = { position: [0, 0, 0] }, tip = { position: [0, 1, 0] };
        throwsKind(() => w.addPrototype({ nodes: [node, tip], rootNode: 3 }), RangeError, "rootNode 3");
        throwsKind(() => w.addPrototype({ nodes: [node, tip], edges: [[0, 5]] }), RangeError, "edge [0, 5]");
        throwsKind(() => w.addPrototype({ nodes: [node, tip], edges: [{ a: -1, b: 1 }] }), RangeError, "edge a -1");
        throwsKind(() => w.addPrototype({ nodes: [node, tip], terminalNodes: [-1] }), RangeError, "terminal -1");
        if (w.addPrototype({ nodes: [node, tip], edges: [[0, 1]], terminalNodes: [1] }) < 0) fail("a valid spec refused");
        if (w.addPrototype({ nodes: [] }) !== -1) fail("an empty spec should still answer -1");

        throwsKind(() => F.addPlacement({ count: -1 }), RangeError, "addPlacement count -1");
        throwsKind(() => F.addPlacement({ count: 2.5 }), RangeError, "addPlacement count 2.5");
        throwsKind(() => F.addPlacement({ count: "3" }), TypeError, "addPlacement count '3'");
        if (F.addPlacement({ count: 2 }).transforms.length !== 32) fail("addPlacement count 2");

        for (const idx of [-1, NaN, 0.5, 1e20]) {
            if (w.plantInfo(idx) !== null) fail("plantInfo(" + idx + ") should be null");
            if (w.removePlant(idx) !== false) fail("removePlant(" + idx + ") should be false");
            if (w.emitPlantSegments(idx).length !== 0) fail("emitPlantSegments(" + idx + ") should be empty");
            if (w.emitPlantMesh(idx) !== null) fail("emitPlantMesh(" + idx + ") should be null");
        }

        const s = F.createWorld({ shadow: { origin: [0, 0, 0], cellSize: 1, width: 4, height: 4, depth: 4, fill: 0.5 } });
        if (s.sampleShadow([0.5, 0.5, 0.5]) !== 0.5) fail("sampleShadow inside");
        for (const p of [[-0.5, 0.5, 0.5], [1e30, 0, 0], [NaN, 0, 0], [0, -1e30, 0]]) {
            if (s.sampleShadow(p) !== null) fail("sampleShadow(" + p + ") should be null");
        }
        F.clear();
        "SUCCESS";
    )JS");
}

static void test_api_in_realm() {
    ev::Realm* realm = ev::createRealm();
    {
        ev::RealmScope scope(realm);
        broflora::api::installFlora();
        test_api_embed_surface();
        test_api_world_lifecycle();
        test_api_option_readers();
        test_api_wind_paths_agree();
        test_api_instance_layout();
        test_api_count_validation();
    }
    ev::destroyRealm(realm);
}

static void test_wind_transforms_and_normals() {
    // 1. Test applyWindToTransforms preserves orthonormality
    float transforms[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 5.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    broflora::api::applyWindToTransforms(transforms, 1, 1.0, 1.0, 1.0, 0.0);

    // Columns of the 3x3 block:
    // col 0: transforms[0], transforms[4], transforms[8]
    // col 1: transforms[1], transforms[5], transforms[9]
    // col 2: transforms[2], transforms[6], transforms[10]
    float len0 = std::sqrt(transforms[0]*transforms[0] + transforms[4]*transforms[4] + transforms[8]*transforms[8]);
    float len1 = std::sqrt(transforms[1]*transforms[1] + transforms[5]*transforms[5] + transforms[9]*transforms[9]);
    float len2 = std::sqrt(transforms[2]*transforms[2] + transforms[6]*transforms[6] + transforms[10]*transforms[10]);
    TEST_CHECK(std::abs(len0 - 1.0f) < 1e-4f);
    TEST_CHECK(std::abs(len1 - 1.0f) < 1e-4f);
    TEST_CHECK(std::abs(len2 - 1.0f) < 1e-4f);

    float dot01 = transforms[0]*transforms[1] + transforms[4]*transforms[5] + transforms[8]*transforms[9];
    float dot02 = transforms[0]*transforms[2] + transforms[4]*transforms[6] + transforms[8]*transforms[10];
    float dot12 = transforms[1]*transforms[2] + transforms[5]*transforms[6] + transforms[9]*transforms[10];
    TEST_CHECK(std::abs(dot01) < 1e-4f);
    TEST_CHECK(std::abs(dot02) < 1e-4f);
    TEST_CHECK(std::abs(dot12) < 1e-4f);

    // Verify there was actual tilting
    TEST_CHECK(transforms[5] < 0.999f);

    // 2. Test applyWindToMeshData updates normals
    bromesh::MeshData md;
    // Vertex at height 5.0
    md.positions = {0.0f, 5.0f, 0.0f};
    md.normals = {0.0f, 1.0f, 0.0f};
    broflora::api::applyWindToMeshData(md, 1.0, 1.0, 1.0, 0.0);

    float normLen = std::sqrt(md.normals[0]*md.normals[0] + md.normals[1]*md.normals[1] + md.normals[2]*md.normals[2]);
    TEST_CHECK(std::abs(normLen - 1.0f) < 1e-4f);
    // Normal should have tilted towards wind direction
    TEST_CHECK(std::abs(md.normals[0]) > 0.01f);
    TEST_CHECK(md.normals[1] < 0.999f);
}

// An instance matrix at p and a mesh vertex at p get the same wind: the
// translation lands where the vertex lands, and each basis column turns
// exactly as a normal along that axis does.
static void test_wind_instance_matches_mesh() {
    const double time = 0.83, dirX = -0.3, dirY = 0.9;
    const float p[3] = {0.7f, 2.4f, -1.1f};
    for (double strength : {1.3, -2.6, 40.0}) {
        float m[16] = {
            1.0f, 0.0f, 0.0f, p[0],
            0.0f, 1.0f, 0.0f, p[1],
            0.0f, 0.0f, 1.0f, p[2],
            1.0f, 1.0f, 1.0f, 1.0f
        };
        broflora::api::applyWindToTransforms(m, 1, time, strength, dirX, dirY);

        bromesh::MeshData md;
        md.positions = {p[0], p[1], p[2], p[0], p[1], p[2], p[0], p[1], p[2]};
        md.normals = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        broflora::api::applyWindToMeshData(md, time, strength, dirX, dirY);

        TEST_CHECK(m[3] == md.positions[0] && m[7] == md.positions[1] && m[11] == md.positions[2]);
        TEST_CHECK(std::abs(m[3] - p[0]) > 1e-3f);  // it did move
        for (int k = 0; k < 3; ++k) {
            TEST_CHECK(m[k] == md.normals[k * 3 + 0]);
            TEST_CHECK(m[4 + k] == md.normals[k * 3 + 1]);
            TEST_CHECK(m[8 + k] == md.normals[k * 3 + 2]);
        }
        // Rigid in both directions: orthonormal, and the tilt of +Y stays
        // within the model's clamp whatever the strength's sign or size.
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) {
                float d = m[a] * m[b] + m[4 + a] * m[4 + b] + m[8 + a] * m[8 + b];
                TEST_CHECK(std::abs(d - (a == b ? 1.0f : 0.0f)) < 1e-5f);
            }
        }
        TEST_CHECK(m[5] >= std::cos(0.35f) - 1e-5f);
        // The instance tint row is not the wind's to touch.
        TEST_CHECK(m[12] == 1.0f && m[13] == 1.0f && m[14] == 1.0f && m[15] == 1.0f);
    }

    // Rooted: nothing at ground level moves, in either path.
    float g[16] = {1, 0, 0, 3.0f, 0, 1, 0, 0.0f, 0, 0, 1, -2.0f, 1, 1, 1, 1};
    broflora::api::applyWindToTransforms(g, 1, time, 5.0, dirX, dirY);
    TEST_CHECK(g[3] == 3.0f && g[7] == 0.0f && g[11] == -2.0f && g[5] == 1.0f);
    bromesh::MeshData base;
    base.positions = {3.0f, 0.0f, -2.0f};
    base.normals = {0.0f, 1.0f, 0.0f};
    broflora::api::applyWindToMeshData(base, time, 5.0, dirX, dirY);
    TEST_CHECK(base.positions[0] == 3.0f && base.positions[1] == 0.0f && base.positions[2] == -2.0f);
    TEST_CHECK(base.normals[1] == 1.0f);
}

int main() {
    std::cout << "Running broflora API test..." << std::endl;
    test_api_in_realm();
    test_wind_transforms_and_normals();
    test_wind_instance_matches_mesh();
    std::cout << "All broflora API tests passed!" << std::endl;
    return 0;
}
