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

static void test_api_in_realm() {
    ev::Realm* realm = ev::createRealm();
    {
        ev::RealmScope scope(realm);
        broflora::api::installFlora();
        test_api_embed_surface();
        test_api_world_lifecycle();
        test_api_option_readers();
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

int main() {
    std::cout << "Running broflora API test..." << std::endl;
    test_api_in_realm();
    test_wind_transforms_and_normals();
    std::cout << "All broflora API tests passed!" << std::endl;
    return 0;
}
