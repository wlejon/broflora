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
    /// How shaded the floor is at a point: the crowns over it, 0 in the
    /// open, 1 under one crown's middle or where crowns overlap (the leaf
    /// litter and the understory bro.scene's grass draws there).
    double canopyAt(double x, double z) const;
    /// at() and canopyAt() over a grid, `cols` x `rows` points (i, j) at
    /// (x0 + i * cell, z0 + j * cell), row-major; either output may be null.
    /// The same answers, a world's grass field at a time, on every core.
    void grid(double x0, double z0, double cell, int cols, int rows, float* cover, float* canopy) const;

private:
    // The cover left by that overlap and clearing (at()'s answer).
    double coverOf(double canopy, double trunk) const;
    // The crowns' overlap at a point and how clear of trunks it is.
    void sum(double x, double z, double& canopy, double& trunk) const;
    // The trees by grid cell (a counting sort: each cell keeps them in the
    // order given), structure of arrays.
    std::vector<double> x_, z_, cx_, cz_, r_;
    std::vector<int32_t> start_;
    int32_t gi0_ = 0, gj0_ = 0, gi1_ = -1, gj1_ = -1, gw_ = 0, span_ = 0;
    ForestCoverOptions opts_;
    double trunkFar2_ = 0.0;
};

} // namespace broflora
