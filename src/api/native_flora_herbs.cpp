// bro.flora's herbs (broflora/herbs.h): the presets by name, a preset as a
// plain object, and the species table bro.scene's grass node draws them from.

#include "native_flora_internal.h"

#include "broflora/herbs.h"

namespace broflora::api {

namespace {

const char* const kHabits[] = {"rosette", "leafy", "carpet"};
const char* const kLeaves[] = {"frond", "simple", "strap", "trifoliate", "lobed", "ribbed"};
const char* const kBlooms[] = {"none", "ray", "cup", "head", "spike", "umbel", "bell"};

// An enum's value from its name or its number; false for anything else.
template <size_t N>
bool enumOf(Value v, const char* const (&names)[N], uint32_t& out) {
    if (ev::isNumber(v)) {
        const double d = ev::toDouble(v);
        if (d >= 0.0 && d < static_cast<double>(N) && d == std::floor(d)) {
            out = static_cast<uint32_t>(d);
            return true;
        }
        return false;
    }
    if (!ev::isString(v)) return false;
    const std::string s = ev::toUtf8(v);
    for (size_t i = 0; i < N; ++i)
        if (s == names[i]) {
            out = static_cast<uint32_t>(i);
            return true;
        }
    return false;
}

struct FloatField { const char* name; float broflora::HerbSpecies::*member; };
struct CountField { const char* name; uint32_t broflora::HerbSpecies::*member; };
struct ColorField { const char* name; bromath::Vec3 broflora::HerbSpecies::*member; };

const FloatField kFloats[] = {
    {"height", &broflora::HerbSpecies::height},       {"spread", &broflora::HerbSpecies::spread},
    {"leafLength", &broflora::HerbSpecies::leafLength}, {"leafWidth", &broflora::HerbSpecies::leafWidth},
    {"leafTip", &broflora::HerbSpecies::leafTip},     {"toothDepth", &broflora::HerbSpecies::toothDepth},
    {"lobes", &broflora::HerbSpecies::lobes},         {"stalk", &broflora::HerbSpecies::stalk},
    {"rise", &broflora::HerbSpecies::rise},           {"arch", &broflora::HerbSpecies::arch},
    {"gloss", &broflora::HerbSpecies::gloss},         {"stemDiameter", &broflora::HerbSpecies::stemDiameter},
    {"ribDiameter", &broflora::HerbSpecies::ribDiameter}, {"bloomSize", &broflora::HerbSpecies::bloomSize},
    {"nod", &broflora::HerbSpecies::nod},             {"shade", &broflora::HerbSpecies::shade},
    {"shadeRange", &broflora::HerbSpecies::shadeRange}, {"abundance", &broflora::HerbSpecies::abundance},
    {"patch", &broflora::HerbSpecies::patch},         {"patchCover", &broflora::HerbSpecies::patchCover},
    {"spacing", &broflora::HerbSpecies::spacing},
};
const CountField kCounts[] = {
    {"leaves", &broflora::HerbSpecies::leaves},   {"stems", &broflora::HerbSpecies::stems},
    {"teeth", &broflora::HerbSpecies::teeth},     {"pinnae", &broflora::HerbSpecies::pinnae},
    {"bloomCount", &broflora::HerbSpecies::bloomCount}, {"petals", &broflora::HerbSpecies::petals},
};
const ColorField kColors[] = {
    {"leafColor", &broflora::HerbSpecies::leafColor},   {"stemColor", &broflora::HerbSpecies::stemColor},
    {"bloomColor", &broflora::HerbSpecies::bloomColor}, {"centreColor", &broflora::HerbSpecies::centreColor},
};

// `spec`'s fields over `h` (unknown or ill-typed fields are left alone).
void applyHerb(Value spec, broflora::HerbSpecies& h) {
    if (!ev::isObject(spec)) return;
    Rooted o(spec);
    for (const FloatField& f : kFloats) readFloatField(o, f.name, h.*f.member);
    for (const CountField& f : kCounts) {
        float v = 0.0f;
        if (readFloatField(o, f.name, v) && v >= 0.0f) h.*f.member = static_cast<uint32_t>(std::lround(v));
    }
    for (const ColorField& f : kColors) readVec3Prop(o, f.name, h.*f.member);
    uint32_t e = 0;
    if (enumOf(ev::getProperty(o, "habit"), kHabits, e)) h.habit = static_cast<broflora::HerbHabit>(e);
    if (enumOf(ev::getProperty(o, "leafShape"), kLeaves, e)) h.leafShape = static_cast<broflora::HerbLeaf>(e);
    if (enumOf(ev::getProperty(o, "bloomForm"), kBlooms, e)) h.bloomForm = static_cast<broflora::HerbBloom>(e);
    Value n = ev::getProperty(o, "name");
    if (ev::isString(n)) h.name = ev::toUtf8(n);
}

// A species from a name, or from {preset | name, ...fields}: the preset (if
// it names one) with the fields over it, its sway worked out again. False
// when it names no preset and is no object.
bool herbOf(Value v, broflora::HerbSpecies& out) {
    if (ev::isString(v)) {
        auto p = broflora::herbPreset(ev::toUtf8(v));
        if (!p) return false;
        out = *p;
        return true;
    }
    if (!ev::isObject(v)) return false;
    Rooted o(v);
    Value base = ev::getProperty(o, "preset");
    if (!ev::isString(base)) base = ev::getProperty(o, "name");
    if (ev::isString(base))
        if (auto p = broflora::herbPreset(ev::toUtf8(base))) out = *p;
    applyHerb(o, out);
    broflora::herbSway(out);
    return true;
}

Value herbObject(const broflora::HerbSpecies& h) {
    ev::Persistent o(ev::createObject());
    Value n = ev::fromUtf8(h.name);
    o.set(ev::setProperty(o.get(), "name", n));
    Value s = ev::fromUtf8(kHabits[static_cast<uint32_t>(h.habit) % broflora::kHerbHabitCount]);
    o.set(ev::setProperty(o.get(), "habit", s));
    s = ev::fromUtf8(kLeaves[static_cast<uint32_t>(h.leafShape) % broflora::kHerbLeafCount]);
    o.set(ev::setProperty(o.get(), "leafShape", s));
    s = ev::fromUtf8(kBlooms[static_cast<uint32_t>(h.bloomForm) % broflora::kHerbBloomCount]);
    o.set(ev::setProperty(o.get(), "bloomForm", s));
    for (const FloatField& f : kFloats)
        o.set(ev::setProperty(o.get(), f.name, ev::fromDouble(static_cast<double>(h.*f.member))));
    for (const CountField& f : kCounts)
        o.set(ev::setProperty(o.get(), f.name, ev::fromDouble(static_cast<double>(h.*f.member))));
    for (const ColorField& f : kColors) {
        Value c = makeVec3(h.*f.member);
        o.set(ev::setProperty(o.get(), f.name, c));
    }
    const auto cant = [&](const char* name, const broflora::Cantilever& c) {
        ev::Persistent co(ev::createObject());
        co.set(ev::setProperty(co.get(), "freq", ev::fromDouble(c.freq)));
        co.set(ev::setProperty(co.get(), "sway", ev::fromDouble(c.sway)));
        co.set(ev::setProperty(co.get(), "length", ev::fromDouble(c.length)));
        o.set(ev::setProperty(o.get(), name, co.get()));
    };
    cant("stemSway", h.stemSway);
    cant("leafSway", h.leafSway);
    float bs = 0.0f, cover = 0.0f;
    Value mean = makeVec3(broflora::herbMeanColor(h, &bs, &cover));
    o.set(ev::setProperty(o.get(), "meanColor", mean));
    o.set(ev::setProperty(o.get(), "bloomShare", ev::fromDouble(bs)));
    o.set(ev::setProperty(o.get(), "cover", ev::fromDouble(cover)));
    return o.get();
}

} // namespace

// herbPresets() -> the herb preset names.
Value jsHerbPresets(Value /*thisVal*/, std::span<const Value> /*args*/) {
    const auto& names = broflora::herbPresetNames();
    return hostArrayOf(names.size(), [&](size_t i) { return ev::fromUtf8(names[i]); });
}

// herbPreset(nameOrSpec) -> the species as a plain object (its sway and its
// mean colour from above worked out), or null.
Value jsHerbPreset(Value /*thisVal*/, std::span<const Value> args) {
    if (args.empty()) return ev::null();
    broflora::HerbSpecies h;
    if (!herbOf(args[0], h)) return ev::null();
    return herbObject(h);
}

// herbRecords(list) -> Float32Array, kHerbRecordFloats a species (names or
// specs; an entry naming no preset and no object is skipped).
Value jsHerbRecords(Value /*thisVal*/, std::span<const Value> args) {
    std::vector<broflora::HerbSpecies> hs;
    if (!args.empty() && ev::isObject(args[0])) {
        Rooted list(args[0]);
        Value lenV = ev::getProperty(list, "length");
        const size_t n = ev::isNumber(lenV) ? static_cast<size_t>(std::max(0.0, ev::toDouble(lenV))) : 0;
        for (size_t i = 0; i < n && i < 64; ++i) {
            broflora::HerbSpecies h;
            if (herbOf(ev::getElement(list, static_cast<uint32_t>(i)), h)) hs.push_back(std::move(h));
        }
    }
    const std::vector<float> rec = broflora::herbRecords(hs);
    return makeFloat32Array(rec.data(), rec.size());
}

} // namespace broflora::api
