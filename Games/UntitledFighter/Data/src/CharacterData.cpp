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

// THE KERNEL'S HEADER, FOR ONE CONSTANT, AND NOT ONE FUNCTION CALL.
//
// Assertion A15 has to reject a hurtbox override that falls outside the bound
// the simulation's box arithmetic is proved overflow-free against, and Combat.h
// says in as many words where that rejection belongs: BoxIsValid is "exposed for
// the data loader, which is where a bad box should be rejected -- the simulation
// itself is total and never needs to ask."
//
// CALLING BoxIsValid IS EXACTLY WHAT MUST NOT HAPPEN. It is an ordinary function
// defined in Combat.cpp, so calling it would put CseKernel on CseData's link
// line, and Games/UntitledFighter/Data/CMakeLists.txt ends in a FATAL_ERROR that
// fires when it does -- deliberately, because the dependency runs data -> a
// kernel-shaped POD and never the reverse. MatchBuilder.cpp hit this first and
// settled it: include the header for the STRUCTS AND THE CONSTEXPR CONSTANTS,
// write the predicate out again, and need no symbol at link time. Its own
// comment calls duplicating four comparisons "the cheaper half of that trade".
// This file does the same thing against the same kMaxBoxCoord, so the bound
// cannot drift from the one the kernel actually clamps to.
//
// WHY THE 256 BELOW IS STILL A LITERAL, since a reader will ask having got this
// far. kMaxBoxCoord is a bound IMPORTED from the kernel and must track it.
// sub_units_per_pixel is a value the FILE DECLARES and this loader CHECKS -- the
// error it raises is "sub_units_per_pixel is not 256", a statement about
// authored content rather than about the kernel's limits. They read alike and
// they are different jobs.
#include "cse/kernel/Combat.h"

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
// executed as written it REJECTS ALL THREE PHASE-0 CORPUS CHARACTERS (now in
// tests/fixtures/characters): kung_fu_girl and kung_fu_man author {__space: 0}
// on the microdash gap action and aof2 authors {__space: 170} on run_close.
// See schema.v2.json x-load-assertions.A04
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

// --- Block height -----------------------------------------------------------

// schema v3 move.blocked_as, and its accepted alternative spelling
// engine.blocked_as. Shared by both call sites so the two spellings cannot
// disagree about what "low" means, which is the failure mode two parsers have.
bool readBlockedAs(Ctx& ctx, const json& owner, const char* key,
                   const std::string& where, BlockHeight& out, bool& authored) {
    const json* v = member(owner, key);
    if (!v) return true;
    if (!v->is_string())
        return ctx.fail(where, std::string("`") + key + "` is not a string");
    const std::string s = v->get<std::string>();
    if      (s == "high") out = BlockHeight::High;
    else if (s == "mid")  out = BlockHeight::Mid;
    else if (s == "low")  out = BlockHeight::Low;
    else return ctx.fail(where, std::string("`") + key + "` is `" + s +
                                "`, which is not one of high / mid / low. The value names THE "
                                "BLOCK THAT STOPS THE MOVE -- a high block stops {high, mid} "
                                "and a low block stops {low, mid} -- and it is not the "
                                "resource minimum `guard`, which is a different field");
    authored = true;
    return true;
}

// --- Priority, and assertion A17 --------------------------------------------

// The authored range, which is the WIDTH of cse::kernel::MoveDef's spare 16 bits
// and not a taste. Written as literals rather than pulled from <limits>, for the
// reason inInt32 above spells its own bounds out: the number this file is
// checking against is a decision recorded here, not a property of the host.
constexpr std::int32_t kMinPriority = -32768;
constexpr std::int32_t kMaxPriority =  32767;

// A17. WHY THIS IS A REFUSAL AND NOT A CLAMP, which is the whole content of the
// rule. This field's destination is MoveDef, whose loaded arrays the connect
// handshake hashes, and narrowing a 32-bit authored value into that slot is not
// a saturation -- it is a SIGN FLIP AT THE BOUNDARY. 32768 truncated to 16 bits
// is -32768, so the move the file says beats everything becomes the move that
// loses to everything, silently, and only in a build that narrows. Clamping
// would be worse still: two peers whose loaders clamped differently would
// disagree about a hashed byte, and the failure would present as a rollback
// desync rather than as a data error, which is the most expensive possible place
// to find out about a number in a file.
//
// WHAT THIS CANNOT CATCH, and it is worth knowing before trusting it. MUGEN's
// HitDef priority is transcribed at engine.reaction.priority on a different
// scale whose default sits in the middle of its band, so a value copied across
// from there is in range, passes here, and means the opposite of what it meant.
// No assertion can separate two legal integers. The schema spends a paragraph on
// the scale precisely because a check cannot.
bool readPriority(Ctx& ctx, const json& owner, const std::string& where,
                  std::int16_t& out) {
    const json* v = member(owner, "priority");
    if (!v) return true;                          // absent -> 0, which is a trade
    std::int32_t p = 0;
    if (!asInt32(*v, p))
        return ctx.fail(where, "`priority` is not an integer that fits 32 bits");
    if (p < kMinPriority || p > kMaxPriority)
        return ctx.failRule("A17", where + ": `priority` is " + toString(p) +
                            ", outside the authored range " + toString(kMinPriority) +
                            ".." + toString(kMaxPriority) + ". The value ends in "
                            "cse::kernel::MoveDef, whose bytes the connect handshake "
                            "hashes, and narrowing it there is not a clamp but a SIGN "
                            "FLIP at the boundary: 32768 becomes -32768, so the move "
                            "that beats everything becomes the move that loses to "
                            "everything. Refused here rather than truncated there");
    out = static_cast<std::int16_t>(p);
    return true;
}

