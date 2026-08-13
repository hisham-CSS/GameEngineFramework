// The character-file loader.
//
// This is the first C++ in the repository that has ever opened a character file.
// Everything it does is shaped by three facts.
//
// 1. AUTHORED CONTENT IS UNTRUSTED (docs/MAINTENANCE.md). The path goes through
//    MyCoreEngine::PathIsContained before the file is opened, the file has a
//    size cap, the JSON is parsed with exceptions turned OFF, and every single
//    field is type-checked before it is read. There is no throw and no exit in
//    this file: a bad character is a `false` return and a message, because a
//    character that fails to load must not be able to take a match down.
//
// 2. THE ADR-001 LOAD ASSERTIONS RUN HERE, because load time is where they were
//    always meant to run -- "require decay.floor <= min(hitstun) asserted at
//    load", ADR-001 section 8 item 3. They are not decoration. A01 in particular
//    caught a real, shipped mistake: this project's own draft house rule (linear,
//    step 2, floor 10) FABRICATED AN INFINITE COMBO on Kung Fu Girl, because both
//    the C++ and the Python implementations compute max(floor, base - step*n) and
//    a floor above a move's authored hitstun RAISES it. Every rule below cites
//    what it protects.
//
// 3. NO FLOAT SURVIVES THE LOAD. The authored files carry damage, reach,
//    pushback, walk speed and the scaling table as JSON floats. They are
//    converted here, once, and every field CharacterData exposes is an integer,
//    because ARCHITECTURE.md D2 says the thing a tick reads is integers or the
//    crossplay guarantee is worth nothing. Where a file ships a pre-quantized
//    integer alongside the float -- engine.quantized_sources -- the INTEGER WINS,
//    since it is the number the author actually derived. Kung Fu Man is the case
//    that proves the point: its walk speed of 2.4 px/tick is exactly 614
//    sub-units and inexactly 0.024 reach units.
#include "cse/data/CharacterData.h"

// PathSandbox.h lives in Engine/src/core but pulls in nothing but Core.h,
// <filesystem> and <string>, so CseData compiles PathSandbox.cpp directly rather
// than linking the Engine -- the same arrangement tests/CMakeLists.txt already
// uses for Editor/src/UndoHistory.cpp. Linking the whole Engine here would give
// character data a dependency on GL, Jolt and Lua, which is precisely the
// direction ADR-002 CHOICE D spends a CMake assertion to prevent for the kernel.
#include "PathSandbox.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace cse::data {
namespace {

using json = nlohmann::json;

// --- Untrusted-JSON accessors -----------------------------------------------
//
// Every one of these is total: no exception, no abort, no undefined behaviour on
// a wrong type. nlohmann's operator[] and at() are not, which is why none of
// them appear below.

// Returns nullptr for absent AND for an explicit null. The files use null to
// mean "the source does not answer this" -- Kung Fu Man's projectiles author
// `reach: null` because a fireball's reach is a function of distance -- and for
// every such field "absent" and "authored null" mean the same thing here.
const json* member(const json& o, const char* key) {
    if (!o.is_object()) return nullptr;
    const auto it = o.find(key);
    if (it == o.end() || it->is_null()) return nullptr;
    return &(*it);
}

// Round half away from zero, done with a truncating cast rather than std::lround
// or std::round. Deliberate: ADR-001 section 3 records that C++ lround rounds
// halves away from zero while Python's round() is banker's, and a loader whose
// rounding rule is "whatever the standard library does" is a divergence waiting
// for a platform. This rule is written down and it is the same everywhere.
std::int64_t quantize(double v, std::int64_t scale) {
    const double s = v * static_cast<double>(scale);
    if (s >= 0.0) return static_cast<std::int64_t>(s + 0.5);
    return static_cast<std::int64_t>(s - 0.5);
}

bool inInt32(std::int64_t v) {
    return v >= -2147483647ll - 1ll && v <= 2147483647ll;
}

// True if the value is a number whose magnitude fits an int32 and which has no
// fractional part. 23.0 is accepted as 23: JSON Schema considers it an integer
// and the shipped files do author some integral quantities as floats.
bool asInt32(const json& v, std::int32_t& out) {
    if (!v.is_number()) return false;
    // Unsigned first: nlohmann's is_number_integer() is true for both signs, and
    // pulling an int64 out of a value above its range is not something to find
    // out about later.
    if (v.is_number_unsigned()) {
        const std::uint64_t u = v.get<std::uint64_t>();
        if (u > 2147483647ull) return false;
        out = static_cast<std::int32_t>(u);
        return true;
    }
    if (v.is_number_integer()) {
        const std::int64_t i = v.get<std::int64_t>();
        if (!inInt32(i)) return false;
        out = static_cast<std::int32_t>(i);
        return true;
    }
    const double d = v.get<double>();
    if (!(d > -2147483649.0 && d < 2147483649.0)) return false;   // also rejects NaN
    const std::int64_t i = static_cast<std::int64_t>(d);
    if (static_cast<double>(i) != d) return false;
    if (!inInt32(i)) return false;
    out = static_cast<std::int32_t>(i);
    return true;
}

// The last path component with its extension removed, used as CharacterData::id.
// Hand-rolled rather than std::filesystem::path::stem so that the in-memory
// loader does not need a path type for what is only ever a label.
std::string stemOf(const std::string& p) {
    std::size_t begin = 0;
    for (std::size_t i = 0; i < p.size(); ++i)
        if (p[i] == '/' || p[i] == '\\') begin = i + 1;
    std::size_t end = p.size();
    for (std::size_t i = end; i > begin; --i)
        if (p[i - 1] == '.') { end = i - 1; break; }
    return p.substr(begin, end - begin);
}

std::string toString(std::int64_t v) { return std::to_string(v); }

// --- The load context -------------------------------------------------------
//
// Carries the file name so that every message names it, per the brief: a failed
// load must say which file, which move, and which rule.
struct Ctx {
    std::string       source;
    LoadReport*       report = nullptr;
    const LoadOptions* opt   = nullptr;

