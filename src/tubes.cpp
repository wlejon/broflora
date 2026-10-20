#include "broflora/tubes.h"

#include <algorithm>
#include <cmath>

namespace broflora {

using bromath::Vec3;

std::vector<TubeSegment> tubeSegments(const std::vector<bromesh::BranchSegment>& segs, float minRadius) {
    TubeShape shape;
    shape.minRadius = minRadius;
    return tubeSegments(segs, shape);
}

void appendTubeVegetation(std::vector<float>& out, const TubeSegment& t, const std::vector<SegmentSway>& sway,
                          const std::vector<bromesh::BranchSegment>& segs) {
    const size_t i = static_cast<size_t>(t.segment);
    if (i >= sway.size() || i >= segs.size()) return;
    appendSegmentVegetation(out, sway[i], t.along0, t.along1, bromath::vlen(segs[i].to - segs[i].from));
}

std::vector<TubeSegment> tubeSegments(const std::vector<bromesh::BranchSegment>& segs, const TubeShape& shape) {
    const float minRadius = shape.minRadius;
    // Each segment's continuation: its thickest child.
    std::vector<int> mainChild(segs.size(), -1);
    for (size_t i = 0; i < segs.size(); ++i) {
        const int p = segs[i].parent;
        if (p < 0 || static_cast<size_t>(p) >= segs.size()) continue;
        if (mainChild[p] < 0 || segs[i].radius > segs[mainChild[p]].radius) mainChild[p] = static_cast<int>(i);
    }
    const auto dirOf = [&](size_t i) {
        const Vec3 d = segs[i].to - segs[i].from;
        const float l = bromath::vlen(d);
        return l > 1e-6f ? d * (1.0f / l) : Vec3{0.0f, 1.0f, 0.0f};
    };
    const auto bisect = [](const Vec3& a, const Vec3& b) {
        const Vec3 s = a + b;
        const float l = bromath::vlen(s);
        return l > 0.2f ? s * (1.0f / l) : b;
    };

    std::vector<TubeSegment> out;
    out.reserve(segs.size());
    for (size_t i = 0; i < segs.size(); ++i) {
        const auto& s = segs[i];
        if (bromath::vlen(s.to - s.from) < 1e-6f) continue;
        if (s.radius < minRadius) continue;
        TubeSegment t;
        t.from = s.from;
        t.to = s.to;
        t.rTo = s.radius;
        t.rFrom = s.radius;
        const int p = s.parent;
        const bool hasParent = p >= 0 && static_cast<size_t>(p) < segs.size();
        const bool continues = hasParent && mainChild[p] == static_cast<int>(i);
        if (hasParent && segs[p].radius > 0.0f)
            t.rFrom = continues ? segs[p].radius : std::min(segs[p].radius, 1.5f * s.radius);
        const Vec3 di = dirOf(i);
        // A side branch leaves its parent half along it, so its first ring
        // lies tilted into the parent, not across it.
        t.tangentFrom = hasParent ? bisect(dirOf(static_cast<size_t>(p)), di) : di;
        t.tangentTo = mainChild[i] >= 0 ? bisect(di, dirOf(static_cast<size_t>(mainChild[i]))) : di;
        t.segment = static_cast<int32_t>(i);

        // The pieces of the segment, at these distances from its from-end
        // with these radii (the first and last ends are t's own).
        const float L = bromath::vlen(s.to - s.from), r = s.radius;
        float cut[4], rad[4];
        int cuts = 0;
        if (!hasParent && shape.rootFlare > 0.0f && r > 0.0f) {
            // The root flare: r (1 + F e^(-h / r)), sampled where it bends.
            for (float h : {0.35f, 1.0f, 2.2f}) {
                if (h * r > L - 0.25f * r) break;
                cut[cuts] = h * r;
                rad[cuts++] = r * (1.0f + shape.rootFlare * std::exp(-h));
            }
            t.rFrom = r * (1.0f + shape.rootFlare);
        } else if (hasParent && !continues && shape.branchCollar > 0.0f && L > 4.0f * r) {
            // The collar: a short swell at the fork, then the branch.
            if (segs[p].radius > 0.0f) t.rFrom = std::min(segs[p].radius, (1.0f + shape.branchCollar) * r);
            cut[0] = 2.0f * r;
            rad[cuts++] = r * (1.0f + 0.25f * shape.branchCollar);
        }
        if (cuts == 0) {
            out.push_back(t);
            continue;
        }
        float at = 0.0f, rAt = t.rFrom;
        for (int k = 0; k <= cuts; ++k) {
            TubeSegment piece = t;
            const float end = k < cuts ? cut[k] : L;
            piece.from = s.from + di * at;
            piece.to = k < cuts ? s.from + di * end : s.to;
            piece.rFrom = rAt;
            piece.rTo = k < cuts ? rad[k] : t.rTo;
            piece.tangentFrom = k == 0 ? t.tangentFrom : di;
            piece.tangentTo = k < cuts ? di : t.tangentTo;
            piece.along0 = at / L;
            piece.along1 = k < cuts ? end / L : 1.0f;
            out.push_back(piece);
            at = end;
            rAt = piece.rTo;
        }
    }
    return out;
}

} // namespace broflora
