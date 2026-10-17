#include "broflora/world_io.h"

#include "broflora/sim_hash.h"  // generated: BROFLORA_SIM_HASH

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace broflora {

namespace {

// A field added to any of these structs changes its size, and so fails
// here until it is written and read below (the mirrors list the fields
// the serializer covers, in order).
struct MirrorInstance {
    const void* prototype; float age, vigor, light, lightExposure, subtreeLight;
    bromath::Vec3 bboxCenter; float bboxRadius; bromath::Vec3 axisTip, worldPos;
    float diameter; Euler3 orientation; uint32_t parent, parentAttachTerminal;
    std::vector<bromath::Vec3> nodePositions; bool isMainChild;
};
static_assert(sizeof(BranchModuleInstance) == sizeof(MirrorInstance), "world_io: BranchModuleInstance changed");
struct MirrorPlant {
    Species species; bromath::Vec3 origin; float age; std::vector<BranchModuleInstance> modules;
    float effectiveRootVigorMax; bool flowering, senescing;
};
static_assert(sizeof(Plant) == sizeof(MirrorPlant), "world_io: Plant changed");
struct MirrorPrototype {
    std::vector<ModuleNode> nodes; std::vector<ModuleEdge> edges; uint32_t rootNode;
    std::vector<uint32_t> terminalNodes; const char* name;
};
static_assert(sizeof(BranchModulePrototype) == sizeof(MirrorPrototype), "world_io: BranchModulePrototype changed");
struct MirrorWorld {
    TerrainMap terrain; SoilMap soil; ShadowGrid shadow; GlobalClimate climate;
    std::vector<BranchModulePrototype> prototypes; std::vector<PrototypeVoronoiSite> voronoi;
    std::vector<Plant> plants; uint64_t rngState; double simTime;
};
static_assert(sizeof(WorldState) == sizeof(MirrorWorld), "world_io: WorldState changed");
struct MirrorShadow { bromath::Vec3 origin; float cellSize; uint32_t width, height, depth; std::vector<float> qg; };
static_assert(sizeof(ShadowGrid) == sizeof(MirrorShadow), "world_io: ShadowGrid changed");
struct MirrorTerrain { GridFootprint2D footprint; std::vector<float> height; };
static_assert(sizeof(TerrainMap) == sizeof(MirrorTerrain), "world_io: TerrainMap changed");
struct MirrorSoil { GridFootprint2D footprint; std::vector<uint8_t> blocked; };
static_assert(sizeof(SoilMap) == sizeof(MirrorSoil), "world_io: SoilMap changed");

// Written whole: all 4-byte fields, so no padding reaches the bytes (and
// the cache key built from them).
static_assert(sizeof(Species) == 64 * 4, "world_io: Species changed");
static_assert(sizeof(ModuleNode) == 6 * 4, "world_io: ModuleNode changed");
static_assert(sizeof(ModuleEdge) == 2 * 4, "world_io: ModuleEdge changed");
static_assert(sizeof(PrototypeVoronoiSite) == 4 * 4, "world_io: PrototypeVoronoiSite changed");
static_assert(sizeof(GlobalClimate) == 3 * 4, "world_io: GlobalClimate changed");
static_assert(sizeof(GridFootprint2D) == 5 * 4, "world_io: GridFootprint2D changed");
static_assert(sizeof(bromath::Vec3) == 12 && sizeof(Euler3) == 12, "world_io: Vec3/Euler3 changed");

constexpr uint32_t kMagic = 0x574c4642;  // "BFLW"
constexpr uint32_t kFormat = 6;   // 2: Species gained the bark fields; 3: the habit, foliage and colour fields, site groups; 4: leafSize; 5: clumpFlat, clumpReach; 6: crownLift, heightMax, clumpFill, the bloom fields

struct Writer {
    std::vector<uint8_t> b;
    void bytes(const void* p, size_t n) {
        const auto* c = static_cast<const uint8_t*>(p);
        b.insert(b.end(), c, c + n);
    }
    template <class T> void pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes(&v, sizeof(T));
    }
    template <class T> void vec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        pod<uint64_t>(v.size());
        bytes(v.data(), v.size() * sizeof(T));
    }
};

