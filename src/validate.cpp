#include "broflora/validate.h"

#include <string>

namespace broflora {

namespace {

void setErr(std::string* err, std::string msg) {
    if (err) *err = std::move(msg);
}

} // namespace

bool validate(const Plant& plant, std::string* err) {
    const auto& mods = plant.modules;
    if (mods.empty()) return true;  // an empty plant is trivially valid

    if (mods.front().parent != UINT32_MAX) {
        setErr(err, "plant.modules[0] is not a root (parent != UINT32_MAX)");
        return false;
    }
    for (size_t i = 0; i < mods.size(); ++i) {
        const auto& m = mods[i];
        if (!m.prototype) {
            setErr(err, "module " + std::to_string(i) + " has null prototype");
            return false;
        }
        if (i == 0) continue;
        if (m.parent == UINT32_MAX) {
            setErr(err, "module " + std::to_string(i) + " is a second root");
            return false;
        }
        if (m.parent >= i) {
            setErr(err, "module " + std::to_string(i) +
                        " violates topological order (parent=" +
                        std::to_string(m.parent) + ")");
            return false;
        }
    }
    return true;
}

bool validate(const WorldState& world, std::string* err) {
    const uint32_t protoCount = static_cast<uint32_t>(world.prototypes.size());
    for (size_t i = 0; i < world.voronoi.size(); ++i) {
        const auto& site = world.voronoi[i];
        if (site.prototypeIndex >= protoCount) {
            setErr(err, "voronoi[" + std::to_string(i) +
                        "].prototypeIndex out of range (" +
                        std::to_string(site.prototypeIndex) + " >= " +
                        std::to_string(protoCount) + ")");
            return false;
        }
    }
    for (size_t i = 0; i < world.plants.size(); ++i) {
        std::string sub;
        if (!validate(world.plants[i], &sub)) {
            setErr(err, "plants[" + std::to_string(i) + "]: " + sub);
            return false;
        }
    }
    return true;
}

} // namespace broflora
