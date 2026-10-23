#include "broflora/broflora.h"
#include "broflora/clumps.h"
#include "broflora/sdf_mesh.h"
#include "broflora/sway.h"
#include "bromesh/analysis/bbox.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace broflora;

static int g_testsRun = 0;
static int g_testsPassed = 0;

#define CHECK(cond, msg) do { \
    g_testsRun++; \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        std::exit(1); \
    } else { \
        g_testsPassed++; \
    } \
} while (0)

static Plant createGrownPlant(bromath::Vec3 origin = {0.0f, 0.0f, 0.0f}) {
    static BranchModulePrototype proto;
    proto.nodes.clear();
    proto.edges.clear();
    proto.terminalNodes.clear();

    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 0.4f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{-0.2f, 0.8f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.2f, 0.8f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.edges.push_back({1, 2});
    proto.edges.push_back({1, 3});
    proto.rootNode = 0;
    proto.terminalNodes = {2, 3};

    Plant plant;
    plant.species = {};
    plant.species.shadeTolerance = 1.0f;
    plant.species.leafDiameter = 0.05f;
    // The hand-built Y exactly as written: the mesher's checks below are
    // pinned to this skeleton, whose node positions a kink would move.
    plant.species.kink = 0.0f;
    plant.effectiveRootVigorMax = plant.species.rootVigorMax;
    plant.origin = origin;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;
    m.age = 0.0f;
    m.vigor = 0.8f;
    m.light = 1.0f;
    m.diameter = 0.12f;
    plant.modules.push_back(m);

    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    world.plants.push_back(plant);

    for (int i = 0; i < 30; ++i) {
        step(world, 0.1f);
    }

    return world.plants.front();
}

static float calculateSurfaceArea(const bromesh::MeshData& mesh) {
    float area = 0.0f;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];
        bromath::Vec3 p0{mesh.positions[3 * i0], mesh.positions[3 * i0 + 1], mesh.positions[3 * i0 + 2]};
        bromath::Vec3 p1{mesh.positions[3 * i1], mesh.positions[3 * i1 + 1], mesh.positions[3 * i1 + 2]};
        bromath::Vec3 p2{mesh.positions[3 * i2], mesh.positions[3 * i2 + 1], mesh.positions[3 * i2 + 2]};
        area += 0.5f * bromath::vlen(bromath::vcross(p1 - p0, p2 - p0));
    }
    return area;
}

static void verifyMeshProperties(const bromesh::MeshData& mesh, const Plant& plant, const SdfMeshOptions& opts) {
    CHECK(!mesh.empty(), "Mesh must not be empty");
    CHECK(mesh.vertexCount() > 0, "Vertices count must be > 0");
    CHECK(mesh.indices.size() > 0, "Indices count must be > 0");
    CHECK(mesh.positions.size() == mesh.normals.size(), "Positions and normals must match in size");
    CHECK(mesh.indices.size() % 3 == 0, "Indices must be a multiple of 3");

    // Compute expected bounding box from segments
    auto segs = emitPlantSegments(plant);
    CHECK(!segs.empty(), "Segments must not be empty");

    bromath::AABB3 expectedBounds = bromath::aempty3();
    for (const auto& s : segs) {
        bromath::Vec3 rVec{s.radius, s.radius, s.radius};
        expectedBounds = bromath::aexpand(expectedBounds, s.from - rVec);
        expectedBounds = bromath::aexpand(expectedBounds, s.from + rVec);
        expectedBounds = bromath::aexpand(expectedBounds, s.to - rVec);
        expectedBounds = bromath::aexpand(expectedBounds, s.to + rVec);
    }
    float safety = opts.smoothK + opts.margin + opts.voxelSize * 2.0f;
    expectedBounds.min.x -= safety;
    expectedBounds.min.y -= safety;
    expectedBounds.min.z -= safety;
    expectedBounds.max.x += safety;
    expectedBounds.max.y += safety;
    expectedBounds.max.z += safety;

    // Check all coordinates are finite and inside expected bounds
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        float px = mesh.positions[3 * i + 0];
        float py = mesh.positions[3 * i + 1];
        float pz = mesh.positions[3 * i + 2];
        CHECK(std::isfinite(px) && std::isfinite(py) && std::isfinite(pz), "Vertex positions must be finite");

        float nx = mesh.normals[3 * i + 0];
        float ny = mesh.normals[3 * i + 1];
        float nz = mesh.normals[3 * i + 2];
        CHECK(std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz), "Vertex normals must be finite");

        CHECK(px >= expectedBounds.min.x && px <= expectedBounds.max.x, "Vertex X inside bounding box");
        CHECK(py >= expectedBounds.min.y && py <= expectedBounds.max.y, "Vertex Y inside bounding box");
        CHECK(pz >= expectedBounds.min.z && pz <= expectedBounds.max.z, "Vertex Z inside bounding box");
    }

    // Surface area
    float area = calculateSurfaceArea(mesh);
    CHECK(area > 0.001f, "Surface area must be non-zero");

    // Watertight / manifold closed topology
    CHECK(bromesh::isManifold(mesh), "Mesh must be watertight manifold");
    CHECK(bromesh::computeVolume(mesh) > 0.0f, "Mesh volume must be strictly positive");
}

