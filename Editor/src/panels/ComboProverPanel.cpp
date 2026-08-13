#include "ComboProverPanel.h"
#include "imgui.h"

// Note 7. The kernel-side half of the resource check comes from the bridge that
// performs the projection, and it is included HERE rather than in the panel's
// header on purpose: MatchBuilder.h includes cse/kernel/Combat.h for the struct
// definitions, and a panel header that drags the kernel's types into every
// translation unit including it would be the same widening this file has spent
// two headers avoiding. CseData already links into the Editor for the prover, so
// this costs no new library -- and it cannot make the Editor link CseKernel,
// because MatchBuilder calls no kernel function (Data/CMakeLists.txt asserts it).
#include "cse/data/MatchBuilder.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using cse::data::BuildLoss;
using cse::data::BuildLossDirection;
using cse::data::BuildOptions;
using cse::data::BuildReport;
using cse::data::Cancel;
using cse::data::CharacterData;
using cse::data::GapAction;
using cse::data::LoadOptions;
using cse::data::LossDirection;
using cse::data::Move;
using cse::data::MoveIndex;
using cse::data::ProjectionLoss;
using cse::data::ProverDeadCancel;
using cse::data::ProverStatus;
using cse::data::RankingAbsence;
using cse::data::ResourceAmount;
using cse::data::ResourceDef;
using cse::data::ResourceIndex;