// --- Invincibility windows, and assertions A18, A19 and A20 -----------------

// The six tokens of schema v3 move.invincibility[].attack_kinds. A chain rather
// than a table for the same reason every other enum in this file is one: six
// comparisons are cheaper to read than a container, and the ONE thing that must
// never happen -- an unrecognised token quietly becoming a default -- is a
// missing else branch either way. The caller supplies that branch; see A19.
bool attackKindFromString(const std::string& s, AttackKind& out) {
    if      (s == "high")       out = AttackKind::High;
    else if (s == "mid")        out = AttackKind::Mid;
    else if (s == "low")        out = AttackKind::Low;
    else if (s == "aerial")     out = AttackKind::Aerial;
    else if (s == "throw")      out = AttackKind::Throw;
    else if (s == "projectile") out = AttackKind::Projectile;
    else return false;
    return true;
}

// The legal set, spelled once, for the error message. A19's message has to LIST
// the tokens rather than say "unknown", because the mutation that proves the
// rule is not a typo at all -- it is `air`, which is what move.stance calls the
// same idea one field up, and a reader who guessed it needs to be told the word
// rather than told they were wrong.
constexpr const char* kAttackKindList =
    "high / mid / low / aerial / throw / projectile";

// A18, A19 and A20 all live here, and the order of the checks is the order the
// data is read rather than the order of the ids.
//
// READ AFTER startup, active AND recovery, and A18 depends on that exactly as
// A14 does: the bound is computed from all three and means nothing until they
// are in `move`.
bool readInvincibility(Ctx& ctx, const json& m, const std::string& where, Move& move) {
    const json* arr = member(m, "invincibility");
    if (!arr) return true;                        // absent -> no invincibility
    if (!arr->is_array())
        return ctx.fail(where, "`invincibility` is not an array");

    // A20. AN EMPTY LIST MEANS WHAT AN ABSENT FIELD MEANS -- this move has no
    // invincibility -- and it warns rather than failing.
    //
    // The two do NOT differ, and that has to be argued rather than assumed,
    // because this schema has a strong habit of the empty array being
    // load-bearing: engine.cancel_window_ticks authors [] to say "no window",
    // and a schema that cannot say `none` forces an author to invent a value.
    // The difference is that `none` is ALREADY SAYABLE HERE BY SAYING NOTHING.
    // A move with no invincibility is the overwhelming majority and needs no
    // invented default, so there is no second meaning left for [] to carry.
    //
    // IT WARNS BECAUSE THERE IS EXACTLY ONE OTHER READING AND IT IS THE
    // DANGEROUS ONE. One level down, an absent or empty `attack_kinds` means
    // EVERY kind -- full invincibility -- so an author who has just read that
    // rule can reasonably write `invincibility: []` meaning "invincible to
    // everything, always". That file loads, reads as `invincible`, and produces
    // a move with no invincibility at all. The warning names both readings and
    // says which one the file got.
    //
    // A WARNING AND NOT AN ERROR because [] is uninformative rather than wrong,
    // and a tool that emits the key uniformly for every move must keep working.
    // Refusing it would make this schema harder to generate into than to write
    // by hand, which is backwards -- the A16 severity argument in miniature.
    if (arr->empty()) {
        ctx.warn(where + ": A20: `invincibility` is an empty list, which means exactly "
                 "what omitting the field means -- this move has NO invincibility. If "
                 "the intent was `invincible to everything`, that is one window with no "
                 "`attack_kinds`, e.g. [{\"from_tick\": 1, \"ticks\": 6}]: an empty list "
                 "INSIDE a window means every kind, while an empty list OF windows means "
                 "no windows at all");
        return true;
    }

    // A14's bound, computed A14's way, and deliberately the same one. The two
    // tick bases in this project disagree by one at the end of a move -- the
    // schema counts from 1 and the kernel from 0 -- and A14 takes the looser so
    // that a file correct under either reading passes. A18 takes the identical
    // bound, because two assertions on adjacent fields disagreeing by one frame
    // about where a move ENDS would be a worse defect than either being loose.
    const std::int64_t last = static_cast<std::int64_t>(move.startup) +
                              static_cast<std::int64_t>(move.active) +
                              static_cast<std::int64_t>(move.recovery);

    for (std::size_t i = 0; i < arr->size(); ++i) {
        const json& w = (*arr)[i];
        const std::string ww = where + ".invincibility[" +
                               toString(static_cast<std::int64_t>(i)) + "]";
        if (!w.is_object()) return ctx.fail(ww, "invincibility window is not an object");

        InvincibilityWindow win{};
        // Both required. A window missing `ticks` is not a one-tick window, it
        // is a window whose length nobody stated, and guessing 1 would author
        // the weakest possible reading of the strongest possible property.
        if (!readInt(ctx, w, "from_tick", ww, win.fromTick, true)) return false;
        if (!readInt(ctx, w, "ticks",     ww, win.ticks,     true)) return false;

        // A18, three clauses, each with its own message because they are three
        // different mistakes.
        //
        // DEGENERATE. A window of zero ticks is not a short window, it is the
        // empty set -- A15's argument for a zero-area hurtbox, one field over
        // and in time instead of space. It would sit in the file looking like
        // protection while providing none, and a reader's eye slides over it.
        if (win.ticks < 1)
            return ctx.failRule("A18", ww + ": `ticks` is " + toString(win.ticks) +
                                ". A window of no length is not a short window, it is "
                                "the empty set: it says the move is invulnerable and "
                                "protects it on no tick at all. This is A15's zero-area "
                                "box argument in time rather than space");
        // BEFORE THE MOVE. Tick 1 is the first tick of the state, so 0 and
        // negatives are not early, they are outside.
        if (win.fromTick < 1)
            return ctx.failRule("A18", ww + ": `from_tick` is " + toString(win.fromTick) +
                                ". Tick 1 is the first tick of the state, so this window "
                                "opens before the move exists");
        // AFTER THE MOVE. The whole job of the field is to answer "can this move
        // be hit on tick N" for the ticks the move occupies; past the end it is
        // answering about a move that is not running.
        const std::int64_t end = static_cast<std::int64_t>(win.fromTick) +
                                 static_cast<std::int64_t>(win.ticks) - 1;
        if (end > last)
            return ctx.failRule("A18", ww + ": the window covers ticks " +
                                toString(win.fromTick) + ".." + toString(end) +
                                ", but this move runs from tick 1 to tick " + toString(last) +
                                " (startup " + toString(move.startup) + " + active " +
                                toString(move.active) + " + recovery " +
                                toString(move.recovery) + "). A move cannot be "
                                "invulnerable after it has ended");

        // The kind set. ABSENT and EMPTY both mean EVERY KIND, deliberately and
        // without a diagnostic: this list only ever NARROWS the window, so the
        // identity element of "no narrowing" is "everything". The asymmetry with
        // A20's outer list is not an inconsistency -- the outer list ENUMERATES
        // windows, so having none is having no invincibility; the inner list
        // NARROWS one, so naming nothing narrows by nothing.
        //
        // Normalised to the full mask HERE rather than stored as "empty", so
        // that no reader downstream has to remember the special case. Same rule
        // this file applies to every other quantity: convert once, at load.
        win.kinds = kAllAttackKinds;
        if (const json* kinds = member(w, "attack_kinds")) {
            if (!kinds->is_array())
                return ctx.fail(ww, "`attack_kinds` is not an array");
            if (!kinds->empty()) {
                AttackKindMask mask = 0;
                for (const auto& t : *kinds) {
                    if (!t.is_string())
                        return ctx.fail(ww, "`attack_kinds` has a non-string entry");
                    const std::string s = t.get<std::string>();
                    AttackKind k{};
                    // A19. REFUSED, NEVER DROPPED, and the direction of the
                    // failure is why. This list narrows a window and an empty
                    // one means everything, so a token that fell out of it has
                    // two ways to go and both are silent and catastrophic in
                    // opposite directions: dropped, ["aerail"] becomes [] and
                    // the move is invulnerable to EVERYTHING; kept as an unknown
                    // kind, it is invulnerable to something no attack ever is,
                    // which is NOTHING -- while the file still says `invincible`
                    // and the designer still believes it. There is no benign
                    // reading, so there is no tolerant branch.
                    //
                    // The message LISTS the legal tokens rather than saying
                    // "unknown", because the mutation that proves this rule is
                    // not a typo: it is `air`, move.stance's word for the same
                    // idea one field up, and somebody who guessed it needs to be
                    // handed the right word.
                    if (!attackKindFromString(s, k))
                        return ctx.failRule("A19", ww + ": `attack_kinds` names `" + s +
                                            "`, which is not one of " + kAttackKindList +
                                            ". An unrecognised kind is refused rather "
                                            "than ignored: dropping it would leave an "
                                            "EMPTY list, which means invulnerable to "
                                            "EVERYTHING, and keeping it would mean "
                                            "invulnerable to something no attack ever is "
                                            "-- while the file goes on saying `invincible` "
                                            "either way. Note that `high`/`mid`/`low` are "
                                            "the incoming attack's blocked_as and `aerial` "
                                            "is its attacker's stance; `air` is stance's "
                                            "spelling and is NOT a token here");
                    mask = static_cast<AttackKindMask>(mask | AttackKindBit(k));
                }
                win.kinds = mask;
            }
        }

        // NOTHING CHECKS FOR OVERLAP, AND NOTHING SORTS THE LIST. Both are
        // deliberate and both were nearly written the other way.
        //
        // A07 requires hits[].tick strictly increasing and A08 requires the same
        // of motion[].tick, so requiring it here looks like consistency. It is
        // not: a hit record and a motion keyframe are APPLIED IN SEQUENCE and
        // the result depends on which came last, which is the entire content of
        // those two rules. An invincibility window contributes to a set UNION,
        // and a union is commutative and idempotent, so neither order nor
        // overlap can change what a move means. Copying A07's rule here would
        // have been a rule inherited from a neighbour's shape rather than
        // derived from this field's meaning -- which is exactly what A04's
        // scope_correction records going wrong once already.
        //
        // And overlap is not merely harmless, it is CORRECT AUTHORING: "a
        // reversal invincible 1-6 and then throw-invincible through its
        // recovery" is most naturally two windows that overlap on six ticks. A
        // diagnostic firing on the example a field was added for is how a
        // diagnostic gets deleted.
        //
        // The list is stored as authored rather than merged into disjoint
        // windows, because the file is what a designer reads and a loader that
        // turned two authored windows into three normalised ones would make the
        // data a program sees stop matching the document a human edits.
        move.invincibility.push_back(win);
    }
    return true;
}