struct Reader {
    const uint8_t* p;
    size_t n;
    bool ok = true;
    bool bytes(void* out, size_t k) {
        if (!ok || k > n) return ok = false;
        std::memcpy(out, p, k);
        p += k;
        n -= k;
        return true;
    }
    template <class T> bool pod(T& v) { return bytes(&v, sizeof(T)); }
    bool count(uint64_t& c, size_t elem) {
        if (!pod(c)) return false;
        if (elem && c > n / elem) return ok = false;
        return true;
    }
    template <class T> bool vec(std::vector<T>& v) {
        uint64_t c = 0;
        if (!count(c, sizeof(T))) return false;
        v.resize(static_cast<size_t>(c));
        return bytes(v.data(), v.size() * sizeof(T));
    }
};

uint64_t fnv64(uint64_t h, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}
constexpr uint64_t kFnvBasis = 1469598103934665603ULL;

// What else the grown bits depend on: the compiler, its version, the
// configuration and the target.
std::string buildTag() {
    std::string t;
#if defined(_MSC_VER)
    t += "msvc" + std::to_string(_MSC_FULL_VER);
#endif
#if defined(__clang__)
    t += "clang" + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    t += "gcc" + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#endif
#if defined(_M_X64) || defined(__x86_64__)
    t += "-x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    t += "-arm64";
#endif
#if defined(NDEBUG)
    t += "-ndebug";
#endif
#if defined(__FAST_MATH__)
    t += "-fastmath";
#endif
#if defined(_OPENMP)
    t += "-omp";
#endif
    return t;
}

}  // namespace

const char* simHash() { return BROFLORA_SIM_HASH; }

const char* internName(std::string_view name) {
    static std::mutex mu;
    static std::unordered_set<std::string>* names = new std::unordered_set<std::string>();  // never freed: pointers outlive statics
    std::lock_guard<std::mutex> lock(mu);
    return names->emplace(name).first->c_str();
}

std::vector<uint8_t> writeWorld(const WorldState& world, std::string* err) {
    Writer w;
    w.pod(kMagic);
    w.pod(kFormat);

    w.pod(world.terrain.footprint);
    w.vec(world.terrain.height);
    w.pod(world.soil.footprint);
    w.vec(world.soil.blocked);
    w.pod(world.shadow.origin);
    w.pod(world.shadow.cellSize);
    w.pod(world.shadow.width);
    w.pod(world.shadow.height);
    w.pod(world.shadow.depth);
    w.vec(world.shadow.qg);
    w.pod(world.climate);

    w.pod<uint64_t>(world.prototypes.size());
    for (const auto& p : world.prototypes) {
        w.vec(p.nodes);
        w.vec(p.edges);
        w.pod(p.rootNode);
        w.vec(p.terminalNodes);
        const uint8_t hasName = p.name ? 1 : 0;
        w.pod(hasName);
        if (p.name) {
            const size_t len = std::strlen(p.name);
            w.pod<uint64_t>(len);
            w.bytes(p.name, len);
        }
    }
    w.vec(world.voronoi);

    w.pod<uint64_t>(world.plants.size());
    for (const auto& pl : world.plants) {
        w.pod(pl.species);
        w.pod(pl.origin);
        w.pod(pl.age);
        w.pod(pl.effectiveRootVigorMax);
        w.pod<uint8_t>(pl.flowering ? 1 : 0);
        w.pod<uint8_t>(pl.senescing ? 1 : 0);
        w.pod<uint64_t>(pl.modules.size());
        for (const auto& m : pl.modules) {
            uint32_t proto = UINT32_MAX;
            if (m.prototype) {
                const BranchModulePrototype* base = world.prototypes.data();
                if (m.prototype < base || m.prototype >= base + world.prototypes.size()) {
                    if (err) *err = "writeWorld: a module's prototype is not one of the world's";
                    return {};
                }
                proto = static_cast<uint32_t>(m.prototype - base);
            }
            w.pod(proto);
            w.pod(m.age);
            w.pod(m.vigor);
            w.pod(m.light);
            w.pod(m.lightExposure);
            w.pod(m.subtreeLight);
            w.pod(m.bboxCenter);
            w.pod(m.bboxRadius);
            w.pod(m.axisTip);
            w.pod(m.worldPos);
            w.pod(m.diameter);
            w.pod(m.orientation);
            w.pod(m.parent);
            w.pod(m.parentAttachTerminal);
            w.vec(m.nodePositions);
            w.pod<uint8_t>(m.isMainChild ? 1 : 0);
        }
    }
    w.pod(world.rngState);
    w.pod(world.simTime);
    return std::move(w.b);
}