    // Records the failure and returns false, so every call site reads
    // `return ctx.fail(...)` and cannot forget to stop.
    bool fail(const std::string& where, const std::string& what) {
        report->error = source + ": " + where + ": " + what;
        return false;
    }
    bool failRule(const char* rule, const std::string& what) {
        report->rule  = rule;
        report->error = source + ": " + rule + ": " + what;
        return false;
    }
    void warn(const std::string& what) {
        report->warnings.push_back(source + ": " + what);
    }
};

// --- Field readers ----------------------------------------------------------

bool readInt(Ctx& ctx, const json& o, const char* key, const std::string& where,
             std::int32_t& out, bool required) {
    const json* v = member(o, key);
    if (!v) {
        if (!required) return true;
        return ctx.fail(where, std::string("missing required integer field `") + key + "`");
    }
    if (!asInt32(*v, out))
        return ctx.fail(where, std::string("field `") + key +
                               "` is not an integer that fits 32 bits");
    return true;
}

bool readString(Ctx& ctx, const json& o, const char* key, const std::string& where,
                std::string& out, bool required) {
    const json* v = member(o, key);
    if (!v) {
        if (!required) return true;
        return ctx.fail(where, std::string("missing required string field `") + key + "`");
    }
    if (!v->is_string())
        return ctx.fail(where, std::string("field `") + key + "` is not a string");
    out = v->get<std::string>();
    return true;
}

bool readBool(Ctx& ctx, const json& o, const char* key, const std::string& where,
              bool& out) {
    const json* v = member(o, key);
    if (!v) return true;
    if (!v->is_boolean())
        return ctx.fail(where, std::string("field `") + key + "` is not a boolean");
    out = v->get<bool>();
    return true;
}

// Reads a float field and quantizes it, preferring an authored integer from
// engine.quantized_sources when one exists for this key. `preQuantized` is that
// integer or nullptr; `scale` converts the float to the same unit.
bool readQuantized(Ctx& ctx, const json& o, const char* key, const std::string& where,
                   const json* preQuantized, std::int64_t scale, std::int32_t& out) {
    if (preQuantized) {
        if (!asInt32(*preQuantized, out))
            return ctx.fail(where, std::string("engine.quantized_sources entry for `") + key +
                                   "` is not an integer");
        return true;
    }
    const json* v = member(o, key);
    if (!v) return true;                                   // leave the caller's default
    if (!v->is_number())
        return ctx.fail(where, std::string("field `") + key + "` is not a number");
    const double d = v->get<double>();
    const double scaled = d * static_cast<double>(scale);
    if (!(scaled > -2147483649.0 && scaled < 2147483649.0))
        return ctx.fail(where, std::string("field `") + key + "` is out of range");
    const std::int64_t q = quantize(d, scale);
    if (!inInt32(q))
        return ctx.fail(where, std::string("field `") + key + "` is out of range");
    // A float that does not land on an integer after scaling is a real loss, and
    // the direction of a loss is the kind of thing ADR-001 spends a section on.
    // It is a warning rather than an error because the corpus contains one
    // legitimate case (Kung Fu Man's 2.4 px/tick walk) and refusing it would
    // reject a shipped character.
    const double diff = scaled - static_cast<double>(q);
    if (diff > 1e-6 || diff < -1e-6)
        ctx.warn(where + ": `" + key + "` did not quantize exactly (" +
                 toString(q) + " from " + std::to_string(d) +
                 "); the stored value is rounded");
    out = static_cast<std::int32_t>(q);
    return true;
}

// --- Resource maps and assertion A04 ----------------------------------------

// A04. The rule is NOT "no map may author __space" -- that wording was in an
// earlier draft of the schema, it was authored from reasoning and never run, and
// executed as written it REJECTS ALL THREE SHIPPED CHARACTERS: kung_fu_girl and
// kung_fu_man author {__space: 0} on the microdash gap action and aof2 authors
// {__space: 170} on run_close. See schema.v2.json x-load-assertions.A04
// .scope_correction, which records the correction and the verification both ways.
//
// The real rule has two halves and they point in opposite directions:
//   * On a MOVE or a CANCEL, `__space` is forbidden, because the prover MERGES
//     rather than honours it -- model.py:476 does guard[SPACE] = max(authored,
//     need) and :479 does effect[SPACE] = authored - units(pushback) -- so an
//     authored number silently combines with a derived one and the file stops
//     saying what it appears to say.
//   * On a GAP ACTION it is REQUIRED to be authorable, because it is the only way
//     to express displacement (gap.py:54-56) and model.py:510 appends rather than
//     collides.
constexpr const char* kSpaceKey = "__space";

bool readResourceMap(Ctx& ctx, const json& owner, const char* key,
                     const std::string& where,
                     const std::vector<ResourceDef>& resources,
                     bool spaceAllowed,
                     std::vector<ResourceAmount>& out,
                     bool* outHasSpace, std::int32_t* outSpace,
                     std::int64_t spaceScale) {
    const json* m = member(owner, key);
    if (!m) return true;
    if (!m->is_object())
        return ctx.fail(where, std::string("field `") + key + "` is not an object");

    for (auto it = m->begin(); it != m->end(); ++it) {
        const std::string name = it.key();

        if (name == kSpaceKey) {
            if (!spaceAllowed)
                return ctx.failRule("A04",
                    where + ": `" + key + "` authors the reserved key `__space`. "
                    "The prover merges an authored __space with the value it derives from "
                    "reach and pushback (model.py:476, :479) instead of honouring it, so "
                    "the file would no longer say what it appears to say. Only gap actions "
                    "may author it.");
            std::int32_t px = 0;
            if (!asInt32(*it, px))
                return ctx.fail(where, std::string("`") + key + ".__space` is not an integer");
            const std::int64_t sub = static_cast<std::int64_t>(px) * spaceScale;
            if (!inInt32(sub))
                return ctx.fail(where, std::string("`") + key + ".__space` is out of range");
            if (outHasSpace) *outHasSpace = true;
            if (outSpace)    *outSpace    = static_cast<std::int32_t>(sub);
            continue;
        }

        std::size_t index = resources.size();
        for (std::size_t i = 0; i < resources.size(); ++i)
            if (resources[i].name == name) { index = i; break; }
        if (index == resources.size())
            return ctx.fail(where, std::string("`") + key + "` names resource `" + name +
                                   "`, which the file does not declare. A resource that is "
                                   "not in the positional list has no index, so the value "
                                   "would be silently dropped");

        std::int32_t value = 0;
        if (!asInt32(*it, value))
            return ctx.fail(where, std::string("`") + key + "." + name + "` is not an integer");

        ResourceAmount a{};
        a.resource = static_cast<ResourceIndex>(index);
        a.value    = value;
        out.push_back(a);
    }

    // Sorted by index so that iteration order is the resource order, which is the
    // build-wide contract, and not the order the author happened to type.
    std::sort(out.begin(), out.end(),
              [](const ResourceAmount& a, const ResourceAmount& b) {
                  return a.resource < b.resource;
              });
    return true;
}

// --- The engine-only arrays -------------------------------------------------

bool readHits(Ctx& ctx, const json& engine, const std::string& where, Move& move) {
    const json* arr = member(engine, "hits");
    if (!arr) return true;
    if (!arr->is_array())
        return ctx.fail(where, "engine.hits is not an array");

    for (std::size_t i = 0; i < arr->size(); ++i) {
        const json& h = (*arr)[i];
        const std::string hw = where + ".engine.hits[" + toString(static_cast<std::int64_t>(i)) + "]";
        if (!h.is_object()) return ctx.fail(hw, "hit record is not an object");

        HitRecord r{};
        if (!readInt(ctx, h, "tick",             hw, r.tick,             true))  return false;
        if (!readInt(ctx, h, "hitstun_ticks",    hw, r.hitstunTicks,     true))  return false;
        if (!readInt(ctx, h, "anim_elem",        hw, r.animElem,         false)) return false;
        if (!readInt(ctx, h, "damage_hundredths",hw, r.damageHundredths, false)) return false;
        if (!readInt(ctx, h, "blockstun_ticks",  hw, r.blockstunTicks,   false)) return false;
        if (!readInt(ctx, h, "slide_ticks",      hw, r.slideTicks,       false)) return false;
        if (!readInt(ctx, h, "pushback_vel_sub", hw, r.pushbackVelSub,   false)) return false;
        if (!readInt(ctx, h, "meter_gain",       hw, r.meterGain,        false)) return false;
        if (!readInt(ctx, h, "reach_sub",        hw, r.reachSub,         false)) return false;

        // A record carrying a `condition` is an ALTERNATIVE profile, not a sequel.
        // The condition itself is kept as prose (see the note in the header).
        if (const json* cond = member(h, "condition")) {
            r.isAlternative = true;
            if (cond->is_string()) {
                r.conditionProse = cond->get<std::string>();
            } else if (const json* prose = member(*cond, "prose")) {
                if (prose->is_string()) r.conditionProse = prose->get<std::string>();
            }
        }

        // A07: ticks strictly increasing. Out-of-order hits would make "the first
        // hit" ambiguous, and every cancel delay in the corpus is measured from
        // first contact.
        if (!move.hits.empty() && r.tick <= move.hits.back().tick)
            return ctx.failRule("A07", hw + ": tick " + toString(r.tick) +
                                " does not increase on the previous hit's tick " +
                                toString(move.hits.back().tick));
        move.hits.push_back(r);
    }

    if (const json* proj = member(engine, "hits_projection")) {
        if (!proj->is_string()) return ctx.fail(where, "engine.hits_projection is not a string");
        const std::string s = proj->get<std::string>();
        if      (s == "single")              move.hitsProjection = HitsProjection::Single;
        else if (s == "first")               move.hitsProjection = HitsProjection::First;
        else if (s == "first_damage_summed") move.hitsProjection = HitsProjection::FirstDamageSummed;
        else return ctx.fail(where, "engine.hits_projection is `" + s +
                                    "`, which is not one of single / first / first_damage_summed");
    }

    if (!move.hits.empty()) {
        // A05. The [P] scalars must BE the first hit, not some other hit or an
        // average of them, because a cancel delay is measured from first contact
        // and `startup` is what that arithmetic subtracts.
        if (move.hits[0].tick != move.startup)
            return ctx.failRule("A05", where + ": hits[0].tick (" + toString(move.hits[0].tick) +
                                ") is not move.startup (" + toString(move.startup) + ")");
        if (move.hits[0].hitstunTicks != move.hitstun)
            return ctx.failRule("A05", where + ": hits[0].hitstun_ticks (" +
                                toString(move.hits[0].hitstunTicks) +
                                ") is not move.hitstun (" + toString(move.hitstun) + ")");

        // A06. Only UNCONDITIONAL records count as sequels. Kung Fu Girl's chop is
        // why: its second HitDef is registered only when the first WHIFFED, so the
        // two are mutually exclusive and the second is never "the hitstun the
        // combo leaves". A draft of this check without the exclusion fired on
        // chop, flagging a 30-vs-33 difference between two hits that can never
        // both land.
        std::size_t lastUnconditional = 0;
        std::size_t unconditionalCount = 0;
        for (std::size_t i = 0; i < move.hits.size(); ++i)
            if (!move.hits[i].isAlternative) { lastUnconditional = i; ++unconditionalCount; }
        if (unconditionalCount > 1 &&
            move.hits[lastUnconditional].hitstunTicks != move.hits[0].hitstunTicks) {
            const json* caveat = member(engine, "hits_projection_caveat");
            if (!caveat || !caveat->is_string() || caveat->get<std::string>().empty())
                return ctx.failRule("A06", where +
                    ": the last unconditional hit's hitstun (" +
                    toString(move.hits[lastUnconditional].hitstunTicks) +
                    ") differs from the first's (" + toString(move.hits[0].hitstunTicks) +
                    "), so the projection onto move.hitstun is an approximation and "
                    "engine.hits_projection_caveat must be present and must name the "
                    "direction of the error");
        }
    }
    return true;
}

bool readMotion(Ctx& ctx, const json& engine, const std::string& where,
                Move& move, std::int64_t /*subPerPixel*/) {
    const json* arr = member(engine, "motion");
    if (!arr) return true;
    if (!arr->is_array())
        return ctx.fail(where, "engine.motion is not an array");

    for (std::size_t i = 0; i < arr->size(); ++i) {
        const json& k = (*arr)[i];
        const std::string mw = where + ".engine.motion[" + toString(static_cast<std::int64_t>(i)) + "]";
        if (!k.is_object()) return ctx.fail(mw, "motion keyframe is not an object");

        MotionKey key{};
        // A08's second half: every velocity must be an INTEGER. The file authors
        // sub-units directly for exactly this reason -- ARCHITECTURE.md:47 makes
        // the authoritative core int32 sub-units, and a float here would be a
        // float in the simulation. asInt32 accepts 1126 and rejects 1126.4.
        if (!readInt(ctx, k, "tick",        mw, key.tick,       true))  return false;
        if (!readInt(ctx, k, "vel_x_sub",   mw, key.velXSub,    true))  return false;
        if (!readInt(ctx, k, "vel_y_sub",   mw, key.velYSub,    true))  return false;
        if (!readInt(ctx, k, "pos_add_x_sub", mw, key.posAddXSub, false)) return false;
        if (!readInt(ctx, k, "pos_add_y_sub", mw, key.posAddYSub, false)) return false;

        // A08's first half: strictly increasing ticks. Two keyframes on one tick
        // means the result depends on which the loader applied last.
        if (!move.motion.empty() && key.tick <= move.motion.back().tick)
            return ctx.failRule("A08", mw + ": tick " + toString(key.tick) +
                                " does not increase on the previous keyframe's tick " +
                                toString(move.motion.back().tick));
        move.motion.push_back(key);
    }
    return true;
}

// --- The whole document -----------------------------------------------------

bool parseDocument(Ctx& ctx, const json& doc, CharacterData& out) {
    if (!doc.is_object()) return ctx.fail("document", "top level is not a JSON object");

    if (!readString(ctx, doc, "name",  "document", out.name,  true))  return false;
    if (!readString(ctx, doc, "stage", "document", out.stage, false)) return false;

    // --- Units. Not an ADR-001 assertion, but a hard error anyway: silently
    // accepting a different sub-unit scale would mis-scale every distance in the
    // file, and the failure would look like a balance problem rather than a unit
    // problem. cse::kernel::kSubUnitsPerPixel is 256 and is not negotiable.
    std::int64_t subPerPixel   = 256;
    std::int64_t pxPerReach    = 100;
    std::int32_t ticksPerSec   = 60;
    const json* engineNs = member(doc, "engine");
    if (engineNs && !engineNs->is_object())
        return ctx.fail("document", "`engine` is present but is not an object");
    const json* units = engineNs ? member(*engineNs, "units") : nullptr;
    if (units) {
        std::int32_t v = 0;
        if (const json* s = member(*units, "sub_units_per_pixel")) {
            if (!asInt32(*s, v) || v != 256)
                return ctx.fail("engine.units",
                    "sub_units_per_pixel is not 256. cse::kernel::kSubUnitsPerPixel is 256 "
                    "and every distance in this loader is expressed in those units");
            subPerPixel = v;
        }
        if (const json* p = member(*units, "pixels_per_reach_unit")) {
            if (!asInt32(*p, v) || v <= 0)
                return ctx.fail("engine.units", "pixels_per_reach_unit is not a positive integer");
            pxPerReach = v;
        }
        if (const json* t = member(*units, "ticks_per_second")) {
            if (!asInt32(*t, v) || v != 60)
                return ctx.fail("engine.units",
                    "ticks_per_second is not 60. cse::kernel::kTicksPerSecond is 60 and every "
                    "duration in this file is counted in those ticks");
            ticksPerSec = v;
        }
    } else {
        ctx.warn("engine.units is absent; assuming 256 sub-units per pixel, "
                 "100 pixels per reach unit, 60 ticks per second");
    }
    (void)ticksPerSec;
    const std::int64_t reachScale = pxPerReach * subPerPixel;   // reach units -> sub-units

    const json* quant = engineNs ? member(*engineNs, "quantized_sources") : nullptr;
    if (quant && !quant->is_object())
        return ctx.fail("engine", "quantized_sources is present but is not an object");
    const json* reachPx    = quant ? member(*quant, "reach_px")    : nullptr;
    const json* pushbackPx = quant ? member(*quant, "pushback_px") : nullptr;
    const json* damageH    = quant ? member(*quant, "damage_hundredths") : nullptr;

    // --- walk_speed. Prefer the authored sub-unit integer when the file carries
    // one: Kung Fu Man's 2.4 px/tick is exactly 614 sub-units and is NOT exactly
    // 0.024 reach units, and ADR-001 section 6.3 records that this is the one
    // place "no loader ever rounds" is not achievable from the float alone.
    const json* walkSub = quant ? member(*quant, "walk_speed_sub_per_tick") : nullptr;
    if (!readQuantized(ctx, doc, "walk_speed", "document", walkSub, reachScale,
                       out.walkSpeedSub)) return false;

    // --- scaling ------------------------------------------------------------
    {
        const json* permille = quant ? member(*quant, "scaling_permille") : nullptr;
        const json* scaling  = member(doc, "scaling");
        if (permille && permille->is_array() && !permille->empty()) {
            for (const auto& e : *permille) {
                std::int32_t v = 0;
                if (!asInt32(e, v)) return ctx.fail("engine.quantized_sources",
                                                    "scaling_permille has a non-integer entry");
                out.scalingPermille.push_back(v);
            }
        } else if (scaling) {
            if (!scaling->is_array()) return ctx.fail("document", "`scaling` is not an array");
            for (const auto& e : *scaling) {
                if (!e.is_number()) return ctx.fail("document", "`scaling` has a non-numeric entry");
                // Range-checked BEFORE the multiply, not after: converting an
                // out-of-range double to an integer is undefined behaviour, and
                // "check the result" is exactly the check that never runs.
                const double d = e.get<double>();
                if (!(d > -2147483.0 && d < 2147483.0))
                    return ctx.fail("document", "`scaling` entry is out of range");
                out.scalingPermille.push_back(static_cast<std::int32_t>(quantize(d, 1000)));
            }
        }
    }

    // --- resources, and assertion A03 ---------------------------------------
    {
        const json* res = member(doc, "resources");
        if (!res || !res->is_array() || res->empty())
            return ctx.fail("document", "`resources` is missing, not an array, or empty. "
                                        "The prover keys resources positionally, so a file "
                                        "with no resource list has no resource vocabulary");
        for (std::size_t i = 0; i < res->size(); ++i) {
            const json& r = (*res)[i];
            const std::string rw = "resources[" + toString(static_cast<std::int64_t>(i)) + "]";
            if (!r.is_object()) return ctx.fail(rw, "resource is not an object");
            ResourceDef def{};
            if (!readString(ctx, r, "name", rw, def.name, true)) return false;
            if (!readInt(ctx, r, "initial", rw, def.initial, false)) return false;
            if (!readInt(ctx, r, "floor",   rw, def.floor,   false)) return false;
            if (const json* c = member(r, "ceiling")) {          // null means "no ceiling"
                if (!asInt32(*c, def.ceiling)) return ctx.fail(rw, "`ceiling` is not an integer");
                def.hasCeiling = true;
            }
            for (const auto& prior : out.resources)
                if (prior.name == def.name)
                    return ctx.fail(rw, "resource `" + def.name + "` is declared twice");
            out.resources.push_back(def);
        }
        if (out.resources.size() > 255)
            return ctx.fail("resources", "more than 255 resources; the resource handle is 8 bits");

        // A03 is a CROSS-FILE rule and no single file can check it, so the caller
        // supplies the build's order. What it protects: comboprover::Character
        // keys a ResourceVec by INDEX (comboprover.hpp:56, :128-129, :152), so a
        // file that declares juggle before meter compares juggle against meter
        // and never says a word. ADR-001 section 8 item 7 makes the order a
        // build-wide contract for exactly this reason -- which is why all three
        // Phase-0 characters declare `meter, juggle` even where one is inert.
        if (ctx.opt->expectedResources.empty()) {
            ctx.warn("A03 was not checked: the caller supplied no expected resource order, "
                     "so nothing verifies that index 0 means the same resource here as in "
                     "every other character in this build");
        } else {
            const auto& want = ctx.opt->expectedResources;
            bool same = want.size() == out.resources.size();
            for (std::size_t i = 0; same && i < want.size(); ++i)
                same = want[i] == out.resources[i].name;
            if (!same) {
                std::string got, exp;
                for (std::size_t i = 0; i < out.resources.size(); ++i)
                    got += (i ? ", " : "") + out.resources[i].name;
                for (std::size_t i = 0; i < want.size(); ++i)
                    exp += (i ? ", " : "") + want[i];
                return ctx.failRule("A03",
                    "resource order is [" + got + "] but this build declares [" + exp +
                    "]. Resources are keyed POSITIONALLY, so a mismatch silently compares "
                    "one resource against another");
            }
        }
    }

    // --- decay, and assertion A02 -------------------------------------------
    {
        const json* d = member(doc, "decay");
        if (d) {
            if (!d->is_object()) return ctx.fail("document", "`decay` is not an object");
            std::string kind = "none";
            if (!readString(ctx, *d, "kind", "decay", kind, false)) return false;

            // A02. Named explicitly rather than falling through to "unknown kind",
            // because the reason it is banned is a measurement and the message is
            // where that measurement is recorded: comboprover.hpp:82-88 computes
            // multiplicative decay by repeated float multiply then a truncating
            // cast, model.py:261-263 by base*ratio**n then int(), and an integer
            // kernel on scaleBy reproduces NEITHER. Forbidding the kind removes a
            // divergence class instead of bounding it (ADR-001 section 8 item 3).
            if (kind == "multiplicative")
                return ctx.failRule("A02",
                    "decay.kind is `multiplicative`. Both reference implementations compute it "
                    "as a chain of float multiplications followed by a truncation, which an "
                    "integer kernel cannot reproduce; for a decision that turns on whether "
                    "hitstun >= startup - advantage, a one-frame disagreement is the whole "
                    "difference between TERMINATING and INFINITE. Use `linear`, or `table` "
                    "with integer permille multipliers");

            if      (kind == "none")   out.decay.kind = DecayKind::None;
            else if (kind == "linear") out.decay.kind = DecayKind::Linear;
            else if (kind == "table")  out.decay.kind = DecayKind::Table;
            else return ctx.fail("decay", "kind is `" + kind +
                                          "`, which is not one of none / linear / table");

            if (!readInt(ctx, *d, "step",  "decay", out.decay.step,  false)) return false;
            if (!readInt(ctx, *d, "floor", "decay", out.decay.floor, false)) return false;

            const json* tablePermille = quant ? member(*quant, "decay_table_permille") : nullptr;
            if (tablePermille && tablePermille->is_array() && !tablePermille->empty()) {
                for (const auto& e : *tablePermille) {
                    std::int32_t v = 0;
                    if (!asInt32(e, v))
                        return ctx.fail("engine.quantized_sources",
                                        "decay_table_permille has a non-integer entry");
                    out.decay.tablePermille.push_back(v);
                }
            } else if (const json* t = member(*d, "table")) {
                if (!t->is_array()) return ctx.fail("decay", "`table` is not an array");
                for (const auto& e : *t) {
                    if (!e.is_number()) return ctx.fail("decay", "`table` has a non-numeric entry");
                    const double ratio = e.get<double>();
                    if (!(ratio > -2147483.0 && ratio < 2147483.0))
                        return ctx.fail("decay", "`table` entry is out of range");
                    out.decay.tablePermille.push_back(
                        static_cast<std::int32_t>(quantize(ratio, 1000)));
                }
            }
            if (out.decay.kind == DecayKind::Table && out.decay.tablePermille.empty())
                return ctx.fail("decay", "kind is `table` but the table is empty");
        }
    }

    // --- moves ---------------------------------------------------------------
    {
        const json* arr = member(doc, "moves");
        if (!arr || !arr->is_array() || arr->empty())
            return ctx.fail("document", "`moves` is missing, not an array, or empty");
        if (arr->size() >= static_cast<std::size_t>(kInvalidMove))
            return ctx.fail("moves", "more than 65534 moves; the move handle is 16 bits and "
                                     "0xFFFF is reserved for `not found`");

        for (std::size_t i = 0; i < arr->size(); ++i) {
            const json& m = (*arr)[i];
            if (!m.is_object())
                return ctx.fail("moves[" + toString(static_cast<std::int64_t>(i)) + "]",
                                "move is not an object");
            Move mv{};
            std::string id;
            if (!readString(ctx, m, "id", "moves[" + toString(static_cast<std::int64_t>(i)) + "]",
                            id, true)) return false;
            const std::string where = "move `" + id + "`";
            mv.id = id;

            for (const auto& prior : out.moves)
                if (prior.id == id) return ctx.fail(where, "a move with this id is declared twice");

            if (!readString(ctx, m, "label", where, mv.label, false)) return false;
            if (!readInt(ctx, m, "startup",  where, mv.startup,  true))  return false;
            if (!readInt(ctx, m, "active",   where, mv.active,   false)) return false;
            if (!readInt(ctx, m, "recovery", where, mv.recovery, false)) return false;
            if (!readInt(ctx, m, "hitstun",  where, mv.hitstun,  false)) return false;
            if (mv.startup < 0 || mv.active < 0 || mv.recovery < 0 || mv.hitstun < 0)
                return ctx.fail(where, "startup, active, recovery and hitstun must not be negative");

            if (const json* s = member(m, "stance")) {
                if (!s->is_string()) return ctx.fail(where, "`stance` is not a string");
                const std::string v = s->get<std::string>();
                if      (v == "any")    mv.stance = Stance::Any;
                else if (v == "ground") mv.stance = Stance::Ground;
                else if (v == "air")    mv.stance = Stance::Air;
                else return ctx.fail(where, "stance is `" + v +
                                            "`, which is not one of any / ground / air");
            }

            const json* qd = damageH    ? member(*damageH,    id.c_str()) : nullptr;
            const json* qr = reachPx    ? member(*reachPx,    id.c_str()) : nullptr;
            const json* qp = pushbackPx ? member(*pushbackPx, id.c_str()) : nullptr;
            if (!readQuantized(ctx, m, "damage",   where, qd, 100,         mv.damageHundredths))
                return false;
            // reach_px and pushback_px are in PIXELS, so a pre-quantized value
            // still needs the pixel-to-sub-unit scale applied; the float form goes
            // straight from reach units to sub-units.
            if (qr) {
                std::int32_t px = 0;
                if (!asInt32(*qr, px)) return ctx.fail(where, "quantized reach_px is not an integer");
                const std::int64_t sub = static_cast<std::int64_t>(px) * subPerPixel;
                if (!inInt32(sub)) return ctx.fail(where, "quantized reach_px is out of range");
                mv.reachSub = static_cast<std::int32_t>(sub);
            } else {
                std::int32_t r = kNoReach;
                if (!readQuantized(ctx, m, "reach", where, nullptr, reachScale, r)) return false;
                mv.reachSub = r;
            }
            if (qp) {
                std::int32_t px = 0;
                if (!asInt32(*qp, px)) return ctx.fail(where, "quantized pushback_px is not an integer");
                const std::int64_t sub = static_cast<std::int64_t>(px) * subPerPixel;
                if (!inInt32(sub)) return ctx.fail(where, "quantized pushback_px is out of range");
                mv.pushbackSub = static_cast<std::int32_t>(sub);
            } else {
                if (!readQuantized(ctx, m, "pushback", where, nullptr, reachScale, mv.pushbackSub))
                    return false;
            }

            // A04 applies here: `__space` is forbidden on a move.
            if (!readResourceMap(ctx, m, "effect", where, out.resources, false,
                                 mv.effect, nullptr, nullptr, subPerPixel)) return false;
            if (!readResourceMap(ctx, m, "guard",  where, out.resources, false,
                                 mv.guard,  nullptr, nullptr, subPerPixel)) return false;

            if (const json* e = member(m, "engine")) {
                if (!e->is_object()) return ctx.fail(where, "`engine` is not an object");
                if (!readInt(ctx, *e, "state_id", where, mv.stateId, false)) return false;
                if (!readInt(ctx, *e, "anim_id",  where, mv.animId,  false)) return false;
                if (!readInt(ctx, *e, "variant",  where, mv.variant, false)) return false;

                if (const json* h = member(*e, "escape_hatch")) {
                    if (!h->is_object()) return ctx.fail(where, "engine.escape_hatch is not an object");
                    if (!readBool(ctx, *h, "needed", where, mv.escapeHatchNeeded)) return false;
                    if (!readString(ctx, *h, "kind", where, mv.escapeHatchKind, false)) return false;
                } else {
                    // The Phase-0 fit gate is computed from this field and nothing
                    // else, so a move without it silently counts as a clean fit.
                    ctx.warn(where + ": engine.escape_hatch is absent; the move counts as "
                                     "needing no hatch, which is what the Phase-0 gate measures");
                }

                if (const json* w = member(*e, "cancel_window_ticks")) {
                    if (!w->is_array()) return ctx.fail(where, "engine.cancel_window_ticks is not an array");
                    // The EMPTY array is load-bearing: the AOF2 thug's moves have no
                    // cancel window at all, and a schema that cannot say "none"
                    // forces an author to invent a value. Anything other than 0 or 2
                    // entries is a real error.
                    if (w->size() == 2) {
                        if (!asInt32((*w)[0], mv.cancelWindowOpen) ||
                            !asInt32((*w)[1], mv.cancelWindowClose))
                            return ctx.fail(where, "engine.cancel_window_ticks has a non-integer entry");
                        if (mv.cancelWindowClose < mv.cancelWindowOpen)
                            return ctx.fail(where, "engine.cancel_window_ticks closes before it opens");
                        mv.hasCancelWindow = true;
                    } else if (!w->empty()) {
                        return ctx.fail(where, "engine.cancel_window_ticks has " +
                                               toString(static_cast<std::int64_t>(w->size())) +
                                               " entries; it must have 0 or 2");
                    }
                }

                // v1's free-prose spelling. kung_fu_girl.json ships it on 17
                // moves, so it is real data and not a hypothetical.
                if (const json* hc = member(*e, "hit_condition")) {
                    if (hc->is_string()) mv.hitConditionProse = hc->get<std::string>();
                }

                if (!readHits(ctx, *e, where, mv)) return false;
                if (!readMotion(ctx, *e, where, mv, subPerPixel)) return false;
            }

            // v2's move-level spelling, which the schema says wins where both
            // exist. It may be a bare string or a predicate object carrying a
            // `prose` sibling; the structured form is not decoded (see the note
            // in CharacterData.h).
            if (const json* hc = member(m, "hit_condition")) {
                if (hc->is_string()) {
                    mv.hitConditionProse = hc->get<std::string>();
                } else if (const json* prose = member(*hc, "prose")) {
                    if (prose->is_string()) mv.hitConditionProse = prose->get<std::string>();
                }
            }

            out.moves.push_back(std::move(mv));
        }
    }

    // --- assertion A01, which needs the moves -------------------------------
    //
    // THE MOST VALUABLE ASSERTION IN THIS FILE, and the reason is that it has
    // already caught a shipped mistake. Both implementations compute linear decay
    // as max(floor, base - step*n) (model.py:260, comboprover.hpp:81), so a floor
    // ABOVE a move's base hitstun does not clamp it downward -- it RAISES it, and
    // invents frame advantage the game does not have. This project's own first
    // draft house rule (linear, step 2, floor 10) does exactly that on two of the
    // three shipped characters: floor 10 exceeds Kung Fu Girl's stand_lp hitstun
    // of 9 and every AOF2 hitstun of 7, and on Kung Fu Girl midscreen it flipped
    // the verdict to INFINITE -- a fabricated infinite combo, produced by a
    // balance rule nobody had run. ADR-001 section 8 item 3.
    //
    // Minimised over moves that ACTUALLY DEAL HITSTUN. `hitstun` is optional in
    // the schema (only `id` and `startup` are required), and absent means 0, so
    // a dash, a taunt or any movement state would drag the minimum to zero and
    // fail this rule for every character with a nonzero floor. That is a false
    // positive in the direction that blocks a correct file, which is the worse
    // direction for a load-time assertion: it makes people delete the check.
    //
    // A move with no hitstun cannot have its hitstun raised by a decay floor, so
    // excluding it is not a loosening — it is the correct domain.
    {
        const Move* smallest = nullptr;
        for (const auto& m : out.moves) {
            if (m.hitstun <= 0) continue;
            if (!smallest || m.hitstun < smallest->hitstun) smallest = &m;
        }
        if (smallest && out.decay.floor > smallest->hitstun)
            return ctx.failRule("A01",
                "decay.floor is " + toString(out.decay.floor) +
                " but the smallest hitstun in the file is " + toString(smallest->hitstun) +
                ", on move `" + smallest->id + "`. Decay is computed as "
                "max(floor, base - step*n), so a floor above a move's authored hitstun RAISES "
                "it and invents frame advantage; this exact mistake fabricated an infinite "
                "combo in the project's own first draft");
    }

    out.RebuildIndices();

    // --- cancels -------------------------------------------------------------
    {
        const json* arr = member(doc, "cancels");
        if (arr) {
            if (!arr->is_array()) return ctx.fail("document", "`cancels` is not an array");
            for (std::size_t i = 0; i < arr->size(); ++i) {
                const json& c = (*arr)[i];
                const std::string idx = toString(static_cast<std::int64_t>(i));
                if (!c.is_object()) return ctx.fail("cancels[" + idx + "]", "cancel is not an object");

                std::string fromId, toId;
                if (!readString(ctx, c, "from", "cancels[" + idx + "]", fromId, true)) return false;
                if (!readString(ctx, c, "to",   "cancels[" + idx + "]", toId,   true)) return false;
                const std::string where = "cancel `" + fromId + "` -> `" + toId + "`";

                Cancel cn{};
                cn.from = out.FindMove(fromId);
                cn.to   = out.FindMove(toId);
                if (cn.from == kInvalidMove)
                    return ctx.fail(where, "`from` names move `" + fromId + "`, which the file "
                                           "does not declare");
                if (cn.to == kInvalidMove)
                    return ctx.fail(where, "`to` names move `" + toId + "`, which the file "
                                           "does not declare");

                if (const json* on = member(c, "on")) {
                    if (!on->is_string()) return ctx.fail(where, "`on` is not a string");
                    const std::string v = on->get<std::string>();
                    if      (v == "hit")    cn.on = Contact::Hit;
                    else if (v == "block")  cn.on = Contact::Block;
                    else if (v == "whiff")  cn.on = Contact::Whiff;
                    else if (v == "always") cn.on = Contact::Always;
                    else return ctx.fail(where, "`on` is `" + v +
                                                "`, which is not one of hit / block / whiff / always");
                }

                // `kind: "link"` is shorthand for "wait the source out": delay is
                // active + recovery - 1 of the SOURCE move. Absent both, the delay
                // is 0, which is the most permissive edge in the vocabulary -- so
                // it is warned about rather than assumed silently. ADR-001's
                // hardest single finding was that Kung Fu Girl's chain cancels are
                // NOT delay 0: they range from 2 to 20, and authoring them as 0
                // would have handed the character free frames on every edge.
                std::string kind;
                if (!readString(ctx, c, "kind", where, kind, false)) return false;
                if (!kind.empty() && kind != "link")
                    return ctx.fail(where, "`kind` is `" + kind + "`; the only shorthand is `link`");
                const json* delay = member(c, "delay");
                if (delay) {
                    // An authored delay wins over the shorthand, matching the
                    // reference loader: the shorthand is a default, not an override.
                    if (!asInt32(*delay, cn.delay)) return ctx.fail(where, "`delay` is not an integer");
                    if (cn.delay < 0) return ctx.fail(where, "`delay` is negative");
                } else if (kind == "link") {
                    const Move& src = out.moves[cn.from];
                    cn.delay = src.active + src.recovery - 1;
                    if (cn.delay < 0) cn.delay = 0;
                } else {
                    ctx.warn(where + ": no `delay` and no `kind`, so the delay is 0 -- the most "
                                     "permissive edge this vocabulary can express");
                }

                if (!readBool(ctx, c, "certain", where, cn.certain)) return false;
                if (!readString(ctx, c, "label",  where, cn.label,  false)) return false;
                if (!readString(ctx, c, "caveat", where, cn.caveat, false)) return false;

                // A04 applies here too: `__space` is forbidden on a cancel.
                if (!readResourceMap(ctx, c, "effect", where, out.resources, false,
                                     cn.effect, nullptr, nullptr, subPerPixel)) return false;
                if (!readResourceMap(ctx, c, "guard",  where, out.resources, false,
                                     cn.guard,  nullptr, nullptr, subPerPixel)) return false;

                if (const json* e = member(c, "engine")) {
                    if (!e->is_object()) return ctx.fail(where, "`engine` is not an object");
                    if (!readString(ctx, *e, "family", where, cn.family, false)) return false;
                    if (cn.family.empty() &&
                        !readString(ctx, *e, "rule", where, cn.family, false)) return false;
                    if (const json* cond = member(*e, "condition")) {
                        if (cond->is_string()) {
                            cn.conditionProse = cond->get<std::string>();
                        } else if (const json* prose = member(*cond, "prose")) {
                            if (prose->is_string()) cn.conditionProse = prose->get<std::string>();
                        }
                    }
                }

                out.cancels.push_back(std::move(cn));
            }
        }
        if (out.cancels.size() > 0xFFFFu)
            return ctx.fail("cancels", "more than 65535 cancels; the cancel handle is 16 bits");
    }

    // --- gap actions ---------------------------------------------------------
    {
        const json* arr = member(doc, "gap_actions");
        if (arr) {
            if (!arr->is_array()) return ctx.fail("document", "`gap_actions` is not an array");
            for (std::size_t i = 0; i < arr->size(); ++i) {
                const json& g = (*arr)[i];
                const std::string idx = toString(static_cast<std::int64_t>(i));
                if (!g.is_object()) return ctx.fail("gap_actions[" + idx + "]",
                                                    "gap action is not an object");
                GapAction ga{};
                if (!readString(ctx, g, "id", "gap_actions[" + idx + "]", ga.id, true)) return false;
                const std::string where = "gap_action `" + ga.id + "`";
                if (!readInt(ctx, g, "frames",   where, ga.frames,  true))  return false;
                if (!readInt(ctx, g, "max_uses", where, ga.maxUses, false)) return false;
                if (!readString(ctx, g, "label", where, ga.label,   false)) return false;
                if (ga.frames < 0) return ctx.fail(where, "`frames` is negative");

                // A04's exemption. Gap actions MAY author `__space` and every
                // shipped character does; it is the documented way to express
                // displacement (gap.py:54-56).
                if (!readResourceMap(ctx, g, "effect", where, out.resources, true,
                                     ga.effect, &ga.hasSpaceEffect, &ga.spaceEffectSub,
                                     subPerPixel)) return false;
                if (!readResourceMap(ctx, g, "guard",  where, out.resources, true,
                                     ga.guard,  &ga.hasSpaceGuard,  &ga.spaceGuardSub,
                                     subPerPixel)) return false;

                if (const json* e = member(g, "engine")) {
                    if (!e->is_object()) return ctx.fail(where, "`engine` is not an object");
                    if (!readBool(ctx, *e, "enabled", where, ga.enabled)) return false;
                }
                out.gapActions.push_back(std::move(ga));
            }
        }
    }

    // --- starters ------------------------------------------------------------
    {
        const json* arr = member(doc, "starters");
        if (arr) {
            if (!arr->is_array()) return ctx.fail("document", "`starters` is not an array");
            for (const auto& s : *arr) {
                if (!s.is_string()) return ctx.fail("starters", "entry is not a string");
                const std::string starterId = s.get<std::string>();
                const MoveIndex mi = out.FindMove(starterId);
                if (mi == kInvalidMove)
                    return ctx.fail("starters", "names move `" + starterId +
                                                "`, which the file does not declare");
                out.starters.push_back(mi);
            }
        }
    }

    out.RebuildIndices();
    return true;
}

} // namespace

