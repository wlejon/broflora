// grove — a minimal end-to-end driver for the broflora API. Builds a
// world from scratch, runs the simulation for a fixed number of ticks,
// and writes the emitted mesh to disk as a Wavefront OBJ.
//
// This is the canonical "how do I use broflora?" reference for code
// integrating into the `bro` runtime. It deliberately avoids any
// dependency on bromesh or rendering — everything here is reachable
// from the public headers under include/broflora/.

#include "broflora/broflora.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

using namespace broflora;

namespace {

// A tiny three-node "Y" prototype: one root, two terminals. Picked so
// the spawning loop has somewhere to put children at every step.
BranchModulePrototype makeYPrototype(const char* name) {
    BranchModulePrototype p;
    p.name = name;
    p.nodes.push_back({{ 0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});  // root
    p.nodes.push_back({{ 0.3f, 1.0f, 0.0f}, 0.2f, 1.0f, 1.0f});  // term A
    p.nodes.push_back({{-0.3f, 1.0f, 0.0f}, 0.2f, 1.0f, 1.0f});  // term B
    p.edges.push_back({0, 1});
    p.edges.push_back({0, 2});
    p.rootNode = 0;
    p.terminalNodes = {1, 2};
    return p;
}

// A "straight" prototype: just a single segment. Used as the alternate
// Voronoi pole so module selection has somewhere to go when (D', λ)
// drifts away from the Y's coordinates.
BranchModulePrototype makeIPrototype(const char* name) {
    BranchModulePrototype p;
    p.name = name;
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    p.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.2f, 1.0f, 1.0f});
    p.edges.push_back({0, 1});
    p.rootNode = 0;
    p.terminalNodes = {1};
    return p;
}

void writeObj(const MeshData& mesh, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "grove: failed to open %s for writing\n", path.c_str());
        return;
    }
    std::fprintf(f, "# broflora grove example — %zu verts, %zu tris\n",
                 mesh.vertexCount(), mesh.triangleCount());
    for (size_t i = 0; i < mesh.positions.size(); i += 3) {
        std::fprintf(f, "v %g %g %g\n",
                     mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
    }
    for (size_t i = 0; i < mesh.normals.size(); i += 3) {
        std::fprintf(f, "vn %g %g %g\n",
                     mesh.normals[i], mesh.normals[i + 1], mesh.normals[i + 2]);
    }
    // OBJ indices are 1-based.
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i] + 1;
        const uint32_t b = mesh.indices[i + 1] + 1;
        const uint32_t c = mesh.indices[i + 2] + 1;
        std::fprintf(f, "f %u//%u %u//%u %u//%u\n", a, a, b, b, c, c);
    }
    std::fclose(f);
}

} // namespace

int main(int argc, char** argv) {
    const std::string outPath = (argc > 1) ? argv[1] : "grove.obj";

    // --- 1. Build the world. -----------------------------------------
    WorldState world;
    world.rngState = 0xC0FFEEULL;

    // Climate: mild temperate. The default Species optima target this
    // range, so adaptation σ stays near 1.0.
    world.climate.annualTempBase = 15.0f;
    world.climate.annualPrecip   = 1000.0f;

    // Shadow grid: a 16×16×16 cube centred on the origin, cell size 1.
    world.shadow.origin   = {-8.0f, 0.0f, -8.0f};
    world.shadow.cellSize = 1.0f;
    world.shadow.width    = 16;
    world.shadow.height   = 16;
    world.shadow.depth    = 16;
    world.shadow.qg.assign(16 * 16 * 16, 1.0f);

    // --- 2. Register prototypes and Voronoi cells. -------------------
    // Use the addPrototype / addVoronoiSite builders so we never have
    // to think about raw-pointer-into-vector invariants.
    const uint32_t protoY = addPrototype(world, makeYPrototype("Y"));
    const uint32_t protoI = addPrototype(world, makeIPrototype("I"));

    // Y at the (mature, branching) corner; I at the (juvenile, straight)
    // corner. Spawning picks the nearest site in (D', λ).
    addVoronoiSite(world, protoY, /*determinacy=*/0.2f, /*apicalControl=*/0.85f);
    addVoronoiSite(world, protoI, /*determinacy=*/0.8f, /*apicalControl=*/0.40f);

    // --- 3. Plant one seedling. --------------------------------------
    Plant seedling;
    seedling.species = {};                    // defaults are fine
    seedling.species.moduleMatureAge = 0.6f;  // mature fast for a short run
    seedling.species.shadeTolerance  = 0.5f;
    seedling.origin = {0.0f, 0.0f, 0.0f};
    seedling.effectiveRootVigorMax = seedling.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoY);
    root.parent    = UINT32_MAX;
    root.age       = 0.0f;
    root.vigor     = seedling.species.minVigor * 2.0f;
    root.light     = 1.0f;
    seedling.modules.push_back(root);

    addPlant(world, std::move(seedling));

    // --- 4. Sanity-check the setup. ----------------------------------
    std::string err;
    if (!validate(world, &err)) {
        std::fprintf(stderr, "grove: world failed validation: %s\n", err.c_str());
        return 1;
    }

    // --- 5. Run the simulation. --------------------------------------
    const int    ticks = 200;
    const float  dt    = 0.1f;
    for (int i = 0; i < ticks; ++i) {
        step(world, dt);
    }

    // --- 6. Report and emit. -----------------------------------------
    size_t totalModules = 0;
    for (const auto& p : world.plants) totalModules += p.modules.size();
    std::printf("grove: %d ticks, %zu plants, %zu modules total\n",
                ticks, world.plants.size(), totalModules);

    const MeshData mesh = emitWorldMesh(world, /*sides=*/6);
    std::printf("grove: mesh — %zu verts, %zu triangles\n",
                mesh.vertexCount(), mesh.triangleCount());

    if (mesh.empty()) {
        std::fprintf(stderr, "grove: warning — emitted mesh is empty\n");
        return 2;
    }

    writeObj(mesh, outPath);
    std::printf("grove: wrote %s\n", outPath.c_str());
    return 0;
}