namespace {

// The same three colours the Inspector uses for the same three meanings. A
// panel that invented its own palette would read as a different application.
const ImVec4 kBad (1.00f, 0.35f, 0.30f, 1.f);
const ImVec4 kWarn(1.00f, 0.60f, 0.20f, 1.f);
const ImVec4 kGood(0.40f, 0.90f, 0.40f, 1.f);

// Almost every explanatory line on this page is a SENTENCE rather than a label,
// and ImGui offers wrapping (TextWrapped) or dimming (TextDisabled) but never
// both. Sentences that do not wrap leave the page unreadable the moment the
// panel is docked narrow, which is how it will normally be docked. These two
// helpers are that missing combination; both pass the text through "%s" so a
// character id or a loader message containing a percent sign cannot be read as
// a format specifier.
void textDim(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
void textLoud(const ImVec4& colour, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// The right edge of the content region, in the same screen coordinates
// GetItemRectMax reports. GetWindowContentRegionMax() would be the obvious
// call and it is OBSOLETED in this ImGui (1.91, imgui.h:481) -- it survives
// only while IMGUI_DISABLE_OBSOLETE_FUNCTIONS is undefined, which is not a
// guarantee worth building a layout on.
float contentRightEdge() {
    return ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
}

// --- the fingerprint -------------------------------------------------------
//
// See note 5 in the header. This is a hash, not a checksum: it never leaves the
// process, never reaches a peer and never enters a determinism comparison, so
// the only property it needs is that a change to anything the analysis reads
// changes it. It is deliberately NOT one of the kernel's hashes -- reusing a
// wire-visible hash here would invite someone to conclude this value means
// something across machines, and it does not.
inline void mix(std::uint64_t& h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
}
inline void mixI(std::uint64_t& h, std::int64_t v) {
    mix(h, static_cast<std::uint64_t>(v));
}
inline void mixStr(std::uint64_t& h, const std::string& s) {
    mix(h, s.size());
    for (unsigned char ch : s) mix(h, ch);
}
inline void mixAmounts(std::uint64_t& h, const std::vector<ResourceAmount>& v) {
    mix(h, v.size());
    for (const ResourceAmount& a : v) { mix(h, a.resource); mixI(h, a.value); }
}

std::uint64_t fingerprintOf(const CharacterData& c,
                            const cse::data::ProverOptions& o,
                            std::uint32_t nonce) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    mix(h, nonce);

    // The options are part of the input. Folding them in here is what makes the
    // budget buttons work without a second "please re-run" path to rot.
    mixI(h, o.limit);
    mixI(h, o.ceilingReplayRounds);
    mixI(h, o.ceilingReplayFrontier);
    mix(h, o.expectedResources.size());
    for (const std::string& r : o.expectedResources) mixStr(h, r);

    mixStr(h, c.id);
    mixStr(h, c.name);
    mixStr(h, c.stage);          // not read by the search, but reported as a loss

    mix(h, c.scalingPermille.size());
    for (std::int32_t s : c.scalingPermille) mixI(h, s);
    mix(h, static_cast<std::uint64_t>(c.decay.kind));
    mixI(h, c.decay.step);
    mixI(h, c.decay.floor);
    mix(h, c.decay.tablePermille.size());
    for (std::int32_t t : c.decay.tablePermille) mixI(h, t);

    // Resource ORDER, not merely contents: it is a build-wide contract, and a
    // reordering with identical names is exactly the edit that must re-run.
    mix(h, c.resources.size());
    for (const ResourceDef& r : c.resources) {
        mixStr(h, r.name);
        mixI(h, r.initial);
        mixI(h, r.floor);
        mixI(h, r.ceiling);
        mix(h, r.hasCeiling ? 1u : 0u);
    }

    mix(h, c.moves.size());
    for (const Move& m : c.moves) {
        mixStr(h, m.id);
        mixI(h, m.startup);
        mixI(h, m.active);
        mixI(h, m.recovery);
        mixI(h, m.hitstun);
        mixI(h, m.damageHundredths);
        // reachSub is here because kNoReach is the projectile-contact-frame
        // check's only signal, and that check is the one that can raise
        // soundnessAlarm. Dropping it would let the alarm go stale.
        mixI(h, m.reachSub);
        // The rest of this move feeds only the projection-loss counts. A loss
        // count IS part of the answer -- it is what tells a designer whether the
        // verdict above it can be trusted -- so an edit to one has to re-run.
        mix(h, static_cast<std::uint64_t>(m.stance));
        mix(h, m.hasCancelWindow ? 1u : 0u);
        mixStr(h, m.hitConditionProse);
        mix(h, m.hits.size());
        mixAmounts(h, m.effect);
        mixAmounts(h, m.guard);
        // DELIBERATELY ABSENT: pushbackSub, and it is the interesting omission.
        // It is the MIDSCREEN model's input; the corner analysis cannot observe
        // it, so folding it in would re-run the whole search on an edit that
        // provably cannot change the answer. The day ProverStage gains a second
        // member, this comment becomes a mixI.
        // Absent for the duller reason -- nothing reads them -- are label,
        // stateId, animId, variant, escapeHatchKind and motion. Labels are read
        // live from the character rather than from the cached result, so a
        // renamed label is never stale on screen.
    }

    mix(h, c.cancels.size());
    for (const cse::data::Cancel& e : c.cancels) {
        mix(h, e.from);
        mix(h, e.to);
        mixI(h, e.delay);
        mix(h, static_cast<std::uint64_t>(e.on));
        mix(h, e.certain ? 1u : 0u);   // counted into the `cancel.certain` loss
        mixAmounts(h, e.effect);
        mixAmounts(h, e.guard);
    }

    mix(h, c.starters.size());
    for (MoveIndex s : c.starters) mix(h, s);

    // Gap actions reach the answer through exactly one number -- how many of
    // them move a RESOURCE -- and that number is a CONSERVATIVE loss, the kind
    // that can hide an infinite. It must not go stale.
    mix(h, c.gapActions.size());
    for (const GapAction& g : c.gapActions) {
        mix(h, g.enabled ? 1u : 0u);
        mixAmounts(h, g.effect);
    }
    return h;
}

// --- small formatting helpers ----------------------------------------------

// Damage is HUNDREDTHS everywhere in CharacterData so that no float survives
// into anything a tick reads. Printing it as %d.%02d keeps that promise on the
// way out too; a %f here would be the only float on the path.
void damageText(std::int32_t hundredths, char* buf, std::size_t n) {
    const std::int32_t whole = hundredths / 100;
    const std::int32_t frac  = hundredths % 100;
    std::snprintf(buf, n, "%d.%02d", static_cast<int>(whole),
                  static_cast<int>(frac < 0 ? -frac : frac));
}

// The four absences are four different pieces of news, and ADR-001 section 3
// gate 3 is emphatic that collapsing them throws away the strongest result the
// tool can produce. One sentence each, written for a designer rather than for
// someone who has read comboprover.hpp.
const char* rankingGloss(RankingAbsence absence) {
    switch (absence) {
    case RankingAbsence::Present:
        return "Every combo strictly runs one of these down, so this is a PROOF "
               "of termination rather than a bound the search happened to reach.";
    case RankingAbsence::NoResources:
        return "The character declares no resources, so there is nothing for a "
               "certificate to rank. Termination above rests on the frame "
               "arithmetic alone -- a weaker result, but not a worse one.";
    case RankingAbsence::CharacterGainsAResource:
        return "EXPECTED, and not bad news. A certificate is only defined for "
               "the spend-only fragment, and any character that builds meter on "
               "hit leaves that fragment. This says nothing about the verdict "
               "above; do not read the missing certificate as doubt.";
    case RankingAbsence::NothingLoops:
        return "The STRONGEST result this tool can produce: not one cancel in "
               "the character connects between two reachable moves, so nothing "
               "can loop at all. If that is a surprise, the dead-cancel list "
               "below is where the character actually is.";
    case RankingAbsence::NoDescendingOrder:
        return "Spend-only, and live edges exist, but no single resource runs "
               "strictly down across all of them. Termination was decided by "
               "exhausting the search rather than by a certificate.";
    }
    return "";
}

// --- note 7: the loss entries that are about resources -----------------------
//
// MatchBuilder's table is keyed by string, so this list is a contract with
// `recordLosses` in Data/src/MatchBuilder.cpp and it is the one part of this
// check that could rot without saying anything. checkResourceGap_ counts how
// many of the five it actually found, and finding none is reported as a broken
// check rather than as an absence of losses -- a silent "nothing dropped" here
// would be the most expensive wrong sentence on the page.
//
// Why these five and not the whole table: they are exactly the entries whose
// inputs the note-5 fingerprint covers -- effect and guard amounts on moves and
// cancels, and the resource declarations. `move.pushback` is deliberately not
// fingerprinted, because the corner search cannot observe it, so a panel that
// printed its count would be printing a number the cache is allowed to let go
// stale.
const char* const kResourceLossFields[] = {
    "resources",      // the pools themselves, their floors and their ceilings
    "move.effect",    // what spends them and what banks them
    "move.guard",     // what they gate
    "cancel.effect",
    "cancel.guard",
};

const BuildLoss* findLoss(const BuildReport& report, const char* field) {
    for (const BuildLoss& loss : report.losses)
        if (loss.field == field) return &loss;
    return nullptr;
}

// "meter, juggle" -- resource indices spelled with the character's own names,
// carrying the same out-of-range guard the ranking order uses above. An index
// that does not fit the character must reach the screen as visibly wrong text
// rather than as a plausible name.
std::string resourceNames(const CharacterData& c,
                          const std::vector<ResourceIndex>& list) {
    std::string out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i) out += ", ";
        out += list[i] < c.resources.size() ? c.resources[list[i]].name
                                            : std::string("<bad resource index>");
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Loading and running
// ---------------------------------------------------------------------------

void ComboProverPanel::loadFromPath_() {
    loadAttempted_ = true;
    LoadOptions lo;
    lo.expectedResources = expectedResources_;
    CharacterData fresh;
    cse::data::LoadReport rep;
    if (cse::data::LoadCharacterFile(contentRoot_, pathBuf_, lo, fresh, rep)) {
        owned_       = std::move(fresh);
        ownedReport_ = std::move(rep);
        ownedLoaded_ = true;
    } else {
        // Keep the previous character on screen rather than blanking the page. A
        // designer who typos a path should lose the typo, not the verdict they
        // were in the middle of reading. The error is shown either way.
        ownedReport_ = std::move(rep);
    }
}

void ComboProverPanel::runIfStale_(const CharacterData& character) {
    options_.expectedResources = expectedResources_;
    const std::uint64_t now = fingerprintOf(character, options_, forceNonce_);
    if (haveResult_ && now == fingerprint_) return;

    const auto t0 = std::chrono::steady_clock::now();
    resultOk_ = cse::data::AnalyseCharacter(character, options_, result_, report_);
    const auto t1 = std::chrono::steady_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ++runs_;
    lastRunMs_   = ms;
    totalRunMs_ += ms;
    if (ms > worstRunMs_) worstRunMs_ = ms;

    // Note 7, timed separately. It runs HERE rather than in the draw path for
    // the reason note 5 gives about the search itself: it is work proportional
    // to the character, and a docked panel draws far more often than a designer
    // types. Inside runIfStale_ it is cached against the same fingerprint as the
    // verdict it qualifies, so the two can never be on screen describing
    // different characters.
    const auto g0 = std::chrono::steady_clock::now();
    checkResourceGap_(character);
    const auto g1 = std::chrono::steady_clock::now();
    lastGapMs_ = std::chrono::duration<double, std::milli>(g1 - g0).count();

    fingerprint_ = now;
    haveResult_  = true;
}

// The three questions note 7 sets out, in order, each answered from a different
// thing this panel can honestly reach. Nothing here remembers what the kernel
// does or does not implement: the day it grows resources, the bridge stops
// reporting the drops and this whole section turns itself off.
void ComboProverPanel::checkResourceGap_(const CharacterData& c) {
    gap_ = ResourceGap{};

    // Only TERMINATING is worth qualifying. INFINITE is not made worse by an
    // engine more permissive than the model -- the designer is already being
    // told to go and break a link -- and UNRESOLVED never proved a bound for a
    // resource to have been holding up.
    if (!resultOk_ || result_.status != ProverStatus::Terminating) return;
    gap_.ran = true;

    // --- 1. WHAT THE VERDICT RESTS ON ---------------------------------------
    //
    // Which resources this character actually moves. A zero delta and a zero
    // minimum are both no-ops, so they do not count: a file that authors
    // `{ "meter": 0 }` for tidiness must not read as a file that depends on
    // meter.
    std::vector<bool> touched(c.resources.size(), false);
    auto note = [&touched](const std::vector<ResourceAmount>& amounts) {
        bool any = false;
        for (const ResourceAmount& a : amounts) {
            if (a.value == 0 || a.resource >= touched.size()) continue;
            touched[a.resource] = true;
            any = true;
        }
        return any;
    };
    for (const Move& m : c.moves) { note(m.effect); note(m.guard); }
    for (const Cancel& e : c.cancels) {
        const bool effect = note(e.effect);
        const bool guard  = note(e.guard);
        if (effect || guard) ++gap_.edgesWithResourceData;
    }

    // The certificate is the strong statement and is taken VERBATIM: it is what
    // the tool proved, and filtering it against the scan above would let this
    // panel quietly disagree with the verdict it is standing next to. Without a
    // certificate the honest statement is weaker -- these are the resources that
    // were in play while the search decided -- and note 3 is why that case is
    // handled at all rather than the page being built around the certificate.
    if (result_.hasRanking && !result_.rankingOrder.empty()) {
        gap_.certified = true;
        gap_.restsOn   = result_.rankingOrder;
    } else {
        for (std::size_t r = 0; r < touched.size(); ++r)
            if (touched[r]) gap_.restsOn.push_back(static_cast<ResourceIndex>(r));
    }
    if (gap_.restsOn.empty()) return;   // nothing rests on a resource; say so and stop

    // The moves that spend one of them. Reported as move indices so they can be
    // drawn with moveButton_ like every other move on this page -- "which move
    // is doing this" is the first question, and a name you cannot click is an
    // answer you have to go and look up.
    for (std::size_t m = 0; m < c.moves.size(); ++m) {
        bool spends = false;
        for (const ResourceAmount& a : c.moves[m].effect) {
            if (a.value >= 0) continue;
            for (ResourceIndex r : gap_.restsOn)
                if (a.resource == r) { spends = true; break; }
            if (spends) break;
        }
        if (spends) gap_.spenders.push_back(static_cast<MoveIndex>(m));
    }

    // --- 2. WHAT THE RUNNING GAME CARRIES -----------------------------------
    //
    // Asked of the bridge that performs the projection, so that the answer is
    // MatchBuilder's rather than this panel's. The build is thrown away
    // immediately: the only thing wanted from it is the loss table.
    //
    // BODY AND BINDINGS ARE LEFT EMPTY DELIBERATELY, and their warnings are not
    // shown. A panel has neither -- the schema authors no body and no input
    // notation, which is exactly why BuildOptions exists -- and neither can
    // change a resource loss: the body decides a hurtbox and the bindings decide
    // which buttons start what. Surfacing "no body supplied, defaulting to 13 x
    // 60 px" here would be a warning about a match nobody is going to fight.
    {
        BuildOptions options;
        cse::kernel::FighterData fighter{};
        cse::data::MoveIndexMap  map;
        BuildReport              report;
        gap_.bridgeRan = cse::data::BuildFighterData(c, options, fighter, map, report);
        if (!gap_.bridgeRan) {
            // A character the prover analysed and the bridge REFUSES is worth
            // seeing in its own right: over 31 moves it cannot be built at all,
            // so the verdict describes a character that could never reach a
            // tick. Reported rather than swallowed.
            gap_.bridgeError = report.error;
        } else {
            gap_.playsAsAnalysed = report.playsAsAnalysed;
            for (const char* field : kResourceLossFields) {
                const BuildLoss* loss = findLoss(report, field);
                if (!loss) continue;              // the name moved -- counted by absence
                ++gap_.resourceLossesFound;
                if (loss->count == 0) continue;
                if (loss->direction == BuildLossDirection::Exact) continue;
                ResourceGap::Drop drop;
                drop.field     = loss->field;
                drop.direction = cse::data::BuildLossDirectionName(loss->direction);
                drop.count     = loss->count;
                gap_.drops.push_back(std::move(drop));
            }
        }
    }

    // --- 3. WHETHER IT ACTUALLY BITES ---------------------------------------
    //
    // The same decision procedure, over the same character, with every move and
    // cancel effect and guard emptied. THE RESOURCE DECLARATIONS ARE LEFT IN
    // PLACE, and that is the whole fidelity of the experiment: "the pool exists
    // and nothing moves it" is not an approximation of the kernel, it is a
    // description of it -- `Fighter::meter` is a field of GameState that no rule
    // reads or writes. Clearing the declarations too would additionally trip
    // A03, and would be answering a different question.
    //
    // No RebuildIndices() call: emptying an effect vector cannot move a move id
    // or an edge endpoint, so moveIndexById and cancelsFrom are still correct.
    // The copy is the price -- one character's worth of strings and vectors, on
    // a path that already runs a search, and only when a resource is in play.
    //
    // ITS COST DOES NOT SIMPLY DOUBLE THE FOOTER'S NUMBER, in either direction.
    // Two things move at once: the resource vector is part of the search state,
    // so emptying every effect collapses it to a single value (much smaller),
    // while removing guards stops edges being refused (larger). On `fighter_a`
    // the primary search explores 552 configurations and this one 124. Either
    // way it is bounded by the same ProverOptions::limit, and a run that reaches
    // the cap comes back Unknown -- drawn as "cannot tell you" rather than
    // quietly downgraded to good news.
    {
        CharacterData stripped = c;
        for (Move& m : stripped.moves)      { m.effect.clear(); m.guard.clear(); }
        for (Cancel& e : stripped.cancels)  { e.effect.clear(); e.guard.clear(); }

        cse::data::ProverResult counterfactual;
        cse::data::ProverReport report;
        if (cse::data::AnalyseCharacter(stripped, options_, counterfactual, report)) {
            gap_.counterfactualRan    = true;
            gap_.counterfactualStatus = counterfactual.status;
            gap_.counterfactualLoop   = counterfactual.loop;
        } else {
            gap_.counterfactualError = report.error;
        }
    }
}

// ---------------------------------------------------------------------------
// Pieces of the page
// ---------------------------------------------------------------------------

void ComboProverPanel::moveButton_(const CharacterData& c, MoveIndex m,
                                   ComboProverActions& actions) {
    if (m >= c.moves.size()) {
        // Not decoration, and not unreachable-by-construction either: a cached
        // result whose indices no longer fit the character is precisely the
        // failure the fingerprint exists to prevent. If it ever reaches the
        // screen it must arrive in red, not as a plausible-looking move name.
        ImGui::TextColored(kBad, "<bad move index %u>", static_cast<unsigned>(m));
        return;
    }
    const Move& move = c.moves[m];
    ImGui::PushID(widgetId_++);
    if (ImGui::SmallButton(move.id.c_str())) actions.jumpToMoveId = move.id;
    if (ImGui::IsItemHovered()) {
        char dmg[32];
        damageText(move.damageHundredths, dmg, sizeof(dmg));
        ImGui::SetTooltip("%s\n%d startup / %d active / %d recovery\n"
                          "%d hitstun, %s damage",
                          move.label.empty() ? move.id.c_str() : move.label.c_str(),
                          static_cast<int>(move.startup), static_cast<int>(move.active),
                          static_cast<int>(move.recovery), static_cast<int>(move.hitstun),
                          dmg);
    }
    ImGui::PopID();
}

// A bare list of moves, wrapped to the panel width. Factored out of the
// unreachable-move list when note 7's "spent by" list wanted exactly the same
// thing: two copies of a measuring loop drift, and the one that drifts is
// always the one nobody is looking at. It is NOT drawSequence_ -- these moves
// are a set, and the arrows drawSequence_ puts between them would claim an
// order that does not exist.
void ComboProverPanel::drawMoveButtons_(const CharacterData& c,
                                        const std::vector<MoveIndex>& moves,
                                        ComboProverActions& actions) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float visibleX = contentRightEdge();
    for (std::size_t i = 0; i < moves.size(); ++i) {
        moveButton_(c, moves[i], actions);
        if (i + 1 >= moves.size()) break;
        const MoveIndex nx = moves[i + 1];
        const char* nextId = nx < c.moves.size() ? c.moves[nx].id.c_str()
                                                 : "<bad move index>";
        const float nextW = ImGui::CalcTextSize(nextId).x +
                            style.FramePadding.x * 2.f + style.ItemSpacing.x;
        if (ImGui::GetItemRectMax().x + nextW < visibleX) ImGui::SameLine(0.f, 4.f);
    }
}

void ComboProverPanel::drawSequence_(const CharacterData& c,
                                     const std::vector<MoveIndex>& sequence,
                                     ComboProverActions& actions) {
    // ImGui does not wrap SameLine content, and a twelve-move loop in a docked
    // panel runs straight off the right edge, where it can be neither read nor
    // clicked. So the next item's width is MEASURED before deciding to stay on
    // the line; when it will not fit the line simply breaks, and the reading
    // order carries on left to right, top to bottom.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float visibleX = contentRightEdge();
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        moveButton_(c, sequence[i], actions);
        if (i + 1 >= sequence.size()) break;

        const MoveIndex nextIndex = sequence[i + 1];
        const char* nextId = nextIndex < c.moves.size() ? c.moves[nextIndex].id.c_str()
                                                        : "<bad move index>";
        const float arrowW = ImGui::CalcTextSize(">").x + style.ItemSpacing.x * 2.f;
        const float nextW  = ImGui::CalcTextSize(nextId).x + style.FramePadding.x * 2.f;
        if (ImGui::GetItemRectMax().x + arrowW + nextW < visibleX) {
            ImGui::SameLine(0.f, 4.f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.f, 4.f);
        }
    }
}

