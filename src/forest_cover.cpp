#include "broflora/forest_cover.h"

#include <algorithm>
#include <cmath>

namespace broflora {

namespace {

constexpr double kCell = 16.0;

inline double smooth(double a, double b, double x) {
    const double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
}

}  // namespace

ForestCover::ForestCover(const std::vector<ForestCoverTree>& trees, const ForestCoverOptions& opts)
    : opts_(opts) {
    const size_t m = trees.size();
    double reach = 1.0;
    std::vector<int32_t> gi(m), gj(m);
    for (size_t k = 0; k < m; ++k) {
        const auto& t = trees[k];
        reach = std::max(reach, t.crownRadius);
        const auto a = static_cast<int32_t>(std::floor(t.crownX / kCell));
        const auto b = static_cast<int32_t>(std::floor(t.crownZ / kCell));
        gi[k] = a;
        gj[k] = b;
        if (k == 0) {
            gi0_ = gi1_ = a;
            gj0_ = gj1_ = b;
        }
        gi0_ = std::min(gi0_, a);
        gi1_ = std::max(gi1_, a);
        gj0_ = std::min(gj0_, b);
        gj1_ = std::max(gj1_, b);
    }
    gw_ = gi1_ - gi0_ + 1;
    const int32_t gh = gj1_ - gj0_ + 1;
    const size_t cells = static_cast<size_t>(std::max(0, gw_ * gh));
    start_.assign(cells + 1, 0);
    for (size_t k = 0; k < m; ++k) start_[static_cast<size_t>((gj[k] - gj0_) * gw_ + (gi[k] - gi0_)) + 1]++;
    for (size_t c = 0; c < cells; ++c) start_[c + 1] += start_[c];
    std::vector<int32_t> fill(start_.begin(), start_.begin() + static_cast<std::ptrdiff_t>(cells));
    x_.resize(m);
    z_.resize(m);
    cx_.resize(m);
    cz_.resize(m);
    r_.resize(m);
    for (size_t k = 0; k < m; ++k) {
        const size_t s = static_cast<size_t>(fill[static_cast<size_t>((gj[k] - gj0_) * gw_ + (gi[k] - gi0_))]++);
        x_[s] = trees[k].x;
        z_[s] = trees[k].z;
        cx_[s] = trees[k].crownX;
        cz_[s] = trees[k].crownZ;
        r_[s] = trees[k].crownRadius;
    }
    span_ = static_cast<int32_t>(std::ceil(reach / kCell));
    // Past these the terms are exactly 1 (trunk) and 0 (canopy); the margin
    // keeps a distance that rounds to the edge on the computed side.
    const double clear = opts_.trunkClear;
    trunkFar2_ = (clear * 3.5) * (clear * 3.5) * 1.001;
}

double ForestCover::at(double x, double z) const {
    double canopy = 0.0, trunk = 1.0;
    sum(x, z, canopy, trunk);
    const double closed = opts_.closed, floor = opts_.floor;
    const double lit = 1 - std::min(1.0, canopy / closed);
    return trunk * (1 - smooth(0.7 * closed, closed, canopy)) * (floor + (1 - floor) * lit * lit);
}

double ForestCover::canopyAt(double x, double z) const {
    double canopy = 0.0, trunk = 1.0;
    sum(x, z, canopy, trunk);
    return std::min(1.0, canopy);
}

void ForestCover::sum(double x, double z, double& canopy, double& trunk) const {
    const double clear = opts_.trunkClear;
    // Far off (or NaN) no tree reaches: the open-ground answer, as in JS.
    const bool inRange = std::abs(x) < 1e9 && std::abs(z) < 1e9;
    const int32_t gi = inRange ? static_cast<int32_t>(std::floor(x / kCell)) : 0;
    const int32_t gj = inRange ? static_cast<int32_t>(std::floor(z / kCell)) : 0;
    const int32_t ja = std::max(gj - span_, gj0_), jb = inRange ? std::min(gj + span_, gj1_) : ja - 1;
    const int32_t ia = std::max(gi - span_, gi0_), ib = std::min(gi + span_, gi1_);
    for (int32_t j = ja; j <= jb; ++j) {
        const int32_t row = (j - gj0_) * gw_ - gi0_;
        for (int32_t i = ia; i <= ib; ++i) {
            const int32_t end = start_[static_cast<size_t>(row + i + 1)];
            for (int32_t k = start_[static_cast<size_t>(row + i)]; k < end; ++k) {
                const double dx = x - cx_[k], dz = z - cz_[k], r = r_[k];
                if (dx * dx + dz * dz < 1.3225 * 1.001 * r * r) {
                    const double d = std::hypot(dx, dz) / r;
                    if (d < 1.15) canopy += 1 - smooth(0.55, 1.15, d);
                }
                const double ex = x - x_[k], ez = z - z_[k];
                if (ex * ex + ez * ez < trunkFar2_) trunk = std::min(trunk, smooth(clear, clear * 3.5, std::hypot(ex, ez)));
            }
        }
    }
}

}  // namespace broflora
