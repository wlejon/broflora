#pragma once

// Branch Module — the smallest scale of the multi-scale model. A small
// acyclic graph G = (N, E) of nodes and edges, with exactly one root
// node and one or more terminal nodes. A *prototype* is the static
// template; an *instance* carries runtime state (vigor, light exposure,
// physiological age, bbox, Euler orientation).
//
// Paper §1 (architectural tiers) and §3 (per-tick state).

#include "broflora/vec_math.h"

#include <cstdint>
#include <vector>

namespace broflora {

// Prototype node — the static template. `position` is in the prototype's
// local space; the runtime instance transforms it by Euler orientation.
struct ModuleNode {
    Vec3  position    = {0.0f, 0.0f, 0.0f};
    float ageAtBirth  = 0.0f;   // a_n — physiological age when this node appears
    float lengthMax   = 1.0f;   // l_max — max branch length out of this node
    float thickening  = 1.0f;   // β — pipe-model thickening factor
};

// Prototype edge — undirected pair into `nodes`. Topologically ordered so
// that walking edges in declaration order gives a basipetal traversal of
// the prototype graph (parent index < child index).
struct ModuleEdge {
    uint32_t a = 0;
    uint32_t b = 0;
};

struct BranchModulePrototype {
    std::vector<ModuleNode> nodes;
    std::vector<ModuleEdge> edges;

    // Index into `nodes` of the single root node n_root.
    uint32_t rootNode = 0;

    // Indices into `nodes` of the terminal nodes n_t_i (one or more).
    std::vector<uint32_t> terminalNodes;

    // Optional human-readable name for debugging.
    const char* name = nullptr;
};

// Runtime instance — refers back to a prototype and carries the
// per-tick state. Lifetime of the prototype must exceed any instance
// referencing it.
struct BranchModuleInstance {
    // Pointer to the immutable prototype. Owned externally.
    const BranchModulePrototype* prototype = nullptr;

    // Overall physiological age a_u (paper §3.3).
    float age = 0.0f;

    // Vigor v̄(u) — flowing-resource budget after acropetal pass (paper §3.2).
    float vigor = 0.0f;

    // Light exposure Q(u) after basipetal accumulation. Raw, not yet
    // shade-tolerance-corrected.
    float light = 1.0f;

    // Bounding sphere B_u (centre + radius), maintained as nodes move
    // under tropism deformation. Used by f_collisions.
    Vec3  bboxCenter = {0.0f, 0.0f, 0.0f};
    float bboxRadius = 0.0f;

    // Cached world position of this module's root node n_root, recomputed
    // each development tick by walking from the plant origin through
    // parents. Bbox centre is derived from this plus a local centroid.
    Vec3 worldPos = {0.0f, 0.0f, 0.0f};

    // Branch diameter d_b at the module's root segment, set by the
    // pipe-model walk in `developModules` (paper §3.3):
    //     d_b = (Σ d_c^pipeExp)^(1/pipeExp)         pipeExp = 2.5
    // For terminal modules d_b is the species-defined leaf radius.
    float diameter = 0.0f;

    // Orientation as the paper's Euler triple (φ, θ, ψ). Applied on top
    // of the parent module's frame.
    Euler3 orientation = {};

    // Index of the parent module in the owning Plant, or UINT32_MAX for root.
    uint32_t parent = UINT32_MAX;

    // Index of the terminal node on the parent that this module attaches to.
    // Unused for the plant's root module.
    uint32_t parentAttachTerminal = 0;
};

} // namespace broflora
