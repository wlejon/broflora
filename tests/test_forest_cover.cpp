#include "test_framework.h"

#include <cmath>

using namespace broflora;

TEST(forest_cover_open_trunk_and_closed_canopy) {
    ForestCoverOptions o;
    o.floor = 0.25;
    o.closed = 4.0;
    o.trunkClear = 0.7;
    const ForestCover none({}, o);
    ASSERT(none.at(3.0, -7.0) == 1.0, "no trees: full cover");

    std::vector<ForestCoverTree> trees;
    trees.push_back({0.0, 0.0, 0.5, 0.0, 3.0});
    const ForestCover one(trees, o);
    ASSERT(one.at(0.0, 0.0) == 0.0, "bare at the trunk");
    ASSERT(one.at(40.0, 40.0) == 1.0, "full in the open");
    const double shade = one.at(2.5, 1.5);
    ASSERT(shade > 0.0 && shade < 1.0, "thin in the crown's shade");

    // Six crowns over one spot (trunks away from it) close the canopy.
    std::vector<ForestCoverTree> stand;
    for (int k = 0; k < 6; ++k) {
        const double a = k * 1.047;
        stand.push_back({10 + 6 * std::cos(a), 10 + 6 * std::sin(a), 10.0, 10.0, 4.0});
    }
    const ForestCover closed(stand, o);
    ASSERT(closed.at(10.0, 10.0) == 0.0, "nothing under a closed canopy");
    ASSERT(closed.at(NAN, 0.0) == 1.0, "a NaN point is open ground");
    // Across a 16 m cell boundary the answer is continuous.
    ASSERT(std::abs(one.at(15.999, 1.0) - one.at(16.001, 1.0)) < 1e-3, "cells join");
}
