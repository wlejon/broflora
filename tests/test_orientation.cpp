#include "test_framework.h"

#include "broflora/development.h"
#include "broflora/orientation.h"
#include "broflora/prototypes.h"
#include "broflora/mesh_emit.h"
#include "bromath/vec.h"
#include "../src/internal_geom.h"

#include <algorithm>
#include <cmath>

using namespace broflora;
using bromath::Vec3;
using bromath::vnorm;
using bromath::vlen;

namespace {

// World growth axis of a spawned module: its mean-terminal direction (the
// same axis `predictHypothesis` reasons about) rotated by the module's
// settled orientation. Uses the real `rotateYawPitch` so this helper can
// never drift from the implementation's convention.
Vec3 childAxis(const BranchModuleInstance& c) {
    const auto& proto = *c.prototype;
    const Vec3 root = proto.nodes[proto.rootNode].position;
    Vec3 sum = {0.0f, 0.0f, 0.0f};
    for (uint32_t t : proto.terminalNodes) sum += proto.nodes[t].position - root;
    sum = sum * (1.0f / static_cast<float>(proto.terminalNodes.size()));
    return vnorm(rotateYawPitch(sum, c.orientation.psi, c.orientation.theta));
}

// Single-terminal prototype whose lone terminal sits at `term`. The module
// it spawns will try to continue this arm's direction.
BranchModulePrototype armProto(Vec3 term) {
    BranchModulePrototype p;
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    p.nodes.push_back({term, 0.0f, 1.5f, 1.0f});
    p.edges.push_back({0, 1});
    p.rootNode = 0;
    p.terminalNodes = {1};
    return p;
}

// Spawn one child off a single mature root module and return its world
// growth axis. `tune` customises the species before the step. The axis is
// computed while `world` (which owns the prototype the module points at) is
// still alive — returning the module itself would dangle that pointer.
template <typename F>
Vec3 growChildAxis(BranchModulePrototype proto, F tune) {
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, std::move(proto));
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance  = 1.0f;
    tune(p.species);
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;     // mature
    root.vigor = 0.6f;
    root.light = 1.0f;
    p.modules.push_back(root);
    world.plants.push_back(std::move(p));

    step(world, 0.1f);
    const auto& mods = world.plants[0].modules;
    return childAxis(mods.size() > 1 ? mods[1] : mods[0]);
}

} // namespace

// rotateYawPitch must apply pitch (θ about +X) first, then yaw (ψ about
// +Y): an upright axis maps to (sinψ·sinθ, cosθ, cosψ·sinθ). Crucially yaw
// must steer the azimuth of the tilt — the old yaw-then-pitch order left it
// a no-op on the vertical axis, forcing every branch to lean toward +Z.
TEST(rotate_yaw_pitch_is_pitch_then_yaw) {
    const float psi = 0.3f, theta = 0.5f;
    Vec3 v = rotateYawPitch({0.0f, 1.0f, 0.0f}, psi, theta);
    ASSERT(std::fabs(v.x - std::sin(psi) * std::sin(theta)) < 1e-5f, "x = sinψ·sinθ");
    ASSERT(std::fabs(v.y - std::cos(theta)) < 1e-5f,                 "y = cosθ");
    ASSERT(std::fabs(v.z - std::cos(psi) * std::sin(theta)) < 1e-5f, "z = cosψ·sinθ");

    Vec3 a = rotateYawPitch({0.0f, 1.0f, 0.0f}, 0.0f, theta);
    Vec3 b = rotateYawPitch({0.0f, 1.0f, 0.0f}, 1.5707963f, theta);
    ASSERT(vlen(a - b) > 0.1f, "yaw steers the tilt azimuth (not a no-op on +Y)");
}

