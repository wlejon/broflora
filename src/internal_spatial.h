#pragma once

// Spatial-hash entry id packing. The per-tick `bromath::SpatialHash3D`
// built in world.cpp uses int32_t ids; we encode the owning plant index
// and module index into a single id so light.cpp and spawning.cpp can
// unpack a query result back into a (plant, module) pair.
//
// Layout: low 20 bits = module index (≈1M per plant), next 11 bits =
// plant index (≈2k). Sign bit stays clear so the int32_t round-trip is
// well-defined.

#include <cstdint>

namespace broflora::internal {

inline int32_t packEntryId(uint32_t plantIdx, uint32_t modIdx) {
    return static_cast<int32_t>((plantIdx << 20) | (modIdx & 0xFFFFFu));
}

inline void unpackEntryId(int32_t id, uint32_t& plantIdx, uint32_t& modIdx) {
    uint32_t u = static_cast<uint32_t>(id);
    plantIdx = u >> 20;
    modIdx   = u & 0xFFFFFu;
}

} // namespace broflora::internal