// --- The hurtbox override, and assertion A15 --------------------------------

// A15. Combat.h::BoxIsValid states the rule; this is that rule written out,
// because Data may not link the kernel (see the include note at the top of this
// file). Each clause fails differently and all three failures are silent:
//
//   INVERTED (x1 <= x0). A half-open overlap test reports an inside-out
//     rectangle as empty, so the character would simply never be hit and nothing
//     would say why. Combat.h names this exact trap when it explains why
//     MirrorBox negates AND swaps -- "the half of the operation the multiply
//     forgets".
//   EMPTY (an equal pair). Total, permanent, unattributed invulnerability
//     authored as four innocent integers. The schema already has a field that
//     says WHICH attacks and for HOW LONG (engine.invuln[]), and saying it with
//     a degenerate rectangle hides the strongest property a move can have.
//   OUT OF BOUNDS. The worst of the three, because it is not caught downstream:
//     PlaceBox CLAMPS, so the box would silently change shape rather than fail.
//     MatchBuilder.cpp already refuses the body and the reach on these grounds.
//
// The Y-DOWN clause below only warns, and it is the one that earns its keep.
bool readHurtbox(Ctx& ctx, const json& engine, const std::string& where, Move& move) {
    const json* b = member(engine, "hurtbox_sub");
    if (!b) return true;
    if (!b->is_object())
        return ctx.fail(where, "engine.hurtbox_sub is not an object");

    const std::string hw = where + ".engine.hurtbox_sub";
    BoxSub box{};
    // All four required. A box missing an edge is not a smaller box, it is a box
    // with an edge silently at zero -- which for y1 is the empty case above.
    if (!readInt(ctx, *b, "x0", hw, box.x0, true)) return false;
    if (!readInt(ctx, *b, "y0", hw, box.y0, true)) return false;
    if (!readInt(ctx, *b, "x1", hw, box.x1, true)) return false;
    if (!readInt(ctx, *b, "y1", hw, box.y1, true)) return false;

    const std::int32_t bound = cse::kernel::kMaxBoxCoord;
    const std::int32_t coords[4] = { box.x0, box.y0, box.x1, box.y1 };
    for (std::int32_t c : coords) {
        if (c > bound || c < -bound)
            return ctx.failRule("A15", hw + ": coordinate " + toString(c) +
                                " is outside the kernel's box bound of +/-" + toString(bound) +
                                " sub-units. PlaceBox CLAMPS rather than refusing, so this "
                                "would silently change the shape of the body instead of "
                                "rejecting the file");
    }
    if (box.x1 <= box.x0 || box.y1 <= box.y0)
        return ctx.failRule("A15", hw + ": [" + toString(box.x0) + ", " + toString(box.y0) +
                            ", " + toString(box.x1) + ", " + toString(box.y1) +
                            "] is not a well-formed non-empty box. Boxes are HALF-OPEN "
                            "(x in [x0, x1), y in [y0, y1), cse/kernel/Combat.h), so x1 must "
                            "EXCEED x0 and y1 must EXCEED y0: an equal pair is not a thin box, "
                            "it is the empty set, and an empty body cannot be hit by anything. "
                            "Total invulnerability is a real thing to want and engine.invuln[] "
                            "is where it is said, with the attack classes and the tick count "
                            "that make it reviewable");

    // WARNS, NEVER REFUSES. +Y is UP in this struct, matching cse::kernel::Box,
    // and DOWN in engine.constants.default_pushbox_sub, which is MUGEN's
    // convention -- fighter_a.json ships [-3328, -15360, 3328, 0] for a body that
    // belongs here as [-3328, 0, 3328, 15360]. Both are well-formed rectangles of
    // identical size, so every clause above passes either one, and an author
    // reaching for a hurtbox will reach for the pushbox they already have.
    //
    // A warning rather than an error because y0 < 0 is only ALMOST certainly
    // wrong: an airborne move legitimately lifts its whole body off the floor,
    // and refusing a negative y0 would be a claim about every character anybody
    // will ever author. Warning is a claim about this one.
    if (box.y0 < 0)
        ctx.warn(hw + ": y0 is " + toString(box.y0) +
                 ", below the floor. +Y is UP here, matching cse::kernel::Box, while "
                 "engine.constants.default_pushbox_sub is MUGEN's Y-DOWN convention "
                 "(fighter_a.json ships [-3328, -15360, 3328, 0] for a body that belongs "
                 "here as [-3328, 0, 3328, 15360]). If this box was copied from a pushbox, "
                 "it is upside down and sixty pixels underground -- and it is well-formed, "
                 "so nothing else will catch it");

    move.hasHurtboxOverride = true;
    move.hurtboxOverride    = box;
    return true;
}