// --- CharacterData ----------------------------------------------------------

void CharacterData::RebuildIndices() {
    moveIndexById.clear();
    moveIndexById.reserve(moves.size());
    for (std::size_t i = 0; i < moves.size(); ++i)
        moveIndexById.emplace_back(moves[i].id, static_cast<MoveIndex>(i));
    std::sort(moveIndexById.begin(), moveIndexById.end(),
              [](const std::pair<std::string, MoveIndex>& a,
                 const std::pair<std::string, MoveIndex>& b) { return a.first < b.first; });

    cancelsFrom.assign(moves.size(), {});
    for (std::size_t i = 0; i < cancels.size(); ++i) {
        const MoveIndex f = cancels[i].from;
        if (f < moves.size()) cancelsFrom[f].push_back(static_cast<CancelIndex>(i));
    }
}

MoveIndex CharacterData::FindMove(std::string_view moveId) const {
    const auto it = std::lower_bound(
        moveIndexById.begin(), moveIndexById.end(), moveId,
        [](const std::pair<std::string, MoveIndex>& p, std::string_view v) {
            return std::string_view(p.first) < v;
        });
    if (it == moveIndexById.end() || std::string_view(it->first) != moveId) return kInvalidMove;
    return it->second;
}

// --- Entry points -----------------------------------------------------------

