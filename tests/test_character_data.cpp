// The character loader, against the three characters that actually exist.
//
// Two halves, and the second is the one that matters.
//
// The first half loads Exported/Characters/kung_fu_girl.json, kung_fu_man.json
// and aof2_strength_training.json -- 59 moves and 247 cancel edges transcribed
// from a real corpus -- and checks the numbers against ADR-001's own table. That
// is a regression test on the loader.
//
// The second half makes every ADR-001 load assertion FAIL, on purpose, from a
// real character mutated one field at a time. A rule nobody has watched reject
// anything is not a rule, and this repository has the receipts: assertion A01
// exists because the project's own draft decay rule (linear, step 2, floor 10)
// FABRICATED AN INFINITE COMBO on Kung Fu Girl, and assertion A04 had to be
// corrected because its first wording rejected all three shipped characters.
// Both of those are reproduced below as tests, from the real files, so that
// neither mistake can come back quietly.
//
// The mutations go through nlohmann::json rather than string surgery on the file
// text, because a test that edits JSON with find-and-replace stops testing what
// it claims the moment somebody reformats a file.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace cse::data;
using json = nlohmann::json;

namespace {

// The build-wide resource order. ADR-001 section 8 item 7: the prover keys
// resources POSITIONALLY, so index 0 must mean the same resource in every file a
// build loads. All three Phase-0 characters declare these two in this order even
// where one of them is inert, and that is exactly why.
LoadOptions phase0Options() {
    LoadOptions o;
    o.expectedResources = { "meter", "juggle" };
    return o;
}

// Where the shipped characters live. CSE_CHARACTERS_DIR is set by CMake; the
// walk-up fallback keeps the test runnable from a shell in the tree, and it
// cannot make the test pass vacuously because every load below ASSERTs.
std::string charactersDir() {
#ifdef CSE_CHARACTERS_DIR
    return CSE_CHARACTERS_DIR;
#else
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        // Two spellings, and both are load-bearing. The first is where the
        // characters are STAGED -- next to the executable, because they live in
        // Editor/src/Exported/Characters and stage_runtime_assets.cmake copies
        // every subdirectory of that asset root. Tests run with their own build
        // directory as the working directory, so this hits on the first
        // iteration. The second is the SOURCE tree, which keeps the binary
        // runnable from a shell anywhere above it.
        for (const fs::path candidate : {
                 here / "Exported" / "Characters",
                 here / "Editor" / "src" / "Exported" / "Characters" }) {
            if (fs::exists(candidate / "kung_fu_girl.json")) return candidate.string();
        }
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported/Characters";
#endif
}

const char* kShipped[] = {
    "kung_fu_girl.json",
    "kung_fu_man.json",
    "aof2_strength_training.json",
};

std::string readShipped(const char* file) {
    const std::filesystem::path p = std::filesystem::path(charactersDir()) / file;
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << p.string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The shipped file, parsed, ready to be mutated one field at a time.
json shippedDoc(const char* file) {
    const std::string text = readShipped(file);
    json doc = json::parse(text.begin(), text.end(), nullptr, false);
    EXPECT_FALSE(doc.is_discarded()) << file << " is not valid JSON";
    return doc;
}

bool loadDoc(const json& doc, const char* name, CharacterData& out, LoadReport& report,
             const LoadOptions& options = phase0Options()) {
    return LoadCharacterJson(name, doc.dump(), options, out, report);
}

// Index of a move inside a parsed document's `moves` array, by id.
std::size_t moveSlot(const json& doc, const char* id) {
    for (std::size_t i = 0; i < doc["moves"].size(); ++i)
        if (doc["moves"][i]["id"] == id) return i;
    ADD_FAILURE() << "no move `" << id << "` in the document";
    return 0;
}

bool mentions(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// The three real characters
// ---------------------------------------------------------------------------

TEST(CharacterFiles, AllThreeShippedCharactersLoadClean) {
    for (const char* file : kShipped) {
        CharacterData c{};
        LoadReport r{};
        ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, phase0Options(), c, r))
            << file << " failed to load: " << r.error;
        EXPECT_TRUE(r.error.empty());
        EXPECT_TRUE(r.rule.empty());
        EXPECT_FALSE(c.name.empty());
        EXPECT_EQ(c.stage, "midscreen") << file << " changed which stage its verdict answers";
    }
}

TEST(CharacterFiles, CountsMatchTheDecisionRecord) {
    // ADR-001 section 2's table, re-verified against the files themselves rather
    // than carried over from the document -- the ADR is allowed to go stale and
    // this test is not.
    struct Expected { const char* file; std::size_t moves; std::size_t cancels; };
    const Expected expected[] = {
        { "kung_fu_girl.json",           25, 134 },
        { "kung_fu_man.json",            24,  87 },
        { "aof2_strength_training.json", 10,  26 },
    };

    std::size_t totalMoves = 0, totalCancels = 0;
    for (const auto& e : expected) {
        CharacterData c{};
        LoadReport r{};
        ASSERT_TRUE(LoadCharacterFile(charactersDir(), e.file, phase0Options(), c, r)) << r.error;
        EXPECT_EQ(c.moves.size(),   e.moves)   << e.file << ": move count";
        EXPECT_EQ(c.cancels.size(), e.cancels) << e.file << ": cancel count";
        EXPECT_EQ(c.cancelsFrom.size(), c.moves.size());
        totalMoves   += c.moves.size();
        totalCancels += c.cancels.size();
    }
    EXPECT_EQ(totalMoves,   59u) << "ADR-001 transcribed 59 moves";
    EXPECT_EQ(totalCancels, 247u) << "ADR-001 transcribed 247 cancel edges";
}

TEST(CharacterFiles, TheProverReadSubsetSurvivesTheLoad) {
    CharacterData c{};
    LoadReport r{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), "kung_fu_girl.json", phase0Options(), c, r))
        << r.error;

    const MoveIndex lp = c.FindMove("stand_lp");
    ASSERT_NE(kInvalidMove, lp);
    const Move& m = c.moves[lp];

    // Frame data, straight from corpus/kfg/Normal.cns via the file.
    EXPECT_EQ(m.startup,  3);
    EXPECT_EQ(m.active,   2);
    EXPECT_EQ(m.recovery, 6);
    EXPECT_EQ(m.hitstun,  9);   // ground.hittime 9, the number A01 turns on
    EXPECT_EQ(m.stance, Stance::Ground);

    // Every float in the file has become an integer. 23.0 damage is 2300
    // hundredths; 0.57 reach units is 57 px is 57*256 sub-units; 0.13 pushback is
    // 13 px. If any of these three drift, the loader has started rounding.
    EXPECT_EQ(m.damageHundredths, 2300);
    EXPECT_EQ(m.reachSub,    57 * 256);
    EXPECT_EQ(m.pushbackSub, 13 * 256);

    // effect/guard resolved to positional resource handles.
    ASSERT_EQ(m.effect.size(), 1u);
    EXPECT_EQ(static_cast<int>(m.effect[0].resource), 0);  // index 0 is `meter`, per the contract
    EXPECT_EQ(m.effect[0].value, 5);
    EXPECT_TRUE(m.guard.empty());

    // The gate over the defender that no schema field can express, kept as the
    // prose the file authors. All 17 of this character's normals carry it.
    EXPECT_FALSE(m.hitConditionProse.empty())
        << "stand_lp's hit condition was dropped on the floor";

    // super_palm is the one that spends: -100 meter with a guard of 100.
    const MoveIndex sp = c.FindMove("super_palm");
    ASSERT_NE(kInvalidMove, sp);
    ASSERT_EQ(c.moves[sp].effect.size(), 1u);
    EXPECT_EQ(c.moves[sp].effect[0].value, -100);
    ASSERT_EQ(c.moves[sp].guard.size(), 1u);
    EXPECT_EQ(c.moves[sp].guard[0].value, 100);

    // The first cancel edge: stand_lp into itself at delay 2. ADR-001's hardest
    // single finding is that this number is NOT zero -- Kung Fu Girl's chain
    // cancels are gated on var(5), whose opening frame is authored per source
    // state, and the delays run from 2 to 20. A loader that defaulted them to 0
    // would hand the character up to 20 free frames on every edge.
    ASSERT_FALSE(c.cancels.empty());
    EXPECT_EQ(c.cancels[0].from, lp);
    EXPECT_EQ(c.cancels[0].to,   lp);
    EXPECT_EQ(c.cancels[0].delay, 2);
    EXPECT_EQ(c.cancels[0].on, Contact::Hit);
    EXPECT_FALSE(c.cancels[0].certain);

    // The reverse index actually points somewhere.
    ASSERT_LT(static_cast<std::size_t>(lp), c.cancelsFrom.size());
    EXPECT_FALSE(c.cancelsFrom[lp].empty())
        << "stand_lp has outgoing cancels in the file but none in the index";

    // Handles, not strings, and a miss is a miss rather than move 0.
    EXPECT_EQ(c.FindMove("no_such_move"), kInvalidMove);
    const MoveIndex chop = c.FindMove("chop");
    ASSERT_NE(kInvalidMove, chop);
    EXPECT_EQ(c.moves[chop].id, "chop");

    // Starters resolved to indices too.
    EXPECT_FALSE(c.starters.empty());
    for (MoveIndex s : c.starters) EXPECT_LT(static_cast<std::size_t>(s), c.moves.size());
}