// --- Going airborne, and assertion A14 --------------------------------------

// A14. A move cannot take the character off the ground before it starts or after
// it has ended; the number's whole job is to answer "is the attacker on the floor
// on tick N" for the ticks this move occupies.
//
// THE UPPER BOUND IS THE LOOSER OF TWO TICK BASES, ON PURPOSE. This schema counts
// from 1 -- "tick 1 is the first tick of the state" -- and mugen_cns.py derives
// recovery as `total - startup - active + 1`, which puts the last tick at
// startup + active + recovery - 1. The kernel counts from 0: Combat.h makes the
// first tick frame 0 and MatchBuilder.cpp computes duration = startup + active +
// recovery. The two disagree by one at the end. This bound takes the larger, so a
// file correct under either reading passes.
//
// That is the deliberate direction. A14 exists to catch NONSENSE -- tick 0, a
// negative, tick 400 on a twenty-tick move -- and a load assertion that rejects a
// correct file over a one-frame convention dispute it does not own is precisely
// the mistake A04 was corrected for. Unifying the two bases is a separate job
// with a golden hash under it.
bool readAirborneFrom(Ctx& ctx, const json& engine, const std::string& where, Move& move) {
    const json* v = member(engine, "airborne_from_tick");   // absent OR null -> never
    if (!v) return true;
    std::int32_t tick = 0;
    if (!asInt32(*v, tick))
        return ctx.fail(where, "engine.airborne_from_tick is not an integer that fits 32 bits");

    const std::int64_t last = static_cast<std::int64_t>(move.startup) +
                              static_cast<std::int64_t>(move.active) +
                              static_cast<std::int64_t>(move.recovery);
    if (tick < 1 || static_cast<std::int64_t>(tick) > last)
        return ctx.failRule("A14", where + ": engine.airborne_from_tick is " + toString(tick) +
                            ", which is outside the move. This move runs from tick 1 to tick " +
                            toString(last) + " (startup " + toString(move.startup) + " + active " +
                            toString(move.active) + " + recovery " + toString(move.recovery) +
                            "), and a move cannot take the character off the ground before it "
                            "starts or after it has ended");

    move.airborneFromTick = tick;
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

    // engine.constants: the block that carries this character's own physical
    // numbers AS PROVENANCE -- cited, MUGEN-signed (Y-down), and mostly
    // unread. Only the crouch height is read from it (ROADMAP M1.3d). Jump
    // physics deliberately did NOT get loaded from here: `engine.movement`
    // (M1.3(b1), ADR-014) is the loadable block, in the kernel's own +Y-up
    // convention, precisely so no sign flip lives in a loader.
    if (const json* consts = engineNs ? member(*engineNs, "constants") : nullptr) {
        if (!consts->is_object())
            return ctx.fail("engine", "constants is present but is not an object");
        std::int32_t crouchPx = 0;
        if (!readInt(ctx, *consts, "crouch_height_px", "engine.constants", crouchPx, false))
            return false;
        if (crouchPx < 0)
            return ctx.fail("engine.constants",
                            "crouch_height_px is " + std::to_string(crouchPx) +
                            "; a body cannot have a negative height.");
        out.crouchHeightSub = crouchPx * cse::kernel::kSubUnitsPerPixel;
    }

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

    // --- engine.movement (ROADMAP M1.3(b1), ADR-014). Kernel semantics on
    // purpose -- +Y up, positive impulse, positive gravity magnitude -- so
    // the projection into FighterData is two assignments a reader checks by
    // eye, never a sign flip to get wrong (the Y-down trap lives in
    // engine.constants and stays there, cited and unread). Zero is the
    // kernel's unauthored sentinel (`!= 0` falls back to the placeholder),
    // so an EXPLICIT zero is refused by name: an author writing 0 means "no
    // jump" or "no gravity", and silently handing them the placeholder is a
    // worse accident than a load error.
    if (const json* mv = engineNs ? member(*engineNs, "movement") : nullptr) {
        if (!mv->is_object())
            return ctx.fail("engine", "`movement` is present but is not an object");
        struct Field { const char* key; std::int32_t* out; };
        const Field fields[] = {
            { "jump_impulse_sub", &out.jumpImpulseSub },
            { "gravity_sub",      &out.gravitySub },
        };
        for (const Field& fdef : fields) {
            const json* v = member(*mv, fdef.key);
            if (v == nullptr) continue;
            std::int32_t n = 0;
            if (!asInt32(*v, n) || n < 0)
                return ctx.fail("engine.movement",
                                std::string("`") + fdef.key +
                                "` must be a positive integer in sub-units, "
                                "+Y up (the kernel's convention, not "
                                "engine.constants' Y-down one)");
            if (n == 0)
                return ctx.fail("engine.movement",
                                std::string("`") + fdef.key +
                                "` is 0, which the kernel reads as UNAUTHORED "
                                "and replaces with its placeholder. Omit the "
                                "key to mean that; an authored zero would be "
                                "a silent lie.");
            *fdef.out = n;
        }
    }

    // --- input_buffer_frames (ROADMAP M1.1e). Character-global, integer ticks,
    // zero-or-absent means no buffering. BOUNDED AT 255 and refused past it,
    // never rounded: the kernel ages the buffer in a uint8, so a window of 256
    // wraps the age and the buffer becomes ETERNAL -- a press firing a move
    // minutes later -- which is a worse authoring accident than a load error.
    if (const json* buf = member(doc, "input_buffer_frames")) {
        std::int32_t v = 0;
        if (!asInt32(*buf, v) || v < 0)
            return ctx.fail("document",
                            "`input_buffer_frames` must be an integer >= 0");
        if (v > 255)
            return ctx.fail("document",
                            "`input_buffer_frames` is " + toString(v) +
                            "; the kernel ages the buffer in a uint8, so "
                            "windows past 255 wrap into an eternal buffer and "
                            "are refused rather than rounded");
        out.inputBufferFrames = v;
    }

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
                if      (v == "any")       mv.stance = Stance::Any;
                else if (v == "ground")    mv.stance = Stance::Ground;
                else if (v == "air")       mv.stance = Stance::Air;
                // NEW IN v3. `ground` above is kept and keeps its meaning --
                // GROUNDED, STANCE UNSPECIFIED -- because that is what a MUGEN
                // import honestly knows, and because collapsing it into
                // `standing` would re-label ten transcribed moves as standing
                // when six of them are crouching. See the note on Stance in the
                // header for why the enum's declaration order is what it is.
                else if (v == "standing")  mv.stance = Stance::Standing;
                else if (v == "crouching") mv.stance = Stance::Crouching;
                else return ctx.fail(where, "stance is `" + v +
                                            "`, which is not one of any / ground / standing / "
                                            "crouching / air. `ground` means GROUNDED, STANCE "
                                            "UNSPECIFIED and is what a MUGEN import transcribes "
                                            "to; a new character says standing or crouching");
            }

            // schema v3 move.blocked_as, and it is ORTHOGONAL to the stance
            // above however strongly the two correlate -- a crouching heavy
            // punch is commonly a MID anti-air. The header's note on BlockHeight
            // is the argument, and nothing here cross-checks the two fields,
            // deliberately: every such check encodes the word "usually" and
            // fires on a move somebody has shipped.
            bool blockedAsAuthored = false;
            if (!readBlockedAs(ctx, m, "blocked_as", where, mv.blockedAs, blockedAsAuthored))
                return false;

            // The other two v3 move-level fields. Both are read HERE rather than
            // beside the engine block, because both are move-level [G] fields
            // sitting with `stance` and `blocked_as` -- and because A18 bounds a
            // window against startup, active and recovery, which have to be in
            // `mv` before the comparison means anything. Same dependency A14 has
            // on the same three numbers, and the same reason the two engine
            // fields are read last.
            if (!readPriority(ctx, m, where, mv.priority)) return false;
            if (!readInvincibility(ctx, m, where, mv)) return false;

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

                // THE TWO MISPLACEMENTS, REFUSED BY NAME RATHER THAN IGNORED,
                // and this is a considered departure from what `blocked_as`
                // does six lines further down. That field ACCEPTS an engine
                // spelling, because both placements were defensible and
                // tolerating both was cheaper than arbitrating. These two do
                // not, because the engine namespace is ALREADY OCCUPIED BY BOTH
                // NAMES: engine.invuln[] is a list of frame windows about
                // invulnerability in MUGEN's letters, and engine.reaction
                // .priority is MUGEN's HitDef parameter, authored with a real
                // value by the AOF2 thug. Accepting a near-synonym beside
                // either would put two similar names at one nesting level
                // meaning different things in different vocabularies -- which
                // is the confusion `blocked_as` was NAMED to avoid against
                // `guard` one level up, and repeating it in the same revision
                // that made that argument would undo it.
                //
                // Ignoring them silently is the outcome being bought off: a key
                // that validates, loads, and gives a move an ordering or an
                // invulnerability it does not have. These are ORDINARY errors
                // with no rule id, on the same footing as "`engine` is not an
                // object" -- a misplaced key is not a false statement about a
                // move, and LoadReport::rule is documented as naming an
                // assertion rather than every refusal.
                //
                // `member` returns nullptr for an explicit null, so
                // `"priority": null` here is not refused. That is consistent
                // with what null means everywhere else in this loader -- "the
                // source does not answer this" -- and a null says nothing to be
                // wrong about.
                if (member(*e, "priority"))
                    return ctx.fail(where, "`engine.priority` is not a field. The move-level "
                                           "spelling is `priority`, a peer of `stance` and "
                                           "`blocked_as`. Note that engine.reaction.priority "
                                           "and engine.projectile.priority DO exist and are "
                                           "MUGEN transcriptions on MUGEN's scale, where the "
                                           "default sits mid-band -- so a value copied from "
                                           "either means the opposite of what it means here, "
                                           "and it must be re-derived rather than moved");
                if (member(*e, "invincibility"))
                    return ctx.fail(where, "`engine.invincibility` is not a field. The "
                                           "move-level spelling is `invincibility`. The "
                                           "engine namespace has `invuln[]`, which is a "
                                           "different field: it is the MUGEN transcription "
                                           "(NotHitBy/HitBy, S/C/A state types, NA/SP/HT "
                                           "attribute classes) and it stays. The move-level "
                                           "field is the designed rule, in this schema's own "
                                           "vocabulary of blocked_as heights plus `aerial`");

                // engine.reaction: what the hit DOES to the defender. Read as
                // a block because that is how it is authored, and optional
                // throughout -- a move that says nothing here behaves exactly as
                // it did before ROADMAP M1.3d read any of it.
                //
                // `priority` inside this block is MUGEN's HitDef parameter and
                // is deliberately NOT read here: the note above refuses it at
                // the `engine` level for being a different vocabulary's word,
                // and reading it one level down would reintroduce the collision
                // that note exists to prevent.
                if (const json* r = member(*e, "reaction")) {
                    if (!r->is_object())
                        return ctx.fail(where, "`engine.reaction` is not an object");
                    if (!readInt(ctx, *r, "hitstop_ticks", where, mv.hitstopTicks, false))
                        return false;
                    if (!readInt(ctx, *r, "air_hitstun_ticks", where, mv.airHitstunTicks, false))
                        return false;
                    if (!readInt(ctx, *r, "fall_recover_ticks", where, mv.fallRecoverTicks, false))
                        return false;
                    if (member(*r, "causes_knockdown") &&
                        !readBool(ctx, *r, "causes_knockdown", where, mv.causesKnockdown))
                        return false;

                    // A KNOCKDOWN WITH NO PER-MOVE RECOVERY IS ORDINARY, and
                    // this warns rather than refuses because the first draft
                    // refused and a shipped fixture caught it: AOF2's
                    // `punk_b_kick` knocks down with `fall_recover_ticks` 0.
                    // That is not a broken file. MUGEN carries liedown time as a
                    // CHARACTER-GLOBAL rather than per-move, so a transcribed
                    // move legitimately says "this knocks down" and leaves the
                    // duration to the character.
                    //
                    // The kernel has no global liedown time, so it will not
                    // knock down for such a move at all -- a real loss, named
                    // here rather than left as a silent zero.
                    if (mv.causesKnockdown && mv.fallRecoverTicks <= 0)
                        ctx.warn(where +
                            ": `engine.reaction.causes_knockdown` is true and "
                            "`fall_recover_ticks` is " + std::to_string(mv.fallRecoverTicks) +
                            ". MUGEN keeps liedown time per CHARACTER and this "
                            "engine keeps it per MOVE, so the kernel will not "
                            "knock down for this move. Author the duration on "
                            "the move to get the knockdown the source had.");
                }

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

                // The two v3 engine fields. Both are read AFTER the frame data
                // above, and A14 depends on that: it bounds the airborne tick
                // against startup, active and recovery, which have to be in
                // `mv` before the comparison means anything.
                if (!readHurtbox(ctx, *e, where, mv)) return false;
                if (!readAirborneFrom(ctx, *e, where, mv)) return false;

                // The alternative spelling, accepted only when the move level
                // did not author one. Same arrangement as hit_condition below,
                // and it exists for one reason: a `blocked_as` written in the
                // other place would validate cleanly and silently do nothing,
                // which is the single worst outcome available. Six lines removes
                // it. schema v3 x-v3-changes.two_spellings.
                if (!blockedAsAuthored) {
                    bool ignored = false;
                    if (!readBlockedAs(ctx, *e, "blocked_as", where, mv.blockedAs, ignored))
                        return false;
                }
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

    // --- assertion A16, the roster convention -------------------------------
    //
    // A WARNING, NEVER A REFUSAL, and the severity is the entire design of this
    // check. The standard is the author's: "we should consider every character
    // will have at least the following: six grounded normals (3 punches, 3
    // kicks), six crouching normals, six aerial attacks." AT LEAST is a floor a
    // designer aims at, not a shape a parser enforces. A grappler with no
    // aerials, a boss with four buttons and a character half blocked out at 2am
    // must all still load, and a loader that refuses to open an incomplete
    // character is a loader nobody can author a character in.
    //
    // IT COUNTS BY STANCE, WHICH IS AN UPPER BOUND ON NORMALS, and the direction
    // is stated because it is the honest one. The standard is about NORMALS --
    // three punches and three kicks -- and nothing in this schema distinguishes a
    // normal from a special, a super or a command normal. Recovering that from
    // move ids would be reading behaviour out of the spelling of a string, which
    // is the heuristic ARCHITECTURE.md D7 rejects and is exactly the defect this
    // whole feature exists to correct: `crouch_mk` and `stand_mk` were only ever
    // told apart by their names. So a character can pass this check on six
    // crouching SPECIALS. A character that FAILS it is definitely short; one that
    // passes may not be. The alternative was a check that lies in the comfortable
    // direction.
    //
    // `ground` COUNTS TOWARD NEITHER, and is reported on its own line. Counting a
    // grounded-unspecified move as standing or as crouching would invent the very
    // datum the transcription lost. Reported separately, the warning stops being
    // a nag and becomes the finding: kung_fu_girl.json does not LACK crouching
    // normals -- it has six, and the file does not say which they are.
    //
    // NOT GATED ON engine.schema_version, which was tempting. Suppressing it for
    // files declaring 1 or 2 would silence it on precisely the three files where
    // the collapse happened, which is the opposite of the point. It was silent
    // once already.
    {
        constexpr std::int32_t kRosterFloor = 6;
        std::int32_t standing = 0, crouching = 0, air = 0, unspecified = 0;
        for (const auto& m : out.moves) {
            // A switch rather than a chain of ifs so that the day Stance gains a
            // sixth value, a compiler with -Wswitch asks whether it belongs in
            // this roster. That question should be answered deliberately, and a
            // default: would answer it silently with "no".
            switch (m.stance) {
                case Stance::Standing:  ++standing;    break;
                case Stance::Crouching: ++crouching;   break;
                case Stance::Air:       ++air;         break;
                case Stance::Ground:    ++unspecified; break;
                case Stance::Any:       break;   // unrestricted: a roster of nothing in particular
            }
        }

        if (standing < kRosterFloor || crouching < kRosterFloor || air < kRosterFloor) {
            std::string missing;
            const auto note = [&](const char* what, std::int32_t have) {
                if (have >= kRosterFloor) return;
                if (!missing.empty()) missing += ", ";
                // The floor is read from the constant rather than typed as "6",
                // so the message cannot go on claiming 6 after somebody changes
                // the rule -- a diagnostic that misquotes its own threshold is
                // worse than one that omits it.
                missing += std::string(what) + " (has " + toString(have) + " of " +
                           toString(kRosterFloor) + ")";
            };
            note("standing",  standing);
            note("crouching", crouching);
            note("air",       air);

            std::string w = "A16: short of the roster convention -- " + missing +
                            ". This is a DESIGN STANDARD, not a format rule: `at least` is a "
                            "floor a designer aims at, and a character missing a whole "
                            "category still loads.";
            if (unspecified > 0)
                w += " " + toString(unspecified) + " further move(s) declare `ground`, which "
                     "means GROUNDED, STANCE UNSPECIFIED -- the file does not say which of "
                     "them are standing and which are crouching, and that is the datum MUGEN's "
                     "S/C/A/L statetype carried and the transcription dropped. Re-declaring "
                     "them as `standing` or `crouching` is the whole of the fix.";
            w += " Counted BY STANCE, which is an UPPER BOUND on normals: this schema has no "
                 "field distinguishing a normal from a special, and reading one out of a move "
                 "id is the heuristic D7 rejects.";
            ctx.warn(w);
        }
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

namespace {

// The one authored-file read: containment BEFORE the file is opened, every
// time (docs/MAINTENANCE.md -- "anything from scene content goes through
// PathIsContained before the file is opened. Absolute paths and `..` are
// refused."), then the size cap, then the bytes. Shared by the plain load and
// the variant load so the two cannot come to disagree about what a refused
// path is.
bool readAuthoredFile(const std::string& baseDir, const std::string& relPath,
                      const LoadOptions& options, std::string& text,
                      LoadReport& report) {
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
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        report.error = relPath + ": file: read failed";
        return false;
    }
    return true;
}

} // namespace