void ComboProverPanel::drawSource_(const CharacterData* external) {
    if (external) {
        textDim("Character supplied by the editor.");
        return;
    }

    ImGui::SetNextItemWidth(-60.f);
    // Typing does not load. A half-typed path is a load failure per keystroke,
    // and a panel that flashes red while you type teaches you to ignore red.
    ImGui::InputText("##charpath", pathBuf_, sizeof(pathBuf_));
    ImGui::SameLine(0.f, 6.f);
    if (ImGui::SmallButton("Load")) loadFromPath_();
    ImGui::TextDisabled("relative to \"%s\"", contentRoot_.c_str());
    textDim("Absolute paths, drive roots and any \"..\" component are refused "
            "lexically, before the file is opened. Authored content is untrusted "
            "and a panel where a human types a path is the definition of it.");

    if (!ownedReport_.error.empty()) {
        if (!ownedReport_.rule.empty())
            ImGui::TextColored(kBad, "%s", ownedReport_.rule.c_str());
        textLoud(kBad, ownedReport_.error.c_str());
    }
    for (const std::string& w : ownedReport_.warnings) textLoud(kWarn, w.c_str());
}

void ComboProverPanel::drawStageBanner_(const CharacterData& character) {
    // ABOVE the verdict, and never inside a collapsing header. See note 1 in the
    // header: the same character is TERMINATING midscreen and INFINITE in the
    // corner, so a verdict with no stage on it is a coin flip.
    ImGui::SeparatorText("Where the fighters are standing");
    textLoud(kWarn, "CORNER -- the defender is pinned against the wall.");
    textDim("Distance and walk-forward are not modelled. The corner is the "
            "attacker's best case: a combo that dies here dies everywhere, and "
            "one that loops here need not loop midscreen. Away from the wall "
            "this verdict is an under-approximation.");

    if (!character.stage.empty() && character.stage != "corner") {
        ImGui::TextColored(kWarn, "This file declares stage \"%s\".",
                           character.stage.c_str());
        textDim("That is a DIFFERENT question, and it is not the one answered "
                "below. Phase 0 ran Kung Fu Man both ways and got TERMINATING "
                "midscreen and INFINITE in the corner -- both correct.");
    }

    ImGui::TextDisabled("Midscreen is not available in-engine.");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "The midscreen model lives in Python (ADR-001 section 3, gate 4),\n"
            "and its pushback constant is ESTIMATED rather than derived: MUGEN\n"
            "records a velocity and a friction and never a displacement. A 20%%\n"
            "error in that estimate flips one of the three Phase-0 verdicts, so\n"
            "midscreen answers would have to be marked conditional even once\n"
            "they exist. Corner answers do not depend on it, which is why they\n"
            "are the ones shipped here.");
    }
}