TEST(CharacterFiles, NoFloatSurvivesTheLoad) {
    CharacterData kfg{}, kfm{};
    LoadReport r{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), "kung_fu_girl.json", phase0Options(), kfg, r))
        << r.error;
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), "kung_fu_man.json", phase0Options(), kfm, r))
        << r.error;

    // 0.03 reach units/tick = 3 px/tick = 768 sub-units.
    EXPECT_EQ(kfg.walkSpeedSub, 3 * 256);
    // Kung Fu Man's 2.4 px/tick is the number ADR-001 section 6.3 names as the
    // one place "no loader ever rounds" is not achievable: 614 sub-units is
    // exact, and the prover's 0.024 reach units is not.
    EXPECT_EQ(kfm.walkSpeedSub, 614);

    const std::vector<std::int32_t> scaling{ 1000, 900, 800, 700, 600, 500 };
    EXPECT_EQ(kfg.scalingPermille, scaling);
    EXPECT_EQ(kfm.scalingPermille, scaling);
}

TEST(CharacterFiles, AnAuthoredIntegerBeatsTheFloatBesideIt) {
    // The corpus never disagrees with itself, so the precedence rule -- when a
    // file ships both a float and a pre-quantized integer, the INTEGER wins,
    // because it is the number the author derived -- cannot be observed on real
    // data. Make them disagree and watch which one comes out.
    json doc = shippedDoc("kung_fu_man.json");
    doc["engine"]["quantized_sources"]["walk_speed_sub_per_tick"] = 999;

    CharacterData c{};
    LoadReport r{};
    ASSERT_TRUE(loadDoc(doc, "kfm_walk.json", c, r)) << r.error;
    EXPECT_EQ(c.walkSpeedSub, 999)
        << "the loader recomputed the walk speed from the float instead of taking the "
           "authored sub-unit integer";
}

