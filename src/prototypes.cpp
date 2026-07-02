#include "broflora/prototypes.h"

#include "bromath/scalar.h"

#include <cmath>

namespace broflora {

BranchModulePrototype straightModule(const char* name) {
    BranchModulePrototype p;
    p.name = name;
    // {position, ageAtBirth, lengthMax, thickening}
    p.nodes.push_back({{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 1.0f});
    p.nodes.push_back({{0.0f, 1.0f, 0.0f}, 0.2f, 1.0f, 1.0f});
    p.edges.push_back({0, 1});
    p.rootNode = 0;
    p.terminalNodes = {1};
    return p;
}

BranchModulePrototype forkModule(const char* name) {
    BranchModulePrototype p;
    p.name = name;
    p.nodes.push_back({{ 0.0f, 0.0f, 0.0f}, 0.0f,  1.0f, 1.0f});  // root
    p.nodes.push_back({{ 0.35f, 1.0f, 0.0f}, 0.25f, 1.0f, 1.0f}); // term A
    p.nodes.push_back({{-0.35f, 1.0f, 0.0f}, 0.25f, 1.0f, 1.0f}); // term B
    p.edges.push_back({0, 1});
    p.edges.push_back({0, 2});
    p.rootNode = 0;
    p.terminalNodes = {1, 2};
    return p;
}

BranchModulePrototype whorlModule(uint32_t arms, float spread, const char* name) {
    if (arms < 2) arms = 2;
    if (arms > 8) arms = 8;
    if (spread < 0.0f) spread = 0.0f;
    if (spread > 1.0f) spread = 1.0f;

    BranchModulePrototype p;
    p.name = name;

    // Node 0: root. Node 1: top of a short trunk the arms fan from. Keeping
    // a real trunk segment (rather than fanning straight off the root) gives
    // the pipe-model a thicker bole and reads as a stem, not a bare burst.
    const float trunkLen = 0.5f;
    const float rise     = 0.7f;          // how much arms climb above the fork
    const float radius   = 0.25f + spread * 0.6f;  // outward reach of arms

    p.nodes.push_back({{0.0f, 0.0f,      0.0f}, 0.0f, 1.0f, 1.0f});  // 0 root
    p.nodes.push_back({{0.0f, trunkLen,  0.0f}, 0.1f, 1.0f, 1.0f});  // 1 fork
    p.edges.push_back({0, 1});
    p.rootNode = 0;

    for (uint32_t i = 0; i < arms; ++i) {
        const float a = bromath::TWO_PI * static_cast<float>(i)
                        / static_cast<float>(arms);
        const float x = std::cos(a) * radius;
        const float z = std::sin(a) * radius;

        // Each arm is a two-segment curved chain rather than a single straight
        // edge: a mid node bowed up off the fork→tip chord gives the arm a
        // gentle upward arc so it reads as a branch, not a rod. The tip keeps
        // its original position, so the crown silhouette and where child
        // modules attach are unchanged — only the straightness is broken, and
        // the extra node also thickens the emitted growth.
        const float tipY = trunkLen + rise;
        const float armLen = std::sqrt(x * x + rise * rise + z * z);
        const float bow = armLen * 0.16f;
        const uint32_t midIdx = static_cast<uint32_t>(p.nodes.size());
        p.nodes.push_back({{x * 0.5f, trunkLen + rise * 0.5f + bow, z * 0.5f},
                           0.2f, 1.0f, 1.0f});
        p.edges.push_back({1, midIdx});

        const uint32_t tipIdx = static_cast<uint32_t>(p.nodes.size());
        p.nodes.push_back({{x, tipY, z}, 0.3f, 1.0f, 1.0f});
        p.edges.push_back({midIdx, tipIdx});
        p.terminalNodes.push_back(tipIdx);
    }
    return p;
}

} // namespace broflora
