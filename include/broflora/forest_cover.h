#pragma once

// ForestCover — how much ground cover (grass) grows at a point under a
// planted forest: 0 under a closed canopy, thin in the crowns' shade, clear
// at each trunk, full in the open. Asked once per texel by a grass bake,
// hundreds of thousands of times, so the trees are bucketed into a grid of
// 16 m cells and each query walks only the cells a crown can reach.
//
// bro.flora.forestCover is this (the JS side turns the forest's instances
// into trees); the answers are the same doubles the JS function computed.

#include <cstdint>
#include <vector>

namespace broflora {

struct ForestCoverTree {
    double x = 0.0, z = 0.0;            // the trunk
    double crownX = 0.0, crownZ = 0.0;  // the crown's centre
    double crownRadius = 0.5;           // at least 0.5
};

struct ForestCoverOptions {
    double floor = 0.35;      // the shade's least cover before the canopy closes
    double closed = 1.6;      // the crown overlap at which nothing grows
    double trunkClear = 0.7;  // bare ground round a trunk, metres
};

class ForestCover {
public:
    ForestCover(const std::vector<ForestCoverTree>& trees, const ForestCoverOptions& opts);
    double at(double x, double z) const;

private:
    // The trees by grid cell (a counting sort: each cell keeps them in the
    // order given), structure of arrays.
    std::vector<double> x_, z_, cx_, cz_, r_;
    std::vector<int32_t> start_;
    int32_t gi0_ = 0, gj0_ = 0, gi1_ = -1, gj1_ = -1, gw_ = 0, span_ = 0;
    ForestCoverOptions opts_;
    double trunkFar2_ = 0.0;
};

} // namespace broflora