// ---------------------------------------------------------------------------
// A03 -- resource order is a build-wide contract
// ---------------------------------------------------------------------------

TEST(CharacterFiles, EveryShippedCharacterDeclaresTheSameResourceOrder) {
    for (const char* file : kShipped) {
        CharacterData c{};
        LoadReport r{};
        ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, phase0Options(), c, r)) << r.error;
        ASSERT_EQ(c.resources.size(), 2u) << file;
        EXPECT_EQ(c.resources[0].name, "meter")  << file;
        EXPECT_EQ(c.resources[1].name, "juggle") << file;
    }
}

TEST(LoadAssertions, A03FiresWhenAFileDisagreesWithTheBuild) {
    // What it protects: comboprover::Character keys a ResourceVec by INDEX, so a
    // file that declares juggle first compares juggle against meter and says
    // nothing at all.
    LoadOptions swapped;
    swapped.expectedResources = { "juggle", "meter" };

    for (const char* file : kShipped) {
        CharacterData c{};
        LoadReport r{};
        EXPECT_FALSE(LoadCharacterFile(charactersDir(), file, swapped, c, r))
            << file << " was accepted against a build that orders resources the other way";
        EXPECT_EQ(r.rule, "A03");
        EXPECT_TRUE(mentions(r.error, file)) << r.error;
        EXPECT_TRUE(mentions(r.error, "meter")) << r.error;
    }

    // A short list is a mismatch too: a build with one resource and a file with
    // two disagree about what index 1 means.
    LoadOptions shorter;
    shorter.expectedResources = { "meter" };
    CharacterData c{};
    LoadReport r{};
    EXPECT_FALSE(LoadCharacterFile(charactersDir(), "kung_fu_girl.json", shorter, c, r));
    EXPECT_EQ(r.rule, "A03");
}

TEST(LoadAssertions, A03IsSkippedLOUDLYWhenTheCallerNamesNoOrder) {
    // A cross-file rule cannot be checked from inside one file, so a caller that
    // supplies no order gets no A03 at all. That is a legitimate configuration
    // and a dangerous one, so it must be visible rather than silent -- otherwise
    // a build acquires an unchecked positional contract and nobody finds out
    // until two characters disagree.
    LoadOptions none;
    CharacterData c{};
    LoadReport r{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), "kung_fu_girl.json", none, c, r)) << r.error;

    bool warned = false;
    for (const auto& w : r.warnings) warned = warned || mentions(w, "A03");
    EXPECT_TRUE(warned) << "A03 was skipped without saying so";
}

// ---------------------------------------------------------------------------
// A01 -- the assertion that would have caught a fabricated infinite
// ---------------------------------------------------------------------------

TEST(LoadAssertions, A01RejectsTheDraftHouseRuleThatFabricatedAnInfinite) {
    // This is the real historical mistake, reproduced. The project's first draft
    // decay rule was `linear, step 2, floor 10`, applied uniformly to every
    // character. Both reference implementations compute linear decay as
    // max(floor, base - step*n), so a floor ABOVE a move's authored hitstun does
    // not clamp it down -- it RAISES it. Kung Fu Girl's stand_lp has
    // ground.hittime 9. Floor 10 gives it a tenth frame it does not have, and the
    // midscreen verdict flipped to INFINITE: an infinite combo invented by a
    // balance constant. ADR-001 section 8 item 3 requires this to be caught at
    // load, which is what the next four lines check.
    json doc = shippedDoc("kung_fu_girl.json");
    doc["decay"]["kind"]  = "linear";
    doc["decay"]["step"]  = 2;
    doc["decay"]["floor"] = 10;

    CharacterData c{};
    LoadReport r{};
    EXPECT_FALSE(loadDoc(doc, "kung_fu_girl.json", c, r))
        << "the loader accepted a decay floor that raises a move's hitstun";
    EXPECT_EQ(r.rule, "A01");
    // The message has to name the file, the move and the rule, or nobody can act
    // on it: "assertion failed" in a character pipeline is not a bug report.
    EXPECT_TRUE(mentions(r.error, "kung_fu_girl.json")) << r.error;
    EXPECT_TRUE(mentions(r.error, "A01"))               << r.error;
    EXPECT_TRUE(mentions(r.error, "stand_lp"))          << r.error;
    EXPECT_TRUE(mentions(r.error, "10"))                << r.error;
    EXPECT_TRUE(mentions(r.error, "9"))                 << r.error;

    // And nothing half-built comes back out.
    EXPECT_TRUE(c.moves.empty());
    EXPECT_TRUE(c.cancels.empty());
}

