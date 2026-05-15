#include "test_framework.h"

#include "bromath/sphere.h"

#include <cmath>
#include <cstdint>

using namespace broflora;

// Spatial-hash regression: build a many-module world, run a tick, and
// verify the per-module Q values match a brute-force O(N²) recomputation
// from the same post-development bboxes. Shade tolerance is pinned to 0
// and the shadow grid is left empty so the public m.light reads back
// exactly exp(-f_collisions(u)).
TEST(spatial_hash_matches_brute_force) {
    static BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    WorldState world;
    // Empty ShadowGrid → shadowCellOf returns false → Q_G = 1 everywhere.

    // 64 plants on a packed 8×8 lattice with 1.2 unit spacing — modules
    // overlap their neighbours but not their neighbours' neighbours, so
    // f_collisions varies across the grid.
    const int side = 8;
    const float spacing = 1.2f;
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            Plant p;
            p.species = {};
            p.species.shadeTolerance = 0.0f;   // m.light = Q exactly
            p.species.moduleMatureAge = 100.0f;
            p.species.maxAge = 1000.0f;
            p.effectiveRootVigorMax = p.species.rootVigorMax;
            p.origin = {x * spacing, 0.0f, z * spacing};
            BranchModuleInstance m;
            m.prototype = &proto;
            m.parent = UINT32_MAX;
            m.age = 5.0f;
            m.vigor = 0.5f;
            m.light = 1.0f;
            p.modules.push_back(m);
            world.plants.push_back(p);
        }
    }

    // First tick seeds bboxes (development runs after light on tick 1);
    // second tick is the one whose light pass uses populated bboxes —
    // that's what we want to match against brute force.
    step(world, 0.1f);
    step(world, 0.1f);

    // Brute-force reference using the bboxes that step() just populated.
    struct S { bromath::Vec3 c; float r; size_t pi; };
    std::vector<S> spheres;
    for (size_t pi = 0; pi < world.plants.size(); ++pi) {
        const auto& m = world.plants[pi].modules.front();
        if (m.bboxRadius > 0.0f) spheres.push_back({m.bboxCenter, m.bboxRadius, pi});
    }
    std::vector<float> fc(spheres.size(), 0.0f);
    for (size_t i = 0; i < spheres.size(); ++i) {
        for (size_t j = i + 1; j < spheres.size(); ++j) {
            float v = bromath::sintersectVolume(
                bromath::Sphere{spheres[i].c, spheres[i].r},
                bromath::Sphere{spheres[j].c, spheres[j].r});
            fc[i] += v;
            fc[j] += v;
        }
    }

    int mismatches = 0;
    float worst = 0.0f;
    for (size_t k = 0; k < spheres.size(); ++k) {
        float ref = std::exp(-fc[k]);
        float got = world.plants[spheres[k].pi].modules.front().light;
        float diff = std::fabs(ref - got);
        if (diff > worst) worst = diff;
        if (diff > 1e-5f) ++mismatches;
    }
    ASSERT(mismatches == 0, "every module's Q matches brute-force within 1e-5");
    ASSERT(worst < 1e-5f, "worst-case Q error stays under 1e-5");
}
