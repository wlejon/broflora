#include "test_framework.h"
#include "broflora/api/api.h"
#include "embed/embed.h"
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

static void test_api_installation_and_smoke() {
    ev::Realm* realm = ev::createRealm();
    ev::RealmScope scope(realm);

    // Install bro.flora API
    broflora::api::installFlora();

    ev::GlobalValue broG = ev::globalValue("bro");
    TEST_CHECK(broG.found);
    TEST_CHECK(ev::isObject(broG.value));

    Value floraV = ev::getProperty(broG.value, "flora");
    TEST_CHECK(ev::isObject(floraV));

    Value availV = ev::getProperty(floraV, "available");
    TEST_CHECK(ev::toBool(availV) == true);

    // Prototypes
    Value protosV = ev::getProperty(floraV, "prototypes");
    TEST_CHECK(ev::isObject(protosV));

    Value straightFn = ev::getProperty(protosV, "straight");
    TEST_CHECK(ev::isFunction(straightFn));
    ev::CallResult straightRes = ev::call(straightFn, protosV, {});
    TEST_CHECK(!straightRes.thrown);
    TEST_CHECK(ev::isObject(straightRes.value));

    Value nodes = ev::getProperty(straightRes.value, "nodes");
    TEST_CHECK(ev::isObject(nodes));
    Value nodesLen = ev::getProperty(nodes, "length");
    TEST_CHECK(ev::toDouble(nodesLen) >= 2.0);

    // createWorld
    Value createWorldFn = ev::getProperty(floraV, "createWorld");
    TEST_CHECK(ev::isFunction(createWorldFn));

    ev::Persistent opts(ev::createObject());
    opts.set(ev::setProperty(opts.get(), "rngSeed", ev::fromDouble(12345.0)));
    Value optVal = opts.get();
    ev::CallResult worldRes = ev::call(createWorldFn, floraV, std::span<const Value>(&optVal, 1));
    TEST_CHECK(!worldRes.thrown);
    TEST_CHECK(ev::isObject(worldRes.value));

    Value world = worldRes.value;

    // prototypeCount initial
    Value protoCountV = ev::getProperty(world, "prototypeCount");
    TEST_CHECK(ev::toDouble(protoCountV) == 0.0);

    // addPrototype
    Value addProtoFn = ev::getProperty(world, "addPrototype");
    TEST_CHECK(ev::isFunction(addProtoFn));
    Value protoArg = straightRes.value;
    ev::CallResult addProtoRes = ev::call(addProtoFn, world, std::span<const Value>(&protoArg, 1));
    TEST_CHECK(!addProtoRes.thrown);
    double protoIdx = ev::toDouble(addProtoRes.value);
    TEST_CHECK(protoIdx == 0.0);

    protoCountV = ev::getProperty(world, "prototypeCount");
    TEST_CHECK(ev::toDouble(protoCountV) == 1.0);

    // addVoronoiSite
    Value addVoronoiFn = ev::getProperty(world, "addVoronoiSite");
    TEST_CHECK(ev::isFunction(addVoronoiFn));
    Value voronoiArgs[3] = {ev::fromDouble(protoIdx), ev::fromDouble(0.5), ev::fromDouble(0.5)};
    ev::CallResult voronoiRes = ev::call(addVoronoiFn, world, std::span<const Value>(voronoiArgs, 3));
    TEST_CHECK(!voronoiRes.thrown);

    // addPlant
    Value addPlantFn = ev::getProperty(world, "addPlant");
    TEST_CHECK(ev::isFunction(addPlantFn));

    ev::Persistent plantSpec(ev::createObject());
    ev::CallResult originParsed = ev::parseJson("[0.0, 0.0, 0.0]");
    TEST_CHECK(!originParsed.thrown);
    plantSpec.set(ev::setProperty(plantSpec.get(), "origin", originParsed.value));
    plantSpec.set(ev::setProperty(plantSpec.get(), "prototypeIndex", ev::fromDouble(protoIdx)));

    ev::Persistent spObj(ev::createObject());
    spObj.set(ev::setProperty(spObj.get(), "apicalControl", ev::fromDouble(0.8)));
    spObj.set(ev::setProperty(spObj.get(), "determinacy", ev::fromDouble(0.2)));
    plantSpec.set(ev::setProperty(plantSpec.get(), "species", spObj.get()));

    Value plantSpecVal = plantSpec.get();
    ev::CallResult addPlantRes = ev::call(addPlantFn, world, std::span<const Value>(&plantSpecVal, 1));
    TEST_CHECK(!addPlantRes.thrown);
    TEST_CHECK(ev::toDouble(addPlantRes.value) == 0.0);

    Value plantCountV = ev::getProperty(world, "plantCount");
    TEST_CHECK(ev::toDouble(plantCountV) == 1.0);

    // validate
    Value validateFn = ev::getProperty(world, "validate");
    TEST_CHECK(ev::isFunction(validateFn));
    ev::CallResult valRes = ev::call(validateFn, world, {});
    TEST_CHECK(!valRes.thrown);
    TEST_CHECK(ev::isNull(valRes.value));

    // step
    Value stepFn = ev::getProperty(world, "step");
    TEST_CHECK(ev::isFunction(stepFn));
    Value dtArg = ev::fromDouble(0.1);
    for (int i = 0; i < 20; ++i) {
        ev::CallResult stepRes = ev::call(stepFn, world, std::span<const Value>(&dtArg, 1));
        TEST_CHECK(!stepRes.thrown);
    }

    Value simTimeV = ev::getProperty(world, "simTime");
    TEST_CHECK(ev::toDouble(simTimeV) > 1.9);

    // plantInfo
    Value plantInfoFn = ev::getProperty(world, "plantInfo");
    TEST_CHECK(ev::isFunction(plantInfoFn));
    Value idxArg = ev::fromDouble(0.0);
    ev::CallResult infoRes = ev::call(plantInfoFn, world, std::span<const Value>(&idxArg, 1));
    TEST_CHECK(!infoRes.thrown);
    TEST_CHECK(ev::isObject(infoRes.value));
    Value modCountV = ev::getProperty(infoRes.value, "moduleCount");
    TEST_CHECK(ev::toDouble(modCountV) > 0.0);

    // emitMesh
    Value emitMeshFn = ev::getProperty(world, "emitMesh");
    TEST_CHECK(ev::isFunction(emitMeshFn));
    Value sidesArg = ev::fromDouble(6.0);
    ev::CallResult meshRes = ev::call(emitMeshFn, world, std::span<const Value>(&sidesArg, 1));
    TEST_CHECK(!meshRes.thrown);
    TEST_CHECK(ev::isObject(meshRes.value));
    Value vc = ev::getProperty(meshRes.value, "vertexCount");
    TEST_CHECK(ev::toDouble(vc) > 0.0);

    // emitPlantSdfMesh & emitWorldSdfMesh
    Value emitPlantSdfFn = ev::getProperty(world, "emitPlantSdfMesh");
    TEST_CHECK(ev::isFunction(emitPlantSdfFn));
    ev::Persistent sdfOpts(ev::createObject());
    sdfOpts.set(ev::setProperty(sdfOpts.get(), "voxelSize", ev::fromDouble(0.01)));
    Value plantArgs[2] = { ev::fromDouble(0.0), sdfOpts.get() };
    ev::CallResult plantSdfRes = ev::call(emitPlantSdfFn, world, std::span<const Value>(plantArgs, 2));
    TEST_CHECK(!plantSdfRes.thrown);
    TEST_CHECK(ev::isObject(plantSdfRes.value));
    Value plantSdfVc = ev::getProperty(plantSdfRes.value, "vertexCount");
    TEST_CHECK(ev::toDouble(plantSdfVc) > 0.0);

    Value emitWorldSdfFn = ev::getProperty(world, "emitWorldSdfMesh");
    TEST_CHECK(ev::isFunction(emitWorldSdfFn));
    Value worldArgs[1] = { sdfOpts.get() };
    ev::CallResult worldSdfRes = ev::call(emitWorldSdfFn, world, std::span<const Value>(worldArgs, 1));
    TEST_CHECK(!worldSdfRes.thrown);
    TEST_CHECK(ev::isObject(worldSdfRes.value));
    Value worldSdfVc = ev::getProperty(worldSdfRes.value, "vertexCount");
    TEST_CHECK(ev::toDouble(worldSdfVc) > 0.0);

    // leafCluster
    Value leafClusterFn = ev::getProperty(floraV, "leafCluster");
    TEST_CHECK(ev::isFunction(leafClusterFn));
    ev::CallResult lcRes = ev::call(leafClusterFn, floraV, {});
    TEST_CHECK(!lcRes.thrown);
    TEST_CHECK(ev::isObject(lcRes.value));
    Value lcVc = ev::getProperty(lcRes.value, "vertexCount");
    TEST_CHECK(ev::toDouble(lcVc) > 0.0);

    // removePlant
    Value removePlantFn = ev::getProperty(world, "removePlant");
    TEST_CHECK(ev::isFunction(removePlantFn));
    ev::CallResult remRes = ev::call(removePlantFn, world, std::span<const Value>(&idxArg, 1));
    TEST_CHECK(!remRes.thrown);
    TEST_CHECK(ev::toBool(remRes.value) == true);

    plantCountV = ev::getProperty(world, "plantCount");
    TEST_CHECK(ev::toDouble(plantCountV) == 0.0);

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
    test_api_installation_and_smoke();
    test_wind_transforms_and_normals();
    std::cout << "All broflora API tests passed!" << std::endl;
    return 0;
}