TEST(LoadAssertions, A01IsAnInequalityAndNotAProhibition) {
    // The two ways this assertion could be wrong, both checked, because a rule
    // that rejects everything is as useless as one that rejects nothing.
    //
    // Off-by-one: the rule is floor <= min(hitstun). A floor EQUAL to the
    // smallest hitstun is legal -- it clamps that move and nothing else -- and a
    // loader that used `<` would reject a correct file.
    json doc = shippedDoc("kung_fu_girl.json");
    doc["decay"]["kind"]  = "linear";
    doc["decay"]["step"]  = 2;
    doc["decay"]["floor"] = 9;          // exactly stand_lp's hitstun

    CharacterData c{};
    LoadReport r{};
    EXPECT_TRUE(loadDoc(doc, "kung_fu_girl.json", c, r))
        << "a floor equal to the smallest hitstun was rejected: " << r.error;
    EXPECT_EQ(c.decay.kind, DecayKind::Linear);
    EXPECT_EQ(c.decay.floor, 9);

    // Vacuity: A01 is not satisfied by "every shipped file has floor 0". Kung Fu
    // Man really does ship the house rule with a floor of 10, and it passes
    // because its smallest hitstun is 11. If this ever starts failing, either the
    // file changed or A01 has quietly become "floor must be zero".
    CharacterData kfm{};
    LoadReport kr{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), "kung_fu_man.json", phase0Options(), kfm, kr))
        << kr.error;
    EXPECT_EQ(kfm.decay.kind, DecayKind::Linear);
    EXPECT_EQ(kfm.decay.floor, 10);
    std::int32_t smallest = kfm.moves[0].hitstun;
    for (const auto& m : kfm.moves) smallest = m.hitstun < smallest ? m.hitstun : smallest;
    EXPECT_GT(smallest, 0);
    EXPECT_GE(smallest, kfm.decay.floor)
        << "Kung Fu Man's shipped floor no longer clears its smallest hitstun";
}

// ---------------------------------------------------------------------------
// A02 -- multiplicative decay
// ---------------------------------------------------------------------------

TEST(LoadAssertions, A02RejectsMultiplicativeDecayByName) {
    // Banned by measurement, not taste: the C++ reference computes it by repeated
    // float multiply then a truncating cast and the Python one by base*ratio**n
    // then int(), and an integer kernel reproduces neither. For a decision that
    // turns on `hitstun >= startup - advantage`, one frame of disagreement is the
    // whole difference between TERMINATING and INFINITE.
    json doc = shippedDoc("kung_fu_man.json");
    doc["decay"]["kind"] = "multiplicative";

    CharacterData c{};
    LoadReport r{};
    EXPECT_FALSE(loadDoc(doc, "kung_fu_man.json", c, r));
    EXPECT_EQ(r.rule, "A02");
    EXPECT_TRUE(mentions(r.error, "kung_fu_man.json")) << r.error;
    EXPECT_TRUE(mentions(r.error, "multiplicative"))   << r.error;

    // An unknown kind is also refused, but it is NOT A02 -- the point of naming
    // A02 separately is that the reason for the ban is recorded, and a typo does
    // not get to borrow that reason.
    doc["decay"]["kind"] = "quadratic";
    LoadReport r2{};
    EXPECT_FALSE(loadDoc(doc, "kung_fu_man.json", c, r2));
    EXPECT_TRUE(r2.rule.empty()) << "an unrecognised kind was reported as " << r2.rule;
    EXPECT_FALSE(r2.error.empty());
}

// ---------------------------------------------------------------------------
// A04 -- __space, and the correction that saved all three characters
// ---------------------------------------------------------------------------