bool LoadCharacterFile(const std::string& baseDir,
                       const std::string& relPath,
                       const LoadOptions& options,
                       CharacterData& out,
                       LoadReport& report) {
    out = CharacterData{};
    report = LoadReport{};

    std::string text;
    if (!readAuthoredFile(baseDir, relPath, options, text, report)) return false;
    return LoadCharacterJson(relPath, text, options, out, report);
}

bool LoadCharacterVariant(const std::string& baseDir,
                          const std::string& baseRelPath,
                          const std::string& variantRelPath,
                          const LoadOptions& options,
                          CharacterData& out,
                          LoadReport& report,
                          std::string* description) {
    out = CharacterData{};
    report = LoadReport{};
    if (description != nullptr) description->clear();

    std::string baseText, variantText;
    if (!readAuthoredFile(baseDir, baseRelPath, options, baseText, report))
        return false;
    if (!readAuthoredFile(baseDir, variantRelPath, options, variantText, report))
        return false;

    nlohmann::json baseDoc = nlohmann::json::parse(baseText, nullptr, false);
    if (baseDoc.is_discarded()) {
        report.error = baseRelPath + ": json: does not parse";
        return false;
    }
    nlohmann::json variantDoc = nlohmann::json::parse(variantText, nullptr, false);
    if (variantDoc.is_discarded()) {
        report.error = variantRelPath + ": json: does not parse";
        return false;
    }

    // The one-line description is REQUIRED (docs/adr/ADR-011 section 4): a
    // catalogue entry that cannot say what it shows is a diff nobody can
    // exhibit, and requiring it here is what keeps the rule from decaying into
    // a convention.
    if (!variantDoc.contains("description") ||
        !variantDoc["description"].is_string() ||
        variantDoc["description"].get<std::string>().empty()) {
        report.error = variantRelPath + ": description: missing or empty. Every "
                       "variant carries one line saying what the diff shows.";
        return false;
    }
    if (description != nullptr)
        *description = variantDoc["description"].get<std::string>();

    if (!variantDoc.contains("patch") || !variantDoc["patch"].is_object()) {
        report.error = variantRelPath + ": patch: missing or not an object. A "
                       "variant IS its patch; a file without one exhibits "
                       "nothing.";
        return false;
    }
    nlohmann::json patch = variantDoc["patch"];

    // MOVES ARE PATCHED BY ID, NOT BY RFC 7386. A merge patch treats arrays as
    // atomic, so a standard patch touching one move would have to restate all
    // of them and the exhibit would stop being the diff. The variant format
    // therefore spells `moves` as an OBJECT keyed by move id, each value an
    // RFC 7386 merge applied to that one array element; every other key merges
    // at the top level the standard way. An id the base does not author is
    // refused -- a patch that silently patched nothing is the worst kind of
    // green.
    if (patch.contains("moves")) {
        if (!patch["moves"].is_object()) {
            report.error = variantRelPath + ": patch.moves: must be an OBJECT "
                           "keyed by move id (arrays are atomic under merge "
                           "patch; restating every move is not a diff).";
            return false;
        }
        if (!baseDoc.contains("moves") || !baseDoc["moves"].is_array()) {
            report.error = baseRelPath + ": moves: missing or not an array";
            return false;
        }
        for (auto it = patch["moves"].begin(); it != patch["moves"].end(); ++it) {
            bool found = false;
            for (nlohmann::json& m : baseDoc["moves"]) {
                if (!m.is_object() || !m.contains("id")) continue;
                if (m["id"] != it.key()) continue;
                m.merge_patch(it.value());
                found = true;
                break;
            }
            if (!found) {
                report.error = variantRelPath + ": patch.moves." + it.key() +
                               ": the base character authors no move with this "
                               "id, so the patch would silently change nothing.";
                return false;
            }
        }
        patch.erase("moves");
    }

    // CANCELS ARE APPENDED, NOT MERGED, for the same atomic-array reason as
    // moves -- and unlike moves they have no single natural key (from, to and
    // delay can all repeat), so the variant format supports exactly the one
    // operation the showcase needs: `patch.cancels` is `{ "append": [edge...] }`
    // and each edge lands at the end of the authored list. Anything else --
    // an array, an edit, a delete -- is refused with the reason, so a future
    // need announces itself here instead of silently restating ninety edges.
    if (patch.contains("cancels")) {
        if (!patch["cancels"].is_object() ||
            !patch["cancels"].contains("append") ||
            !patch["cancels"]["append"].is_array() ||
            patch["cancels"].size() != 1) {
            report.error = variantRelPath + ": patch.cancels: must be exactly "
                           "{ \"append\": [edge, ...] } (arrays are atomic "
                           "under merge patch, and edges have no single "
                           "natural key to merge by).";
            return false;
        }
        if (!baseDoc.contains("cancels") || !baseDoc["cancels"].is_array()) {
            report.error = baseRelPath + ": cancels: missing or not an array";
            return false;
        }
        for (const nlohmann::json& edge : patch["cancels"]["append"])
            baseDoc["cancels"].push_back(edge);
        patch.erase("cancels");
    }

    baseDoc.merge_patch(patch);
    return LoadCharacterJson(baseRelPath + " + " + variantRelPath,
                             baseDoc.dump(), options, out, report);
}

} // namespace cse::data