static void test_surface_nets() {
    std::cout << "Testing emitPlantSdfMesh with Surface Nets..." << std::endl;
    Plant plant = createGrownPlant();
    SdfMeshOptions opts;
    opts.useSurfaceNets = true;
    opts.voxelSize = 0.02f;
    opts.smoothK = 0.03f;
    opts.margin = 0.08f;

    bromesh::MeshData mesh = emitPlantSdfMesh(plant, opts);
    verifyMeshProperties(mesh, plant, opts);
    std::cout << "Surface Nets: " << mesh.vertexCount() << " vertices, "
              << mesh.triangleCount() << " triangles." << std::endl;
}

static void test_marching_cubes() {
    std::cout << "Testing emitPlantSdfMesh with Marching Cubes..." << std::endl;
    Plant plant = createGrownPlant();
    SdfMeshOptions opts;
    opts.useSurfaceNets = false;
    opts.voxelSize = 0.04f;
    opts.smoothK = 0.03f;
    opts.margin = 0.08f;

    bromesh::MeshData mesh = emitPlantSdfMesh(plant, opts);
    verifyMeshProperties(mesh, plant, opts);
    std::cout << "Marching Cubes: " << mesh.vertexCount() << " vertices, "
              << mesh.triangleCount() << " triangles." << std::endl;
}

static void test_world_sdf_mesh() {
    std::cout << "Testing emitWorldSdfMesh..." << std::endl;
    WorldState world;
    world.plants.push_back(createGrownPlant({-0.4f, 0.0f, 0.0f}));
    world.plants.push_back(createGrownPlant({0.4f, 0.0f, 0.0f}));

    SdfMeshOptions opts;
    opts.useSurfaceNets = true;
    opts.voxelSize = 0.03f;
    opts.smoothK = 0.03f;
    opts.margin = 0.08f;

    bromesh::MeshData mesh = emitWorldSdfMesh(world, opts);
    CHECK(!mesh.empty(), "World mesh must not be empty");
    CHECK(mesh.vertexCount() > 0, "World mesh vertex count > 0");
    CHECK(mesh.indices.size() > 0, "World mesh indices count > 0");
    CHECK(bromesh::isManifold(mesh), "World mesh must be manifold");
    CHECK(bromesh::computeVolume(mesh) > 0.0001f, "World mesh volume must be positive");
    std::cout << "World mesh: " << mesh.vertexCount() << " vertices, "
              << mesh.triangleCount() << " triangles." << std::endl;
}

static void test_empty_plant() {
    std::cout << "Testing empty plant..." << std::endl;
    Plant emptyPlant;
    SdfMeshOptions opts;
    bromesh::MeshData mesh = emitPlantSdfMesh(emptyPlant, opts);
    CHECK(mesh.empty(), "Empty plant must produce empty mesh");
    CHECK(mesh.vertexCount() == 0, "Vertex count must be 0");
    CHECK(mesh.indices.empty(), "Indices must be empty");
}