void ComboProverPanel::drawVerdict_(const CharacterData& character,
                                    ComboProverActions& actions) {
    ImGui::SeparatorText("Verdict");

    if (!resultOk_) {
        // A character that could not be PROJECTED is a different thing from a
        // search that could not decide, and the panel must not let the two look
        // alike. This branch is the former; UNRESOLVED below is the latter.
        if (!report_.rule.empty()) ImGui::TextColored(kBad, "%s", report_.rule.c_str());
        textLoud(kBad, report_.error.empty()
                           ? "No verdict: the character could not be projected."
                           : report_.error.c_str());
        return;
    }

    for (const std::string& w : report_.warnings) {
        // A skipped A03 lands here, and it means the verdict may have compared
        // juggle points against meter and said nothing about it. Not a footnote.
        textLoud(kWarn, w.c_str());
    }

    switch (result_.status) {
    case ProverStatus::Infinite:
        ImGui::TextColored(kBad, "INFINITE COMBO");
        break;
    case ProverStatus::Terminating:
        // No green tick this panel cannot back. The soundness alarm is exactly
        // the case where TERMINATING is not a clean bill of health, so the
        // colour changes with it rather than after it in small print.
        if (result_.soundnessAlarm)
            ImGui::TextColored(kWarn, "TERMINATING -- but see the soundness alarm below");
        else
            ImGui::TextColored(kGood, "TERMINATING");
        break;
    case ProverStatus::Unknown:
        ImGui::TextColored(kWarn, "UNRESOLVED -- the search hit its budget");
        break;
    }
    if (!result_.reason.empty()) ImGui::TextWrapped("%s", result_.reason.c_str());

    if (result_.status == ProverStatus::Infinite) {
        ImGui::Spacing();
        textDim("The loop, as a move sequence. Click a move to reveal it.");
        if (!result_.prefix.empty()) {
            ImGui::TextUnformatted("get in:");
            drawSequence_(character, result_.prefix, actions);
        }
        ImGui::TextUnformatted("loop:");
        drawSequence_(character, result_.loop, actions);
        textDim("The loop ends on the move it returns to, and repeats from there.");

        if (!result_.loopEntryKnown) {
            textLoud(kWarn,
                "The opening move is missing. When the loop is not entered on "
                "the very first move, comboprover.hpp:511-517 omits that move "
                "from both lists, so the sequence above starts mid-combo. The "
                "verdict is unaffected; the transcription is incomplete.");
        }

        // The ceiling replay is the adapter's own correctness work, and it is
        // the difference between "this loop exists in the model" and "this loop
        // exists in the game". It belongs next to the loop, not buried with the
        // projection losses.
        if (!result_.ceilingReplayRan || !result_.loopEntryKnown) {
            textDim("Ceilings: not replayed. No starter affordable from the "
                    "clamped initial vector joins this witness, so the opening "
                    "move could not be recovered.");
        } else if (result_.loopHoldsUnderCeilings) {
            textLoud(kBad,
                "Ceilings: the loop still returns to a position no worse than it "
                "started, with every resource capped. This infinite is real "
                "under the character's declared ceilings.");
        } else {
            textLoud(kWarn,
                "Ceilings: the loop could NOT be reproduced once resources were "
                "capped. The verdict stands -- a failed replay proves nothing in "
                "the other direction -- but this particular witness may rest on "
                "meter the game would never have handed out.");
        }
    }

    if (result_.status == ProverStatus::Terminating) {
        char dmg[32];
        damageText(result_.maxDamageHundredths, dmg, sizeof(dmg));
        ImGui::Text("Worst case: %d hits, %d frames, %s damage",
                    static_cast<int>(result_.maxHits),
                    static_cast<int>(result_.maxFrames), dmg);

        if (result_.hasRanking) {
            ImGui::TextColored(kGood, "Ranking certificate: present.");
            std::string order;
            for (std::size_t i = 0; i < result_.rankingOrder.size(); ++i) {
                if (i) order += ", ";
                const ResourceIndex r = result_.rankingOrder[i];
                order += r < character.resources.size()
                             ? character.resources[r].name
                             : std::string("<bad resource index>");
            }
            ImGui::TextWrapped("These run down, in order: %s", order.c_str());
        } else {
            ImGui::Text("Ranking certificate: absent -- %s",
                        cse::data::RankingAbsenceName(result_.rankingAbsence));
        }
        // Glossed in the present case too: a designer who has never met a
        // ranking certificate needs to be told what HAVING one means every bit
        // as much as what missing one means.
        textDim(rankingGloss(result_.rankingAbsence));

        // Note 7, drawn HERE: inside the verdict block, under the certificate it
        // qualifies, with nothing collapsible between them. TERMINATING over a
        // certificate reading "meter, juggle" is the most trustworthy-looking
        // thing this page can print and today it is the least trustworthy thing
        // this page can print, and those two sentences have to arrive together.
        //
        // It is a SECOND axis, not a louder soundness alarm. The alarm above is
        // the model possibly being wrong about the file; this is the model being
        // right about the file and the game being a different character. Both
        // can be up at once and neither implies the other -- on `fighter_a` they
        // are: its alarm is raised by `projectile contact frame, outgoing`.
        drawResourceGap_(character, actions);
    }

    if (result_.status == ProverStatus::Unknown) {
        ImGui::Text("Explored %d configurations, then stopped at the budget of %d.",
                    static_cast<int>(result_.explored),
                    static_cast<int>(options_.limit));
        textDim("A budget statement, not an error and NOT a clean bill of "
                "health: the search was still finding new positions when it was "
                "cut off, so this character may or may not have an infinite.");
        if (ImGui::SmallButton("Raise budget x2")) options_.limit *= 2;
        ImGui::SameLine(0.f, 6.f);
        if (ImGui::SmallButton("x10"))             options_.limit *= 10;
        ImGui::SameLine(0.f, 6.f);
        if (ImGui::SmallButton("Reset budget"))    options_.limit = 200000;
        textDim("Phase 0 measured 63 / 79 / 10 configurations on the three real "
                "characters, so reaching 200000 means something is pathological "
                "rather than large. Raise it once; if it caps again, read the "
                "cancel list rather than the budget.");
    }
    else if (result_.capped) {
        // Vacuity guard. `capped` is what produces UNRESOLVED, so seeing it
        // beside a decided verdict means the adapter and this panel disagree
        // about what the search did. Quietly ignoring that would leave a verdict
        // on screen that nobody should trust, looking exactly like one they
        // should.
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      "INCONSISTENT: the search reported hitting its budget but "
                      "still returned %s. Do not trust this verdict; this is a bug.",
                      cse::data::ProverStatusName(result_.status));
        textLoud(kBad, msg);
    }
}