// A heavy neighbour sphere on +X pushes the gradient descent to orient the
// new module away from +X — collision avoidance is intact under the new
// arm-continuation objective. Tested differentially (blocked vs. unblocked)
// so it depends only on the sign of the collision response, not on an
// absolute axis threshold.
TEST(orientation_avoids_neighbour) {
    auto collisionHeavy = [](Species& s) {
        s.distributionWeightCollisions = 4.0f;
        s.distributionWeightTropism    = 0.1f;
    };
    const float unblockedX =
        growChildAxis(armProto({1.0f, 1.0f, 0.0f}), collisionHeavy).x;

    // Same plant, now with a big blocker straddling +X.
    WorldState world;
    world.shadow.qg.assign(1, 1.0f);
    uint32_t protoIdx = addPrototype(world, armProto({1.0f, 1.0f, 0.0f}));
    addVoronoiSite(world, protoIdx, 0.5f, 0.5f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance = 1.0f;
    collisionHeavy(p.species);
    p.effectiveRootVigorMax = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, protoIdx);
    root.parent = UINT32_MAX;
    root.age = 1.0f;
    root.vigor = 0.6f;
    root.light = 1.0f;
    root.bboxCenter = {0.0f, 0.5f, 0.0f};
    root.bboxRadius = 0.8f;
    p.modules.push_back(root);
    world.plants.push_back(p);

    // A heavy neighbour occupying the child's +X growth corridor. The child
    // attaches at the parent module's grown terminal (~(0.7,0.7,0)) and, left
    // unblocked, continues the arm toward +X/+Y. We plant a fat blocker astride
    // that +X reach: growing toward +X drives the candidate capsule deep into
    // the neighbour, while turning toward -X stays clear.
    //
    // Two things must line up, because a tick builds its spatial index from the
    // *incoming* module bboxes but narrow-phases against the *post-develop*
    // capsules (worldPos->axisTip, diameter*0.5):
    //   - bboxCenter/bboxRadius (set below) must cover the child's broad-phase
    //     radius query, and
    //   - the developed capsule must overlap the child's path. develop overwrites
    //     `diameter` with the species tip value, so girth comes from a large
    //     leafDiameter on the blocker's own species; placing the origin so the
    //     arm's midpoint lands on the corridor keeps the fat rod centred there.
    // The blocker reuses this species' single prototype (protoIdx) — it has a
    // Voronoi site, so the blocker's own spawn pass stays in bounds.
    Plant blocker;
    blocker.species = p.species;
    blocker.species.leafDiameter = 1.5f;      // developed capsule radius ~0.75
    blocker.origin = {1.05f, 1.05f, 0.0f};    // arm midpoint ~ child's +X reach
    BranchModuleInstance ghost;
    ghost.prototype = prototypeAt(world, protoIdx);
    ghost.parent = UINT32_MAX;
    ghost.age = 1.0f;
    ghost.vigor = 0.6f;
    ghost.light = 1.0f;
    ghost.bboxCenter = {1.4f, 1.4f, 0.0f};
    ghost.bboxRadius = 2.5f;                   // broad-phase: cover the query
    blocker.modules.push_back(ghost);
    world.plants.push_back(blocker);

    step(world, 0.1f);

    const auto& plant0 = world.plants[0];
    ASSERT(plant0.modules.size() == 2, "parent spawned a child");
    ASSERT(childAxis(plant0.modules[1]).x < unblockedX - 0.05f,
           "blocking neighbour on +X turned the growth axis away from +X");
}

namespace {

struct CrownStats {
    float spanX = 0, spanY = 0, spanZ = 0;   // world bounding-box extents
    float cx = 0, cz = 0;                     // segment-midpoint centroid (x,z)
    float minY = 0;                           // lowest segment endpoint
    int   segs = 0;
};

// Grow a single whorl plant (no flowering/seeding, so exactly one plant)
// and measure the crown its branch segments span. `orthotropy` is the only
// knob varied; gravitropic droop is dialled down so the shape reflects the
// orientation solve rather than sag.
CrownStats growWhorl(float orthotropy) {
    WorldState world;
    world.shadow.origin = {-8, 0, -8};
    world.shadow.cellSize = 1.0f;
    world.shadow.width = world.shadow.height = world.shadow.depth = 16;
    world.shadow.qg.assign(16 * 16 * 16, 1.0f);
    uint32_t pi = addPrototype(world, whorlModule(4, 0.7f));
    addVoronoiSite(world, pi, 0.5f, 0.3f);

    Plant p;
    p.species = {};
    p.species.moduleMatureAge = 0.5f;
    p.species.shadeTolerance  = 1.0f;
    p.species.floweringAge    = 1000.0f;   // never flowers → never seeds
    p.species.tropismG2       = 0.1f;      // minimal droop
    p.species.rootVigorMax    = 3.0f;
    p.species.apicalControl   = 0.3f;
    p.species.orthotropy      = orthotropy;
    p.effectiveRootVigorMax   = p.species.rootVigorMax;

    BranchModuleInstance root;
    root.prototype = prototypeAt(world, pi);
    root.parent = UINT32_MAX;
    root.vigor = 1.0f;
    root.light = 1.0f;
    p.modules.push_back(root);
    addPlant(world, std::move(p));

    for (int i = 0; i < 220; ++i) step(world, 0.05f);

    CrownStats s;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    float sx = 0, sz = 0;
    int n = 0;
    auto acc = [&](float x, float y, float z) {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
    };
    for (const auto& e : emitPlantSegments(world.plants[0])) {
        acc(e.from.x, e.from.y, e.from.z);
        acc(e.to.x,   e.to.y,   e.to.z);
        sx += (e.from.x + e.to.x) * 0.5f;
        sz += (e.from.z + e.to.z) * 0.5f;
        ++n;
    }
    if (n > 0) { s.spanX = maxX-minX; s.spanY = maxY-minY; s.spanZ = maxZ-minZ;
                 s.cx = sx/n; s.cz = sz/n; s.minY = minY; s.segs = n; }
    return s;
}

} // namespace