static void test_grove_parallel_emit() {
    std::cout << "Testing 16-plant grove parallel emit and SDF mesh..." << std::endl;
    WorldState world;
    for (int gz = 0; gz < 4; ++gz) {
        for (int gx = 0; gx < 4; ++gx) {
            Plant p = createGrownPlant({static_cast<float>(gx) * 5.0f, 0.0f, static_cast<float>(gz) * 5.0f});
            p.flowering = true;
            world.plants.push_back(std::move(p));
        }
    }

    ClumpOptions clumpOpts;
    clumpOpts.minDepth = 0;
    clumpOpts.minDensity = 0.0f;

    auto t0 = std::chrono::steady_clock::now();
    auto singleSegs = emitPlantSegments(world.plants[0]);
    auto worldSegs = emitWorldSegments(world);
    auto worldFoliage = emitWorldFoliage(world);
    auto worldSway = emitWorldSway(world);
    auto worldClumps = emitWorldClumps(world, clumpOpts);
    auto worldBlooms = emitWorldBloomAnchors(world);
    auto t1 = std::chrono::steady_clock::now();

    CHECK(worldSegs.size() == singleSegs.size() * 16, "World segments count matches 16 plants");
    CHECK(worldFoliage.size() == worldSegs.size(), "World foliage count matches world segments");
    CHECK(worldSway.size() == worldSegs.size(), "World sway count matches world segments");
    CHECK(!worldClumps.empty(), "World clumps must not be empty");
    CHECK(!worldBlooms.empty(), "World bloom anchors must not be empty");

    // Verify parent segment indices in plant k are properly offset by k * singleSegs.size()
    const size_t nPerPlant = singleSegs.size();
    for (size_t p = 0; p < 16; ++p) {
        const size_t base = p * nPerPlant;
        for (size_t s = 0; s < nPerPlant; ++s) {
            const int expectedParent = singleSegs[s].parent >= 0
                ? singleSegs[s].parent + static_cast<int>(base)
                : -1;
            CHECK(worldSegs[base + s].parent == expectedParent, "World segment parent offset matches");
        }
    }

    SdfMeshOptions opts;
    opts.useSurfaceNets = true;
    opts.voxelSize = 0.04f;
    opts.smoothK = 0.03f;
    opts.margin = 0.08f;

    auto t2 = std::chrono::steady_clock::now();
    bromesh::MeshData singleMesh = emitPlantSdfMesh(world.plants[0], opts);
    auto t3 = std::chrono::steady_clock::now();
    bromesh::MeshData groveMesh = emitWorldSdfMesh(world, opts);
    auto t4 = std::chrono::steady_clock::now();

    CHECK(groveMesh.vertexCount() == singleMesh.vertexCount() * 16,
          "Disjoint 16-plant grove SDF vertex count equals 16x single plant");
    CHECK(groveMesh.triangleCount() == singleMesh.triangleCount() * 16,
          "Disjoint 16-plant grove SDF triangle count equals 16x single plant");
    CHECK(bromesh::isManifold(groveMesh), "16-plant grove SDF mesh must be manifold");

    double emitMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double singleSdfMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    double groveSdfMs = std::chrono::duration<double, std::milli>(t4 - t3).count();
    std::cout << "16-plant parallel emit: " << emitMs << " ms | single SDF: "
              << singleSdfMs << " ms | 16-plant grove SDF: " << groveSdfMs << " ms" << std::endl;
}

int main() {
    std::cout << "=== Running broflora SDF Mesh Tests ===" << std::endl;
    test_surface_nets();
    test_marching_cubes();
    test_world_sdf_mesh();
    test_empty_plant();
    test_grove_parallel_emit();
    std::cout << "All tests passed! (" << g_testsPassed << "/" << g_testsRun << " assertions)" << std::endl;
    return 0;
}