// Note 7 on the page. Three paragraphs in the order the check computes them:
// what the verdict rests on, what the running game carries, and whether the
// difference reaches this character. Each one is drawn from the corresponding
// field of gap_ and none of them is drawn from a memory of what the kernel does.
void ComboProverPanel::drawResourceGap_(const CharacterData& c,
                                        ComboProverActions& actions) {
    if (!gap_.ran) return;

    ImGui::Spacing();

    // A check that ran and found nothing is not the same as a check that does
    // not exist -- the argument the projection table already makes for keeping
    // its count-0 rows -- so the empty case costs one dim line rather than
    // silence. It is also the good news: this character cannot be exposed to the
    // one gap that is measured rather than suspected.
    if (gap_.restsOn.empty()) {
        textDim("Resources: checked. No move or cancel in this character spends "
                "one or requires one, so nothing in this verdict can be resting "
                "on a resource.");
        return;
    }

    const std::string names = resourceNames(c, gap_.restsOn);
    const bool kernelDrops  = gap_.bridgeRan && gap_.resourceLossesFound > 0 &&
                              !gap_.drops.empty();
    const bool flips        = gap_.counterfactualRan &&
                              gap_.counterfactualStatus == ProverStatus::Infinite;

    char line[640];

    // --- what the verdict rests on ------------------------------------------
    if (gap_.certified) {
        std::snprintf(line, sizeof(line),
            "WHAT THIS VERDICT RESTS ON: the certificate above IS the "
            "termination argument, and it is arithmetic about %s. It is a "
            "statement about the file.", names.c_str());
        textLoud(kernelDrops && flips ? kBad : kWarn, line);
    } else {
        std::snprintf(line, sizeof(line),
            "WHAT THIS VERDICT RESTS ON: no certificate, so this is the weaker "
            "statement -- %s %s in play while the search decided, and how much "
            "of the bound %s carrying is not something this panel can tell you.",
            names.c_str(),
            gap_.restsOn.size() == 1 ? "was" : "were",
            gap_.restsOn.size() == 1 ? "it was" : "they were");
        textLoud(kWarn, line);
    }

    if (!gap_.spenders.empty()) {
        ImGui::TextDisabled("Spent by:");
        ImGui::SameLine(0.f, 6.f);
        drawMoveButtons_(c, gap_.spenders, actions);
    }
    if (gap_.edgesWithResourceData > 0) {
        ImGui::TextDisabled("%d cancel(s) carry resource data of their own.",
                            static_cast<int>(gap_.edgesWithResourceData));
    }

    // --- what the running game carries --------------------------------------
    if (!gap_.bridgeRan) {
        std::snprintf(line, sizeof(line),
            "THE KERNEL CANNOT BE HANDED THIS CHARACTER AT ALL. The bridge "
            "refused it, so what the projection drops is unknown -- and a "
            "verdict about a character that can never reach a tick is worth less "
            "than the refusal: %s",
            gap_.bridgeError.empty() ? "(the build reported no message)"
                                     : gap_.bridgeError.c_str());
        textLoud(kBad, line);
    } else if (gap_.resourceLossesFound == 0) {
        // Rot, reported as rot. The alternative -- an empty drop list read as
        // "nothing was dropped" -- would turn a renamed field in MatchBuilder
        // into a green light on the most dangerous claim this page makes.
        textLoud(kBad,
            "THIS CHECK IS BROKEN, NOT CLEAR. None of the loss entries it reads "
            "by name (resources, move.effect, move.guard, cancel.effect, "
            "cancel.guard) is in the bridge's table any more, so it has nothing "
            "to read. Treat everything below as unknown and re-read "
            "Data/src/MatchBuilder.cpp.");
    } else if (gap_.drops.empty()) {
        std::snprintf(line, sizeof(line),
            "The bridge reports the kernel carries this character's resource "
            "data: none of the %d resource entries in its loss table reports a "
            "difference. The argument above survives the projection into the game.",
            gap_.resourceLossesFound);
        textLoud(kGood, line);
    } else {
        textLoud(kBad, "THE RUNNING GAME DOES NOT CARRY IT. The bridge into the "
                       "kernel reports these drops:");
        ImGui::Indent();
        for (const ResourceGap::Drop& d : gap_.drops)
            ImGui::TextDisabled("%s x%d -- %s", d.field.c_str(),
                                static_cast<int>(d.count), d.direction.c_str());
        ImGui::Unindent();
        // Spelled from the DIRECTION rather than from any claim about the
        // kernel's internals, so this sentence cannot go stale behind the table
        // it is explaining.
        textDim("KernelOmits means the game does LESS than the file: a delta "
                "that is never applied. KernelPermits means it does MORE: a "
                "minimum that is never checked. Either way the resource "
                "arithmetic this verdict was computed from is not arithmetic the "
                "running game performs.");
    }
    if (gap_.bridgeRan && !gap_.playsAsAnalysed) {
        textDim("BuildReport::playsAsAnalysed is false for this character: the "
                "game differs from the file in other ways as well, and the "
                "projection table below is where they are counted.");
    }

    // --- and whether the difference reaches THIS character ------------------
    if (!gap_.counterfactualRan) {
        std::snprintf(line, sizeof(line),
            "Whether that reaches this character could not be measured: the same "
            "character with its resource effects emptied could not be projected "
            "(%s).",
            gap_.counterfactualError.empty() ? "the run reported no message"
                                             : gap_.counterfactualError.c_str());
        textLoud(kWarn, line);
        return;
    }

    switch (gap_.counterfactualStatus) {
    case ProverStatus::Terminating:
        textLoud(kGood,
            "Checked, and it does NOT reach this character: with every resource "
            "effect and guard emptied the verdict is still TERMINATING, so this "
            "character does not need the arithmetic the game is missing.");
        break;

    case ProverStatus::Unknown:
        textLoud(kWarn,
            "Whether it reaches this character is UNRESOLVED: the second search "
            "hit the same budget the verdict above uses. Raise the budget and "
            "this answers too -- it is the same search on the same options.");
        break;

    case ProverStatus::Infinite:
        if (kernelDrops) {
            std::snprintf(line, sizeof(line),
                "AND IT REACHES THIS CHARACTER. Asked the same question with "
                "every resource effect and guard emptied -- the pools still "
                "declared and nothing moving them, which is what the drops above "
                "amount to -- the decision procedure answers INFINITE COMBO. "
                "This is the loop the arithmetic about %s was holding back, and "
                "the game is not doing that arithmetic:", names.c_str());
            textLoud(kBad, line);
        } else {
            std::snprintf(line, sizeof(line),
                "For scale: with every resource effect and guard emptied this "
                "character is INFINITE, so %s really is load-bearing here. The "
                "bridge says the game carries it, so this is a measure of how "
                "much rests on that continuing to be true:", names.c_str());
            textLoud(kWarn, line);
        }
        drawSequence_(c, gap_.counterfactualLoop, actions);
        // WHAT THIS IS NOT, in the same breath. The second run is the decision
        // procedure asked a second question; it is not a simulation, and the
        // kernel differs in both directions at once -- it takes contact-gated
        // edges early and ignores stance, while an edge whose resolved window
        // came out empty is inert in the game and live here. Claiming the game
        // can perform THIS loop would be the same overreach in the opposite
        // direction from the one this whole section exists to catch.
        textDim("That is the same decision procedure asked a second question, "
                "not a simulation of the kernel: whether the game can perform "
                "this particular loop is a separate question, and the game "
                "differs in both directions at once (it ignores stance and takes "
                "contact-gated edges early; an edge whose window resolved empty "
                "is inert there and live here). What is settled is that the "
                "bound above is not a bound the game enforces.");
        break;
    }
}