bool LoadCharacterJson(const std::string& sourceName,
                       const std::string& jsonText,
                       const LoadOptions& options,
                       CharacterData& out,
                       LoadReport& report) {
    out = CharacterData{};
    report = LoadReport{};

    Ctx ctx;
    ctx.source = sourceName;
    ctx.report = &report;
    ctx.opt    = &options;

    // allow_exceptions = false. The whole point: a malformed character file
    // returns a discarded value instead of unwinding through a match-start path,
    // and the loader stays usable in a build with exceptions disabled.
    const json doc = json::parse(jsonText.begin(), jsonText.end(),
                                 /*callback*/ nullptr, /*allow_exceptions*/ false);
    if (doc.is_discarded()) {
        report.error = sourceName + ": document: not valid JSON";
        return false;
    }

    out.id = stemOf(sourceName);
    if (!parseDocument(ctx, doc, out)) {
        out = CharacterData{};   // never hand back a half-built character
        return false;
    }
    return true;
}

bool LoadCharacterFile(const std::string& baseDir,
                       const std::string& relPath,
                       const LoadOptions& options,
                       CharacterData& out,
                       LoadReport& report) {
    out = CharacterData{};
    report = LoadReport{};

    // Containment BEFORE the file is opened, every time. docs/MAINTENANCE.md:
    // "anything from scene content goes through PathIsContained before the file
    // is opened. Absolute paths and `..` are refused." A character file is
    // authored content with a path in it and there is no exception to that rule.
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(baseDir, relPath, full)) {
        report.error = relPath + ": path: refused, because it is absolute, carries a "
                                 "drive/UNC root, or contains a `..` component that would "
                                 "escape the character directory";
        return false;
    }

    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(full, ec);
    if (ec) {
        report.error = relPath + ": file: cannot be opened (" + ec.message() + ")";
        return false;
    }
    if (size > options.maxFileBytes) {
        report.error = relPath + ": file: " + toString(static_cast<std::int64_t>(size)) +
                       " bytes exceeds the " +
                       toString(static_cast<std::int64_t>(options.maxFileBytes)) +
                       "-byte cap on authored content";
        return false;
    }

    std::ifstream in(full, std::ios::binary);
    if (!in) {
        report.error = relPath + ": file: cannot be opened for reading";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        report.error = relPath + ": file: read failed";
        return false;
    }

    return LoadCharacterJson(relPath, text, options, out, report);
}

} // namespace cse::data