bool readWorld(const uint8_t* data, size_t size, WorldState& out, std::string* err) {
    auto fail = [&](const char* why) {
        if (err) *err = std::string("readWorld: ") + why;
        return false;
    };
    Reader r{data, size};
    uint32_t magic = 0, format = 0;
    if (!r.pod(magic) || magic != kMagic) return fail("not a broflora world");
    if (!r.pod(format) || format != kFormat) return fail("another format version");

    WorldState w;
    r.pod(w.terrain.footprint);
    r.vec(w.terrain.height);
    r.pod(w.soil.footprint);
    r.vec(w.soil.blocked);
    r.pod(w.shadow.origin);
    r.pod(w.shadow.cellSize);
    r.pod(w.shadow.width);
    r.pod(w.shadow.height);
    r.pod(w.shadow.depth);
    r.vec(w.shadow.qg);
    r.pod(w.climate);

    uint64_t protoCount = 0;
    if (!r.count(protoCount, 1)) return fail("truncated");
    w.prototypes.resize(static_cast<size_t>(protoCount));
    for (auto& p : w.prototypes) {
        r.vec(p.nodes);
        r.vec(p.edges);
        r.pod(p.rootNode);
        r.vec(p.terminalNodes);
        uint8_t hasName = 0;
        r.pod(hasName);
        if (hasName) {
            uint64_t len = 0;
            if (!r.count(len, 1)) return fail("truncated");
            std::string name(static_cast<size_t>(len), '\0');
            r.bytes(name.data(), name.size());
            p.name = internName(name);
        }
        if (!r.ok) return fail("truncated");
    }
    r.vec(w.voronoi);
    for (const auto& s : w.voronoi) {
        if (s.prototypeIndex >= w.prototypes.size()) return fail("a Voronoi site's prototype is out of range");
    }

    uint64_t plantCount = 0;
    if (!r.count(plantCount, 1)) return fail("truncated");
    w.plants.resize(static_cast<size_t>(plantCount));
    for (auto& pl : w.plants) {
        uint8_t flowering = 0, senescing = 0;
        r.pod(pl.species);
        r.pod(pl.origin);
        r.pod(pl.age);
        r.pod(pl.effectiveRootVigorMax);
        r.pod(flowering);
        r.pod(senescing);
        pl.flowering = flowering != 0;
        pl.senescing = senescing != 0;
        uint64_t moduleCount = 0;
        if (!r.count(moduleCount, 1)) return fail("truncated");
        pl.modules.resize(static_cast<size_t>(moduleCount));
        for (size_t i = 0; i < pl.modules.size(); ++i) {
            auto& m = pl.modules[i];
            uint32_t proto = UINT32_MAX;
            uint8_t mainChild = 0;
            r.pod(proto);
            r.pod(m.age);
            r.pod(m.vigor);
            r.pod(m.light);
            r.pod(m.lightExposure);
            r.pod(m.subtreeLight);
            r.pod(m.bboxCenter);
            r.pod(m.bboxRadius);
            r.pod(m.axisTip);
            r.pod(m.worldPos);
            r.pod(m.diameter);
            r.pod(m.orientation);
            r.pod(m.parent);
            r.pod(m.parentAttachTerminal);
            r.vec(m.nodePositions);
            r.pod(mainChild);
            if (!r.ok) return fail("truncated");
            m.isMainChild = mainChild != 0;
            if (proto != UINT32_MAX) {
                if (proto >= w.prototypes.size()) return fail("a module's prototype is out of range");
                m.prototype = &w.prototypes[proto];
            }
            if (m.parent != UINT32_MAX && m.parent >= i) return fail("a module's parent does not precede it");
        }
    }
    r.pod(w.rngState);
    r.pod(w.simTime);
    if (!r.ok) return fail("truncated");
    if (r.n != 0) return fail("trailing bytes");
    // The instances point into w.prototypes; a vector move keeps its buffer.
    out = std::move(w);
    return true;
}