TEST(LoadAssertions, A04ForbidsSpaceOnAMoveAndOnACancel) {
    // On a move or a cancel the prover MERGES an authored `__space` with the value
    // it derives from reach and pushback (model.py:476 takes a max, :479
    // subtracts the pushback) instead of honouring it, so the number silently
    // combines with a derived one and the file stops saying what it appears to
    // say.
    {
        json doc = shippedDoc("kung_fu_girl.json");
        doc["moves"][moveSlot(doc, "stand_lp")]["effect"]["__space"] = 5;

        CharacterData c{};
        LoadReport r{};
        EXPECT_FALSE(loadDoc(doc, "kung_fu_girl.json", c, r));
        EXPECT_EQ(r.rule, "A04");
        EXPECT_TRUE(mentions(r.error, "stand_lp")) << r.error;
        EXPECT_TRUE(mentions(r.error, "__space"))  << r.error;
    }
    {
        json doc = shippedDoc("kung_fu_girl.json");
        doc["cancels"][0]["guard"]["__space"] = 5;

        CharacterData c{};
        LoadReport r{};
        EXPECT_FALSE(loadDoc(doc, "kung_fu_girl.json", c, r));
        EXPECT_EQ(r.rule, "A04");
        EXPECT_TRUE(mentions(r.error, "cancel"))  << r.error;
        EXPECT_TRUE(mentions(r.error, "__space")) << r.error;
    }
}

TEST(LoadAssertions, A04ExemptsGapActionsAndEveryShippedCharacterNeedsThat) {
    // THIS IS THE VACUITY GUARD FOR THE EXEMPTION, and it is not hypothetical.
    // An earlier draft of A04 read "no effect or guard map may author __space",
    // unqualified. Executed as written it rejects ALL THREE shipped characters:
    // kung_fu_girl and kung_fu_man author {__space: 0} on the microdash gap
    // action, and aof2 authors {__space: 170} on run_close. On a gap action
    // authoring __space is the only way to express displacement and is the
    // documented mechanism.
    //
    // So: every shipped character must load, AND every shipped character must
    // actually exercise the exempt branch. Without the second half, deleting the
    // exemption would leave this test green.
    struct Expected { const char* file; std::int32_t spacePx; };
    const Expected expected[] = {
        { "kung_fu_girl.json",             0 },   // has a run, but no fixed-distance dash
        { "kung_fu_man.json",              0 },   // hold-to-run; a fixed gap action would be invented
        { "aof2_strength_training.json", 170 },   // 8.5 px/frame for a fixed 20 ticks
    };

    for (const auto& e : expected) {
        CharacterData c{};
        LoadReport r{};
        ASSERT_TRUE(LoadCharacterFile(charactersDir(), e.file, phase0Options(), c, r))
            << e.file << ": " << r.error;
        ASSERT_FALSE(c.gapActions.empty()) << e.file << " has no gap action at all";
        EXPECT_TRUE(c.gapActions[0].hasSpaceEffect)
            << e.file << " does not author __space on its gap action, so this test is not "
                         "exercising A04's exemption";
        // Stored in sub-units, like every other distance in the header. One prover
        // space unit is exactly one pixel.
        EXPECT_EQ(c.gapActions[0].spaceEffectSub, e.spacePx * 256) << e.file;
    }
}

// ---------------------------------------------------------------------------
// The engine-only namespace: hits[] and motion[]
// ---------------------------------------------------------------------------
//
// No shipped character carries these yet -- they are two of schema v2's nine new
// fields and the three Phase-0 files are v1-shaped. So the data comes from
// schema.v2.json's own worked examples, transcribed here, and is injected into
// the real move each example is about. Without this the hits/motion half of the
// loader would be code that has never seen an input.

namespace {

// schema.v2.json x-worked-examples.hits -- Kung Fu Man f_lk_cancel, statedef 275,
// two HitDefs at animelem 4 and 10 (kfma4a_cns.txt:779-808 and :810-839).
json workedHitsForFLkCancel() {
    return json::array({
        json{ {"tick", 4},  {"anim_elem", 4},  {"damage_hundredths", 2000},
              {"hitstun_ticks", 23}, {"slide_ticks", 12}, {"pushback_vel_sub", -768} },
        json{ {"tick", 21}, {"anim_elem", 10}, {"damage_hundredths", 3000},
              {"hitstun_ticks", 23}, {"slide_ticks", 16}, {"pushback_vel_sub", -768} },
    });
}

// schema.v2.json x-worked-examples.hits_alternative_profile -- Kung Fu Girl chop.
// The second record is an ALTERNATIVE, registered only when the first WHIFFED,
// which is why it carries a condition and why A06 must not count it.
json workedHitsForChop(bool keepCondition) {
    json second{ {"tick", 17}, {"anim_elem", 5}, {"damage_hundredths", 9600},
                 {"hitstun_ticks", 33}, {"slide_ticks", 10},
                 {"pushback_vel_sub", -2304} };
    if (keepCondition) {
        second["condition"] = json{
            {"all", json::array({
                json{ {"property", "self.move_contact"},  {"op", "eq"}, {"value", false} },
                json{ {"property", "self.move_reversed"}, {"op", "eq"}, {"value", false} },
            })},
            {"prose", "triggerall = !movecontact && !movereversed"},
        };
    }
    return json::array({
        json{ {"tick", 14}, {"anim_elem", 4}, {"damage_hundredths", 10200},
              {"hitstun_ticks", 30}, {"slide_ticks", 10},
              {"pushback_vel_sub", 0}, {"reach_sub", 14848} },
        second,
    });
}

// schema.v2.json x-worked-examples.motion -- Kung Fu Girl palm, statedef 1000:
// a 3 px teleport at tick 3, 4.4 px/tick for ticks 6-13, zero from tick 14.
json workedMotionForPalm() {
    return json::array({
        json{ {"tick", 3},  {"vel_x_sub", 0},    {"vel_y_sub", 0}, {"pos_add_x_sub", 768} },
        json{ {"tick", 6},  {"vel_x_sub", 1126}, {"vel_y_sub", 0} },
        json{ {"tick", 14}, {"vel_x_sub", 0},    {"vel_y_sub", 0} },
    });
}

} // namespace