void ComboProverPanel::drawDeadCancels_(const CharacterData& c,
                                        const std::vector<ProverDeadCancel>& list,
                                        ComboProverActions& actions) {
    if (list.empty()) {
        // Two very different empties, and telling them apart is the whole point
        // of the list. "No dead cancels" is good news only if there were cancels
        // to kill; with none authored, an empty list is an empty character.
        if (c.cancels.empty()) {
            textLoud(kWarn, "The character authors no cancels at all, so this "
                            "list is empty for a reason that is not good news.");
        } else {
            textLoud(kGood, "None -- every authored cancel can connect.");
        }
        return;
    }

    // Kung Fu Girl's pre-decay list is 134 entries long. Left unbounded it would
    // push the projection section and the footer off the bottom of the panel,
    // and the sections below it are not less important than the 90th dead
    // cancel, so a long list scrolls inside its own region.
    const float rowH   = ImGui::GetTextLineHeightWithSpacing();
    const bool  scroll = list.size() > 8;
    const float height = scroll ? rowH * 9.f : 0.f;
    ImGui::BeginChild("##deadcancels", ImVec2(0.f, height),
                      scroll ? ImGuiChildFlags_Border : ImGuiChildFlags_None);
    for (const ProverDeadCancel& d : list) {
        moveButton_(c, d.from, actions);
        ImGui::SameLine(0.f, 4.f);
        ImGui::TextDisabled("->");
        ImGui::SameLine(0.f, 4.f);
        moveButton_(c, d.to, actions);
        ImGui::SameLine(0.f, 8.f);
        ImGui::TextDisabled("leaves %d, needs %d, short by %d",
                            static_cast<int>(d.advantage), static_cast<int>(d.startup),
                            static_cast<int>(d.shortfall()));
    }
    ImGui::EndChild();
}