// The whole point of the fix: a whorl plant grows a crown that spreads in
// all directions and stays centred over its root — not a vertical column,
// and not a one-direction lean (the reported "hard angle veer").
TEST(whorl_crown_spreads_and_stays_centred) {
    CrownStats s = growWhorl(0.3f);
    ASSERT(s.segs > 30, "grew a real crown");
    // Horizontal spread is a meaningful fraction of height in *both* axes.
    ASSERT(s.spanX > 0.5f * s.spanY, "crown spreads along X, not a column");
    ASSERT(s.spanZ > 0.5f * s.spanY, "crown spreads along Z, not a column");
    // Centroid stays over the root at (0,0): no population-wide lean.
    ASSERT(std::fabs(s.cx) < 0.25f * s.spanX, "crown is centred in X (no lean)");
    ASSERT(std::fabs(s.cz) < 0.25f * s.spanZ, "crown is centred in Z (no lean)");
}

// orthotropy trades height for spread: more orthotropy lifts the crown
// taller and narrower, less spreads it wider.
TEST(orthotropy_trades_height_for_spread) {
    CrownStats low  = growWhorl(0.15f);
    CrownStats high = growWhorl(0.8f);
    ASSERT(high.spanY > low.spanY, "higher orthotropy grows taller");
    ASSERT(low.spanX + low.spanZ > high.spanX + high.spanZ,
           "lower orthotropy spreads wider");
}

// Shoots are negatively gravitropic: with droop dialled down, no branch
// dives below the ground the plant is rooted on.
TEST(shoots_never_aim_below_horizon) {
    CrownStats s = growWhorl(0.3f);
    ASSERT(s.minY > -0.4f, "no shoot drives the crown below the root plane");
}

// Biomechanical tropism: mature branches maintain cantilever gravity sag
// and do not unbend/snap back to straight lines as age grows to large values (10, 50, 100).
TEST(tropism_mature_branches_maintain_cantilever_sag_at_large_ages) {
    BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f}); // root
    proto.nodes.push_back({{1.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f}); // horizontal lateral arm (span = 1.0)
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Species sp;
    sp.tropismDir = {0.0f, -1.0f, 0.0f};
    sp.tropismG1  = 1.0f;
    sp.tropismG2  = 0.5f;

    BranchModuleInstance m;
    m.prototype = &proto;
    m.parent = UINT32_MAX;

    // Root node must identically maintain zero drift across all ages
    for (float testAge : {0.0f, 1.0f, 5.0f, 10.0f, 50.0f, 100.0f}) {
        m.age = testAge;
        Vec3 rootOff = internal::nodeOffsetFromRoot(sp, m, 0);
        ASSERT(vlen(rootOff) < 1e-6f, "root node always maps to exact zero offset");
    }

    // At transition age (age == tropismG1 == 1.0), phototropism balances sag
    m.age = 1.0f;
    Vec3 offTrans = internal::nodeOffsetFromRoot(sp, m, 1);
    ASSERT(std::fabs(offTrans.y) < 1e-5f, "inflection point at age == tropismG1");

    // Mature branches at ages 10, 50, 100 must accumulate and sustain downward sag
    m.age = 10.0f;
    Vec3 off10 = internal::nodeOffsetFromRoot(sp, m, 1);
    ASSERT(off10.y < -0.35f, "mature branch at age 10 sags downward");

    m.age = 50.0f;
    Vec3 off50 = internal::nodeOffsetFromRoot(sp, m, 1);
    ASSERT(off50.y < off10.y, "branch at age 50 sags more than at age 10");

    m.age = 100.0f;
    Vec3 off100 = internal::nodeOffsetFromRoot(sp, m, 1);
    ASSERT(off100.y <= off50.y, "branch at age 100 maintains cantilever sag (does NOT unbend)");
    ASSERT(std::fabs(off100.y - (-0.5f * (99.0f / 101.0f))) < 1e-3f,
           "sag closely matches asymptotic cantilever formula");
}