TEST(EngineNamespace, MultiHitAndMotionArraysLoad) {
    {
        json doc = shippedDoc("kung_fu_man.json");
        const std::size_t slot = moveSlot(doc, "f_lk_cancel");
        doc["moves"][slot]["engine"]["hits"] = workedHitsForFLkCancel();
        doc["moves"][slot]["engine"]["hits_projection"] = "first";

        CharacterData c{};
        LoadReport r{};
        ASSERT_TRUE(loadDoc(doc, "kung_fu_man.json", c, r)) << r.error;
        const Move& m = c.moves[c.FindMove("f_lk_cancel")];
        ASSERT_EQ(m.hits.size(), 2u);
        EXPECT_EQ(m.hitsProjection, HitsProjection::First);
        EXPECT_EQ(m.hits[0].tick, 4);
        EXPECT_EQ(m.hits[0].damageHundredths, 2000);
        EXPECT_EQ(m.hits[1].tick, 21);
        EXPECT_EQ(m.hits[1].pushbackVelSub, -768);
        EXPECT_FALSE(m.hits[1].isAlternative);
        // The file authors no blockstun for Kung Fu Man anywhere; absent stays -1
        // rather than becoming a zero that reads as "no blockstun frames".
        EXPECT_EQ(m.hits[0].blockstunTicks, -1);
    }
    {
        json doc = shippedDoc("kung_fu_girl.json");
        doc["moves"][moveSlot(doc, "palm")]["engine"]["motion"] = workedMotionForPalm();

        CharacterData c{};
        LoadReport r{};
        ASSERT_TRUE(loadDoc(doc, "kung_fu_girl.json", c, r)) << r.error;
        const Move& m = c.moves[c.FindMove("palm")];
        ASSERT_EQ(m.motion.size(), 3u);
        // PosAdd is a teleport, not a velocity, and it has its own slot precisely
        // so that integrating it as a velocity is not possible.
        EXPECT_EQ(m.motion[0].posAddXSub, 768);
        EXPECT_EQ(m.motion[0].velXSub, 0);
        EXPECT_EQ(m.motion[1].velXSub, 1126);
        EXPECT_EQ(m.motion[2].velXSub, 0);
    }
}

TEST(LoadAssertions, A05RequiresTheScalarsToBeTheFirstHit) {
    // Every cancel delay in the corpus is measured from FIRST contact, so if the
    // [P] scalars are some other hit the delay arithmetic is against the wrong
    // frame.
    json doc = shippedDoc("kung_fu_man.json");
    const std::size_t slot = moveSlot(doc, "f_lk_cancel");
    json hits = workedHitsForFLkCancel();
    hits[0]["tick"] = 5;                       // move.startup is 4
    doc["moves"][slot]["engine"]["hits"] = hits;

    CharacterData c{};
    LoadReport r{};
    EXPECT_FALSE(loadDoc(doc, "kung_fu_man.json", c, r));
    EXPECT_EQ(r.rule, "A05");
    EXPECT_TRUE(mentions(r.error, "f_lk_cancel")) << r.error;
}