void ComboProverPanel::drawDaily_(const CharacterData& c, ComboProverActions& actions) {
    if (!resultOk_) return;

    // Deliberately NOT behind a collapsing header, and drawn directly under the
    // verdict. ADR-001: these are "the features a designer will actually use
    // daily, and they land before anyone cares about the theorem."
    ImGui::SeparatorText("What you can use today");

    ImGui::Text("Usable cancels: %d of %d authored.",
                static_cast<int>(result_.usableCancels),
                static_cast<int>(c.cancels.size()));
    ImGui::Text("Settling index: hit %d.", static_cast<int>(result_.settlingIndex));
    textDim("Every cancel below is judged at the hitstun the decay curve has "
            "settled to by that hit, which is how the prover judges them too -- "
            "so the two lists cannot drift apart.");

    ImGui::Spacing();
    ImGui::Text("Dead cancels: %d before decay, %d at the settled hitstun.",
                static_cast<int>(result_.deadCancelsPreDecay.size()),
                static_cast<int>(result_.deadCancels.size()));
    ImGui::Checkbox("show the settled list instead", &showSettledDeadCancels_);
    textDim(showSettledDeadCancels_
                ? "Settled: the model's own list. A decay curve reports real "
                  "cancels as dead here -- 128 of Kung Fu Girl's 134 under this "
                  "project's draft house rule -- so read it as a statement about "
                  "the curve as much as about the character."
                : "Pre-decay: the list that blames you only for what you "
                  "authored. ADR-001's amendment to section 5.3 asks for this "
                  "one by default, for exactly that reason.");
    drawDeadCancels_(c, showSettledDeadCancels_ ? result_.deadCancels
                                                : result_.deadCancelsPreDecay,
                     actions);

    ImGui::Spacing();
    ImGui::Text("Unreachable moves: %d of %d.",
                static_cast<int>(result_.unreachableMoves.size()),
                static_cast<int>(c.moves.size()));
    if (result_.unreachableMoves.empty()) {
        // The second vacuity guard, and this one fires on every shipped
        // character. An empty starter list is read as "any move may open a
        // combo" (comboprover.hpp:350-354), under which almost nothing can be
        // unreachable -- so an empty list here is a property of the FILE, not a
        // compliment, and printing a green "none" would be a lie of omission.
        if (c.starters.empty()) {
            textLoud(kWarn,
                "None -- but this file declares no starters, which the prover "
                "reads as \"any move may open a combo\". An empty list is very "
                "nearly guaranteed under that reading and says almost nothing. "
                "Author starters to make this question mean something.");
        } else {
            textLoud(kGood, "None -- every move can be reached by some combo.");
        }
    } else {
        textDim("No combo can produce these:");
        drawMoveButtons_(c, result_.unreachableMoves, actions);
    }
}