GrowResult growCached(WorldState& world, uint32_t ticks, float dt, const std::string& cacheDir) {
    GrowResult res;
    auto grow = [&] {
        for (uint32_t t = 0; t < ticks; ++t) step(world, dt);
    };
    if (cacheDir.empty()) {
        grow();
        return res;
    }

    std::string err;
    const std::vector<uint8_t> before = writeWorld(world, &err);
    if (before.empty()) {
        res.error = err;
        grow();
        return res;
    }
    const std::string tag = std::string("broflora-grow/1 ") + simHash() + " " + buildTag();
    uint64_t key = fnv64(kFnvBasis, tag.data(), tag.size());
    key = fnv64(key, before.data(), before.size());
    key = fnv64(key, &ticks, sizeof(ticks));
    key = fnv64(key, &dt, sizeof(dt));
    res.key = key;

    namespace fs = std::filesystem;
    char name[40];
    std::snprintf(name, sizeof(name), "broflora-%016llx.world", static_cast<unsigned long long>(key));
    const fs::path path = fs::path(cacheDir) / name;

    // File: key, payload size, payload (writeWorld bytes), FNV of the payload.
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            uint64_t fileKey = 0, len = 0, sum = 0;
            in.read(reinterpret_cast<char*>(&fileKey), sizeof(fileKey));
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::vector<uint8_t> payload;
            if (in && fileKey == key && len < (uint64_t(1) << 40)) {
                payload.resize(static_cast<size_t>(len));
                in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(len));
                in.read(reinterpret_cast<char*>(&sum), sizeof(sum));
            }
            if (in && fileKey == key && fnv64(kFnvBasis, payload.data(), payload.size()) == sum &&
                readWorld(payload.data(), payload.size(), world, &err)) {
                res.cached = true;
                return res;
            }
            res.error = err.empty() ? "growCached: the cached world is damaged; growing" : err;
        }
    }

    grow();

    const std::vector<uint8_t> after = writeWorld(world, &err);
    if (after.empty()) {
        res.error = err;
        return res;
    }
    std::error_code ec;
    fs::create_directories(cacheDir, ec);
    const auto salt = std::chrono::steady_clock::now().time_since_epoch().count() ^
                      static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    fs::path tmp = path;
    tmp += ".tmp" + std::to_string(static_cast<unsigned long long>(salt));
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        const uint64_t len = after.size();
        const uint64_t sum = fnv64(kFnvBasis, after.data(), after.size());
        o.write(reinterpret_cast<const char*>(&key), sizeof(key));
        o.write(reinterpret_cast<const char*>(&len), sizeof(len));
        o.write(reinterpret_cast<const char*>(after.data()), static_cast<std::streamsize>(after.size()));
        o.write(reinterpret_cast<const char*>(&sum), sizeof(sum));
        if (!o) {
            res.error = "growCached: could not write " + tmp.string();
            o.close();
            fs::remove(tmp, ec);
            return res;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        res.error = "growCached: could not place " + path.string() + ": " + ec.message();
        fs::remove(tmp, ec);
    }
    return res;
}

}  // namespace broflora