TEST(LoadAssertions, A06CountsSequelsAndNotAlternatives) {
    // Kung Fu Girl's chop, exactly as the schema's worked example ships it: two
    // hit records with different hitstun, 30 and 33, and the second gated on the
    // first having WHIFFED. They are mutually exclusive, so the second is never
    // "the hitstun the combo leaves" and A06 must stay quiet. A draft of A06
    // without the exclusion fired here, and it was wrong -- counting an
    // alternative as a sequel would also credit the attacker with 198 damage from
    // one button.
    {
        json doc = shippedDoc("kung_fu_girl.json");
        const std::size_t slot = moveSlot(doc, "chop");
        doc["moves"][slot]["engine"]["hits"] = workedHitsForChop(/*keepCondition*/ true);
        doc["moves"][slot]["engine"]["hits_projection"] = "first";

        CharacterData c{};
        LoadReport r{};
        ASSERT_TRUE(loadDoc(doc, "kung_fu_girl.json", c, r))
            << "A06 fired on an alternative profile: " << r.error;
        const Move& m = c.moves[c.FindMove("chop")];
        ASSERT_EQ(m.hits.size(), 2u);
        EXPECT_FALSE(m.hits[0].isAlternative);
        EXPECT_TRUE(m.hits[1].isAlternative)
            << "the conditional record was not recognised as an alternative, so A06 stayed "
               "quiet for the wrong reason";
        EXPECT_FALSE(m.hits[1].conditionProse.empty());
        EXPECT_EQ(m.hits[0].reachSub, 14848);   // chop's first box, 58 px
    }
    // Drop the condition and the same two records ARE a sequel: the combo now
    // leaves 33 frames of hitstun where the projection says 30, which is an
    // approximation whose direction only the author knows.
    {
        json doc = shippedDoc("kung_fu_girl.json");
        const std::size_t slot = moveSlot(doc, "chop");
        doc["moves"][slot]["engine"]["hits"] = workedHitsForChop(/*keepCondition*/ false);
        doc["moves"][slot]["engine"]["hits_projection"] = "first";

        CharacterData c{};
        LoadReport r{};
        EXPECT_FALSE(loadDoc(doc, "kung_fu_girl.json", c, r))
            << "two unconditional hits with different hitstun were accepted with no caveat";
        EXPECT_EQ(r.rule, "A06");
        EXPECT_TRUE(mentions(r.error, "chop")) << r.error;

        // ...and the caveat is what makes it loadable again, which is the whole
        // point of the rule: it does not forbid the shape, it forbids shipping it
        // without naming the direction of the error.
        doc["moves"][slot]["engine"]["hits_projection_caveat"] =
            "move.hitstun understates the combo's real hitstun by 3 frames, which can hide "
            "an infinite and cannot invent one";
        LoadReport r2{};
        EXPECT_TRUE(loadDoc(doc, "kung_fu_girl.json", c, r2)) << r2.error;
    }
}

TEST(LoadAssertions, A07AndA08RejectTicksThatDoNotIncrease) {
    {
        json doc = shippedDoc("kung_fu_man.json");
        const std::size_t slot = moveSlot(doc, "f_lk_cancel");
        json hits = workedHitsForFLkCancel();
        hits[1]["tick"] = 4;                  // equal to hits[0], so "first" is ambiguous
        doc["moves"][slot]["engine"]["hits"] = hits;

        CharacterData c{};
        LoadReport r{};
        EXPECT_FALSE(loadDoc(doc, "kung_fu_man.json", c, r));
        EXPECT_EQ(r.rule, "A07");
    }
    {
        json doc = shippedDoc("kung_fu_girl.json");
        json motion = workedMotionForPalm();
        motion[2]["tick"] = 6;                // equal to the previous keyframe
        doc["moves"][moveSlot(doc, "palm")]["engine"]["motion"] = motion;

        CharacterData c{};
        LoadReport r{};
        EXPECT_FALSE(loadDoc(doc, "kung_fu_girl.json", c, r));
        EXPECT_EQ(r.rule, "A08");
    }
    {
        // A08's other half: a velocity must be an integer. A float here would be a
        // float in the simulation, and ARCHITECTURE.md D2 does not allow one.
        json doc = shippedDoc("kung_fu_girl.json");
        json motion = workedMotionForPalm();
        motion[1]["vel_x_sub"] = 1126.4;
        doc["moves"][moveSlot(doc, "palm")]["engine"]["motion"] = motion;

        CharacterData c{};
        LoadReport r{};
        EXPECT_FALSE(loadDoc(doc, "kung_fu_girl.json", c, r));
        EXPECT_TRUE(mentions(r.error, "vel_x_sub")) << r.error;
    }
}

// ---------------------------------------------------------------------------
// Untrusted input
// ---------------------------------------------------------------------------

TEST(UntrustedContent, AuthoredPathsCannotEscapeTheCharacterDirectory) {
    // docs/MAINTENANCE.md, "authored paths are untrusted": anything from authored
    // content goes through PathIsContained BEFORE the file is opened, and that
    // rule has no exceptions. A character file is authored content with a path
    // attached to it.
    CharacterData c{};
    LoadReport r{};

    EXPECT_FALSE(LoadCharacterFile(charactersDir(), "../kung_fu_girl.json", {}, c, r));
    EXPECT_FALSE(r.error.empty());
    EXPECT_FALSE(LoadCharacterFile(charactersDir(), "a/../../kung_fu_girl.json", {}, c, r));
    EXPECT_FALSE(LoadCharacterFile(charactersDir(), "/etc/passwd", {}, c, r));

#ifdef _WIN32
    // Windows-shaped only, deliberately. MAINTENANCE.md records the trap: on
    // libstdc++ a backslash is an ordinary filename character and "C:" is just a
    // first path component, so these strings are correctly CONTAINED on Linux and
    // asserting EXPECT_FALSE on them there would be asserting a bug.
    EXPECT_FALSE(LoadCharacterFile(charactersDir(), "C:/Windows/win.ini", {}, c, r));
    EXPECT_FALSE(LoadCharacterFile(charactersDir(), "..\\kung_fu_girl.json", {}, c, r));
#endif

    // The control: the same call with an ordinary name works, so the four
    // rejections above are about the paths and not about the loader being broken.
    EXPECT_TRUE(LoadCharacterFile(charactersDir(), "kung_fu_girl.json", phase0Options(), c, r))
        << r.error;
}

