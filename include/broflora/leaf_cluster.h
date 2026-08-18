#pragma once

#include <bromesh/mesh_data.h>
#include <bromesh/procedural/plants.h>
#include <bromesh/procedural/leaf_scatter.h>
#include <bromesh/procedural/space_colonization.h>
#include <bromath/vec.h>

#include "broflora/plant.h"
#include "broflora/world.h"

#include <vector>

namespace broflora {

using MeshData = bromesh::MeshData;
using LeafShape = bromesh::LeafShape;
using LeafPlacements = bromesh::LeafPlacements;
using BranchSegment = bromesh::BranchSegment;

/// Botanical leaf arrangement pattern along a twig or shoot axis.
enum class Phyllotaxy : int {
    Alternate = 0,       ///< Distichous / 2-ranked spiral, alternating sides along twig (oak, elm, birch, beech)
    Opposite = 1,        ///< Decussate paired leaves opposite each other with 90° node twist (maple, ash, lilac)
    Spiral = 2,          ///< Golden angle (~137.5°) rosette along shoot axis (magnolia, apple, cherry)
    Fascicle = 3,        ///< Pine needle bundle: 2–5 needles radiating from a basal sheath
    CompoundPinnate = 4, ///< Paired lateral leaflets along a central rachis with a terminal leaflet (walnut, rowan, acacia)
};

/// Configuration options for procedural botanical leaf clusters and twig sprays.
struct LeafClusterOptions {
    /// Number of leaves / leaflets in the cluster.
    int count = 6;
    /// Length of the supporting micro-twig / rachis along local +Z.
    float twigLength = 0.25f;
    /// Radius of the supporting micro-twig.
    float twigRadius = 0.005f;
    /// Length of the individual leaf stalk (petiole) connecting leaf to twig.
    float petioleLength = 0.04f;
    /// Width of individual leaf cards.
    float leafWidth = 0.12f;
    /// Length of individual leaf cards.
    float leafLength = 0.20f;
    /// Leaf shape profile (atlas cell or silhouette).
    LeafShape leafShape = LeafShape::Oval;
    /// Compatibility alias for leafShape.
    LeafShape shape = LeafShape::Oval;
    /// Length-wise bend deflection (radians).
    float leafBend = 0.3f;
    /// Axial twist curl (radians).
    float leafCurl = 0.1f;
    /// Bilateral transverse cupping.
    float leafCup = 0.2f;
    /// Gravitational sag deflection along petiole and leaf.
    float droop = 0.2f;
    /// Phototropic bias: adaxial surface turned toward sky/light (+Y).
    float upBias = 0.6f;
    /// Lateral fan/divergence angle (radians) from the central twig axis.
    float spread = 0.7f;
    /// If true, generates the micro-twig cylinder stem so leaves aren't floating in space.
    bool includeTwigMesh = true;
    /// If true, modulates leaf card width geometrically by shape silhouette.
    bool shapedSilhouette = true;
    /// If true, leaf card UVs span full [0, 1] instead of 4x4 atlas cell.
    bool fullUV = false;
};

/// Build a low-poly botanical leaf cluster / twig spray with petioles and leaves
/// in the specified phyllotaxy arrangement.
///
/// Output mesh is in local space:
/// - Twig root is at (0, 0, 0), extending along local +Z to (0, 0, twigLength)
/// - Local +Y is the upward / light-facing normal direction
/// - Local +X is the lateral spreading axis
///
/// Vertex colors encode wind bend in the R channel: 0.0 at the twig base attachment,
/// scaling smoothly to 1.0 at the outermost leaf tips.
MeshData leafCluster(Phyllotaxy phyllotaxy, const LeafClusterOptions& opts = {});

/// Placement options for scattering leaf clusters along branch segments.
struct LeafClusterPlacementOptions : public bromesh::LeafPlacementOptions {
    /// Outward branching angle flare (in radians) from the branch tangent axis.
    float branchAngle = 0.5f;
};

/// Compute leaf cluster instance transforms along branch segments.
LeafPlacements placeLeafClustersOnBranches(
    const std::vector<BranchSegment>& segments,
    const LeafClusterPlacementOptions& opts = {});

/// Stamp botanical leaf clusters in the specified phyllotaxy arrangement
/// along branch segments and return a single merged mesh.
MeshData scatterLeafClusters(
    const std::vector<BranchSegment>& segments,
    Phyllotaxy phyllotaxy,
    const LeafClusterOptions& clusterOpts = {},
    const LeafClusterPlacementOptions& placementOpts = {});

/// Compute leaf cluster instance transforms for a single plant.
/// Uses the plant's grown branch segments and automatically populates
/// segment density weights from simulated foliage vigor/maturity/light if none are set.
LeafPlacements placeLeafClustersOnPlant(
    const Plant& plant,
    const LeafClusterPlacementOptions& opts = {});

/// Compute leaf cluster instance transforms specifically on terminal twigs of a plant.
LeafPlacements placeLeafClustersOnTerminals(
    const Plant& plant,
    const LeafClusterPlacementOptions& opts = {});

/// Emit a single merged foliage mesh for a plant composed of botanical leaf clusters.
MeshData emitPlantLeafClusters(
    const Plant& plant,
    Phyllotaxy phyllotaxy,
    const LeafClusterOptions& clusterOpts = {},
    const LeafClusterPlacementOptions& placementOpts = {});

/// Emit a single merged foliage mesh for all plants in a world composed of botanical leaf clusters.
MeshData emitWorldLeafClusters(
    const WorldState& world,
    Phyllotaxy phyllotaxy,
    const LeafClusterOptions& clusterOpts = {},
    const LeafClusterPlacementOptions& placementOpts = {});

} // namespace broflora