void ComboProverPanel::drawProjection_() {
    if (!resultOk_) return;

    if (result_.soundnessAlarm) {
        // ProverAdapter.h asks for these words specifically. This is the one
        // message on the page that must not be paraphrased into something
        // softer, and it is drawn OUTSIDE the collapsing header below so that
        // collapsing the section cannot hide it.
        textLoud(kBad,
            "SOUNDNESS ALARM: this character contains data whose projection can "
            "HIDE a real infinite. A TERMINATING verdict here is not a clean "
            "bill of health.");
    }

    int live = 0;
    for (const ProjectionLoss& l : result_.losses) if (l.count != 0) ++live;

    char header[96];
    std::snprintf(header, sizeof(header),
                  "What the projection could not carry (%d live)###losses", live);
    // Open by default only when something is actually wrong. This section is
    // about the TOOL rather than about the character, which is why it is the one
    // allowed to collapse while the section above it is not.
    ImGui::SetNextItemOpen(result_.soundnessAlarm, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader(header)) return;

    ImGui::Checkbox("show checks that found nothing", &showInertLosses_);
    textDim("A check that ran and found nothing is not the same as a check that "
            "does not exist, which is why the inert ones are kept rather than "
            "dropped.");

    for (const ProjectionLoss& l : result_.losses) {
        if (l.count == 0 && !showInertLosses_) continue;
        // Colour follows the DIRECTION of the loss, because that is the whole
        // soundness argument: permissive means you may investigate a combo that
        // does not exist, conservative means you may be told you are finished
        // when you are not. Only one of those costs the shipped game.
        const ImVec4* colour = nullptr;
        if (l.count != 0 && l.direction == LossDirection::Conservative)     colour = &kBad;
        else if (l.count != 0 && l.direction == LossDirection::Permissive)  colour = &kWarn;

        if (colour) ImGui::PushStyleColor(ImGuiCol_Text, *colour);
        ImGui::Text("%s x%d -- %s", l.field.c_str(), static_cast<int>(l.count),
                    cse::data::LossDirectionName(l.direction));
        if (colour) ImGui::PopStyleColor();
        if (!l.note.empty()) {
            ImGui::Indent();
            textDim(l.note.c_str());
            ImGui::Unindent();
        }
    }
}

void ComboProverPanel::drawFooter_(const CharacterData& c) {
    ImGui::Separator();
    if (resultOk_ && ImGui::SmallButton("Copy verdict")) {
        // DescribeVerdict lives in CseData rather than here on purpose: the
        // tests assert on the same text a designer pastes into a bug report, so
        // the two can never describe the character differently.
        const std::string text = cse::data::DescribeVerdict(c, result_);
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine(0.f, 6.f);
    // A forced re-run goes through the fingerprint like every other re-run --
    // see the note on forceNonce_ in the header.
    if (ImGui::SmallButton("Re-run")) ++forceNonce_;

    // ARCHITECTURE.md section 5.5 item 2: a latency distribution over a real
    // authoring session is a stronger claim than comboprover.hpp's own
    // "sub-millisecond", and the panel running the analysis is the only place it
    // can be gathered honestly. It costs two clock reads, so it is gathered.
    if (runs_ > 0) {
        ImGui::TextDisabled("%d run%s: last %.3f ms, worst %.3f ms, mean %.3f ms",
                            runs_, runs_ == 1 ? "" : "s", lastRunMs_, worstRunMs_,
                            totalRunMs_ / static_cast<double>(runs_));
        // Note 7's cost, beside those four rather than inside them: they are the
        // `analyse` latency section 5.5 item 2 asks for and must stay comparable
        // with every other measurement of that call. This one is a second
        // analysis plus a bridge build, and it runs only for a TERMINATING
        // character with a resource in play -- so a designer who sees it is also
        // being told which of the two paths their character took.
        if (gap_.ran) {
            ImGui::SameLine(0.f, 8.f);
            ImGui::TextDisabled("(+ %.3f ms resource check)", lastGapMs_);
        }
    }
}

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------

ComboProverActions ComboProverPanel::Draw(const CharacterData* character, bool* pOpen) {
    ComboProverActions actions;
    widgetId_ = 0;   // see moveButton_: a loop that revisits a move draws the
                     // same label twice, and equal labels are one widget

    ImGui::SetNextWindowSize(ImVec2(520, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Combo Prover", pOpen)) {
        drawSource_(character);

        // The editor's live character wins. The panel's own copy is what makes
        // this useful on the day before the editor has a character-owning system
        // at all, and it stops being reached the moment one exists.
        const CharacterData* subject =
            character ? character : (ownedLoaded_ ? &owned_ : nullptr);

        if (!subject) {
            textDim(loadAttempted_ ? "No character loaded -- see the error above."
                                   : "No character loaded. Type a path and press Load.");
        } else {
            ImGui::Text("%s", subject->name.empty() ? subject->id.c_str()
                                                    : subject->name.c_str());
            ImGui::SameLine(0.f, 8.f);
            ImGui::TextDisabled("%d moves, %d cancels, %d resources",
                                static_cast<int>(subject->moves.size()),
                                static_cast<int>(subject->cancels.size()),
                                static_cast<int>(subject->resources.size()));

            runIfStale_(*subject);
            drawStageBanner_(*subject);
            drawVerdict_(*subject, actions);
            drawDaily_(*subject, actions);
            drawProjection_();
            drawFooter_(*subject);
        }
    }
    ImGui::End();
    return actions;
}