TEST(UntrustedContent, MalformedFilesAreDataAndNeverAnException) {
    // A hostile or merely broken character must not be able to throw through a
    // match-start path. Every one of these returns false with a message.
    struct Case { const char* what; const char* text; };
    const Case cases[] = {
        { "empty",              "" },
        { "garbage",            "this is not json" },
        { "truncated",          "{\"name\": \"x\"," },
        { "array at top level", "[]" },
        { "no moves",           "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],\"moves\":[]}" },
        { "no resources",       "{\"name\":\"x\",\"moves\":[{\"id\":\"a\",\"startup\":1}]}" },
        { "startup is a string",
          "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
          "\"moves\":[{\"id\":\"a\",\"startup\":\"soon\"}]}" },
        { "negative startup",
          "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
          "\"moves\":[{\"id\":\"a\",\"startup\":-1}]}" },
        { "duplicate move id",
          "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
          "\"moves\":[{\"id\":\"a\",\"startup\":1},{\"id\":\"a\",\"startup\":2}]}" },
        { "undeclared resource in a guard",
          "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
          "\"moves\":[{\"id\":\"a\",\"startup\":1,\"guard\":{\"ki\":3}}]}" },
        { "cancel to a move that does not exist",
          "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
          "\"moves\":[{\"id\":\"a\",\"startup\":1}],"
          "\"cancels\":[{\"from\":\"a\",\"to\":\"ghost\"}]}" },
        { "starter that does not exist",
          "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
          "\"moves\":[{\"id\":\"a\",\"startup\":1}],\"starters\":[\"ghost\"]}" },
        { "sub-unit scale changed underneath us",
          "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
          "\"moves\":[{\"id\":\"a\",\"startup\":1}],"
          "\"engine\":{\"units\":{\"sub_units_per_pixel\":128}}}" },
    };

    for (const auto& k : cases) {
        CharacterData c{};
        LoadReport r{};
        LoadOptions o;                       // no expected resource order: these are shape tests
        EXPECT_FALSE(LoadCharacterJson("hostile.json", k.text, o, c, r))
            << "accepted a malformed character: " << k.what;
        EXPECT_FALSE(r.error.empty()) << k.what << " was rejected without saying why";
        EXPECT_TRUE(mentions(r.error, "hostile.json"))
            << k.what << ": the message does not name the file -- " << r.error;
        EXPECT_TRUE(c.moves.empty())
            << k.what << ": a rejected load handed back a half-built character";
    }

    // And the positive control for the whole block: a minimal WELL-formed
    // character does load, so the twelve rejections above are about the input.
    CharacterData c{};
    LoadReport r{};
    LoadOptions o;
    EXPECT_TRUE(LoadCharacterJson(
        "minimal.json",
        "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
        "\"moves\":[{\"id\":\"a\",\"startup\":1,\"hitstun\":10}],"
        "\"cancels\":[{\"from\":\"a\",\"to\":\"a\",\"delay\":3}]}",
        o, c, r)) << r.error;
    EXPECT_EQ(c.id, "minimal");
    EXPECT_EQ(c.moves.size(), 1u);
    EXPECT_EQ(c.cancels.size(), 1u);
}

TEST(UntrustedContent, ACancelWithNoDelayAndNoKindIsWarnedAbout) {
    // Delay 0 is the most permissive edge this vocabulary can express, and
    // ADR-001's hardest finding was that assuming it invents free frames on every
    // chain. A file that omits the delay still loads -- that is what the
    // reference loader does -- but it does not get to do so silently.
    CharacterData c{};
    LoadReport r{};
    LoadOptions o;
    ASSERT_TRUE(LoadCharacterJson(
        "quiet.json",
        "{\"name\":\"x\",\"resources\":[{\"name\":\"meter\"}],"
        "\"moves\":[{\"id\":\"a\",\"startup\":1,\"active\":2,\"recovery\":5,\"hitstun\":10}],"
        "\"cancels\":[{\"from\":\"a\",\"to\":\"a\"},{\"from\":\"a\",\"to\":\"a\",\"kind\":\"link\"}]}",
        o, c, r)) << r.error;

    ASSERT_EQ(c.cancels.size(), 2u);
    EXPECT_EQ(c.cancels[0].delay, 0);
    // `link` is shorthand for active + recovery - 1 of the SOURCE move: 2 + 5 - 1.
    EXPECT_EQ(c.cancels[1].delay, 6);

    bool warned = false;
    for (const auto& w : r.warnings) warned = warned || mentions(w, "permissive");
    EXPECT_TRUE(warned) << "a delay-0-by-omission edge was accepted without a word";
}