// Biomechanical tropism: young shoot tips exhibit upward light-seeking phototropism.
TEST(tropism_young_shoots_show_upward_phototropic_curvature) {
    BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.nodes.push_back({{1.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Species sp;
    sp.tropismDir = {0.0f, -1.0f, 0.0f};
    sp.tropismG1  = 1.0f;
    sp.tropismG2  = 0.6f;

    BranchModuleInstance shoot;
    shoot.prototype = &proto;
    shoot.parent = UINT32_MAX;

    // Newborn young shoot (age = 0)
    shoot.age = 0.0f;
    Vec3 tipOff0 = internal::nodeOffsetFromRoot(sp, shoot, 1);
    ASSERT(tipOff0.y > 0.5f, "young shoot curls upward (+Y) toward light");

    // Slightly grown shoot (age = 0.25)
    shoot.age = 0.25f;
    Vec3 tipOff1 = internal::nodeOffsetFromRoot(sp, shoot, 1);
    ASSERT(tipOff1.y > 0.3f, "young shoot still shows positive phototropic lift");
    ASSERT(tipOff1.y < tipOff0.y, "phototropic lift relaxes as shoot tissue matures");
}

// S-Curve: Drooping heavy mature bough with upward-turned young apical tips.
TEST(tropism_botanical_s_curve_and_module_attachment) {
    BranchModulePrototype proto;
    proto.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f}); // 0: base
    proto.nodes.push_back({{1.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f}); // 1: terminal
    proto.edges.push_back({0, 1});
    proto.rootNode = 0;
    proto.terminalNodes = {1};

    Plant plant;
    plant.species.tropismDir = {0.0f, -1.0f, 0.0f};
    plant.species.tropismG1  = 1.0f;
    plant.species.tropismG2  = 0.5f;
    plant.origin = {0.0f, 10.0f, 0.0f};

    // Parent module: mature heavy bough (age = 20)
    BranchModuleInstance parent;
    parent.prototype = &proto;
    parent.parent = UINT32_MAX;
    parent.age = 20.0f;
    parent.vigor = 0.8f;
    parent.nodePositions = {proto.nodes[0].position, proto.nodes[1].position};

    // Child module: newly sprouted apical shoot (age = 0.2)
    BranchModuleInstance child;
    child.prototype = &proto;
    child.parent = 0;
    child.parentAttachTerminal = 1;
    child.age = 0.2f;
    child.vigor = 0.8f;

    plant.modules = {parent, child};

    developModules(plant, 0.0f);

    const auto& mods = plant.modules;
    // Parent bough terminal sags downward
    Vec3 parentTerm = mods[0].worldPos + internal::nodeOffsetFromRoot(plant.species, mods[0], 1);
    ASSERT(parentTerm.y < plant.origin.y, "mature parent bough droops below origin height");

    // Child module is attached at exact parent terminal (zero drift at junction)
    ASSERT(vlen(mods[1].worldPos - parentTerm) < 1e-5f, "child module perfectly anchored to parent terminal");

    // Child tip curls upward relative to its attachment point
    Vec3 childTip = mods[1].worldPos + internal::nodeOffsetFromRoot(plant.species, mods[1], 1);
    ASSERT(childTip.y > mods[1].worldPos.y, "young child shoot tip curls upward creating S-curve");
}
