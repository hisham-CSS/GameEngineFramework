#include "FightHud.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace untitledfighter {

namespace {

    // --- Type scale ----------------------------------------------------------
    //
    // Multipliers on the font's baked atlas height (kUIFontAtlasPixels, 48 px).
    // kUIFontBaseScale IS "ordinary text" and is used as such rather than being
    // re-derived, so this readout is the same size as the rest of the host's UI.
    constexpr float kBodyScale   = MyCoreEngine::kUIFontBaseScale;   // 18 px
    constexpr float kSmallScale  = 0.29f;                            // ~14 px
    constexpr float kHeadScale   = 0.34f;                            // ~16 px
    constexpr float kBannerScale = 0.46f;                            // ~22 px
    constexpr float kTitleScale  = 0.52f;                            // ~25 px

    // --- Layout --------------------------------------------------------------
    constexpr float kMarginPx  = 18.0f;
    constexpr float kPadPx     = 12.0f;
    constexpr float kRowGapPx  = 3.0f;
    constexpr float kGutterPx  = 8.0f;
    constexpr float kLeftW     = 336.0f;
    constexpr float kRightW    = 392.0f;
    constexpr float kLabelColW = 96.0f;

    // --- Colours -------------------------------------------------------------
    //
    // The PHASE colours are NOT here: they come from FightView's one table, so
    // the legend on this screen and the boxes in the world cannot part company --
    // which is why the legend below grew a sixth swatch the day FightView grew a
    // sixth phase, without this file being told. Everything below is furniture.
    const glm::vec4 kPanelFill { 0.07f, 0.075f, 0.10f, 0.90f };
    const glm::vec4 kPanelEdge { 0.22f, 0.24f, 0.31f, 0.90f };
    const glm::vec4 kHeadCol   { 0.72f, 0.78f, 0.92f, 1.00f };
    const glm::vec4 kLabelCol  { 0.52f, 0.55f, 0.63f, 1.00f };
    const glm::vec4 kValueCol  { 0.88f, 0.90f, 0.94f, 1.00f };
    const glm::vec4 kDimCol    { 0.45f, 0.47f, 0.54f, 1.00f };
    const glm::vec4 kGoodCol   { 0.44f, 0.86f, 0.58f, 1.00f };
    const glm::vec4 kWarnCol   { 0.96f, 0.72f, 0.30f, 1.00f };
    const glm::vec4 kAlarmCol  { 0.99f, 0.38f, 0.34f, 1.00f };
    const glm::vec4 kLoudFill  { 0.40f, 0.06f, 0.09f, 0.95f };
    const glm::vec4 kLoudEdge  { 0.99f, 0.38f, 0.34f, 1.00f };
    const glm::vec4 kTitleCol  { 0.96f, 0.96f, 0.98f, 1.00f };

    // A panel's background is emitted AFTER its text and at a LOWER layer, so
    // that the panel can be exactly as tall as the words that went into it. That
    // is what Renderer2D's stable sort by layer is for -- its header says "a
    // caller can emit in any order and still get correct back-to-front painting"
    // -- and it removes the whole class of bug where a fixed panel height and a
    // sentence somebody edited disagree about how much room there is.
    constexpr int kLayerPanel = 0;
    constexpr int kLayerInk   = 1;
    constexpr int kLayerTop   = 2;

    // --- Small formatting helpers --------------------------------------------

    std::string hex32(std::uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", v);
        return std::string(buf);
    }

    std::string oneDecimal(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
        return std::string(buf);
    }

    std::string signedTicks(std::int32_t v) {
        return (v > 0 ? "+" : "") + std::to_string(v);
    }

    // Sub-units to pixels, for a readout. Through FightView's WorldPx, so the
    // number printed here and the position drawn out there come from the one
    // conversion -- and the rounding to one decimal is for display only, which is
    // the last thing that happens to it.
    std::string pixelsOf(std::int32_t subUnits) {
        return oneDecimal(WorldPx(subUnits)) + " px";
    }

    // The move's id, from the map the build produced. Never the character file's
    // move list indexed by hand: MoveIndexMap is the mapping as a function
    // (MatchBuilder.h), and "kernel slot minus one" written out at a call site is
    // the correction that is right in four places and forgotten in the fifth.
    std::string moveIdOf(const cse::data::MoveIndexMap* names, std::uint16_t slot) {
        if (slot == 0) return "idle";
        if (names == nullptr) return "slot " + std::to_string(slot);
        const std::string_view id = names->IdOf(slot);
        return id.empty() ? ("slot " + std::to_string(slot)) : std::string(id);
    }

    // The authored label, for the one line where prose is friendlier than an id.
    std::string moveLabelOf(const cse::data::CharacterData* character,
                            std::uint16_t slot) {
        if (character == nullptr || slot == 0) return std::string();
        const cse::data::MoveIndex m =
            cse::data::MoveIndexMap::CharacterMoveOf(slot);
        if (m == cse::data::kInvalidMove || m >= character->moves.size())
            return std::string();
        return character->moves[m].label;
    }

    // --- A cursor down a panel -----------------------------------------------
    //
    // Every string that goes through it is WRAPPED to the panel's width first.
    // Not a nicety: DrawText walks the pen with no clipping of its own, so an
    // unwrapped sentence paints straight across whatever is to its right -- and
    // most of the sentences on this screen are quotes from other layers, whose
    // length is not this file's to decide. Font::WrapLines is the engine's own
    // wrap (it breaks a word longer than the line rather than letting it
    // overflow), so the answer here is the same one the UI layer would get.
    //
    // Rows are drawn against a FIXED label column rather than against the widest
    // measured label. Aligning to the widest means measuring every label before
    // drawing any of them, which is a layout pass, and there is a layout engine
    // one directory over for anything that needs one.
    //
    // --- THE INK FLAG, WHICH IS THE ONLY REASON THIS SCREEN FITS ---------------
    //
    // With ink off the pen advances exactly as it would with ink on and emits
    // nothing. A caller can therefore ask "how tall is this panel" by laying it
    // out, and get an answer that CANNOT be wrong, because the answer came from
    // the same statements that will draw it -- no parallel height table, no
    // constant to keep in step with a sentence somebody edits. Two passes over
    // ten short strings per panel is a rounding error against the draw itself.
    //
    // It is a flag rather than a null Renderer2D because a null-object renderer
    // would be a second implementation of every DrawX in the engine, kept alive
    // for one caller in a title.
    class Pen {
    public:
        Pen(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
            float x, float y, float w, bool ink = true)
            : r2d_(r2d), font_(font), x_(x), y_(y), w_(w), ink_(ink) {}

        float y() const { return y_; }

        void gap(float px) { y_ += px; }

        void heading(const std::string& text, const glm::vec4& colour) {
            draw_(text, x_, w_, colour, kHeadScale);
            y_ += 2.0f;
        }

        void line(const std::string& text, const glm::vec4& colour,
                  float scale = kSmallScale) {
            draw_(text, x_, w_, colour, scale);
        }

        void row(const char* label, const std::string& value,
                 const glm::vec4& colour = kValueCol, float scale = kBodyScale) {
            if (ink_)
                r2d_.DrawText(font_, label, { x_, y_ }, kLabelCol, kLayerInk, scale);
            // An empty value still advances a full row: Font::Measure returns
            // (0, lineHeight) for "" precisely so a caller need not special-case
            // it, and a blank line in a table is still a line.
            draw_(value, x_ + kLabelColW, w_ - kLabelColW, colour, scale);
        }

    private:
        void draw_(const std::string& text, float x, float width,
                   const glm::vec4& colour, float scale) {
            const std::string wrapped = wrap_(text, width, scale);
            // THE ADVANCE IS OUTSIDE THE BRANCH, deliberately and load-bearingly:
            // the measuring pass and the drawing pass differ in what reaches the
            // renderer and in nothing else.
            if (ink_)
                r2d_.DrawText(font_, wrapped, { x, y_ }, colour, kLayerInk, scale);
            y_ += font_.Measure(wrapped, scale).y + kRowGapPx;
        }

        // Re-joined with '\n' rather than drawn line by line, because DrawText
        // already breaks on '\n' and Measure already counts the lines -- so one
        // string keeps the draw and the advance reading off the same thing.
        std::string wrap_(const std::string& text, float width, float scale) const {
            if (width <= 0.0f) return text;
            std::vector<MyCoreEngine::Font::Line> lines;
            font_.WrapLines(text, width, scale, lines);
            if (lines.size() <= 1) return text;
            std::string out;
            out.reserve(text.size() + lines.size());
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (i) out += '\n';
                out.append(text, lines[i].begin, lines[i].end - lines[i].begin);
            }
            return out;
        }

        MyCoreEngine::Renderer2D& r2d_;
        const MyCoreEngine::Font& font_;
        float x_, y_, w_;
        bool  ink_;
    };

    void panel(MyCoreEngine::Renderer2D& r2d, float x, float y, float w, float h,
               const glm::vec4& fill, const glm::vec4& edge) {
        MyCoreEngine::Renderer2D::BoxStyle style{};
        style.radiusPx    = 6.0f;
        style.borderPx    = 1.0f;
        style.borderColor = edge;
        r2d.DrawBox({ x, y }, { w, h }, fill, style, 0, {}, kLayerPanel);
    }

    // --- A column of panels, with a bottom it is not allowed to cross ---------
    //
    // WHAT THIS REPLACES, AND WHY IT IS NOT JUST A SMALLER FONT.
    //
    // The columns used to be `y += panel(...)`, forever, and the furniture along
    // the bottom used to be pinned at a constant. At 1280x720 -- the size
    // Player/src/PlayerMain.cpp opens the window at -- the left column's last
    // panel began at y569 and ended at y730 in a 720 px window, and the legend,
    // on the same layer and submitted later, painted straight over it. The panel
    // destroyed was the one that says which key does what.
    //
    // Neither half of that is fixable by typing a smaller number. The content
    // genuinely does not fit, so SOMETHING HAS TO GIVE, and the only question is
    // what and whether the screen says so. This class makes the answer: panels go
    // in priority order, each is measured before it is placed, and one that does
    // not fit is DROPPED WHOLE and named at the bottom of the column.
    //
    // Dropped whole rather than clipped, because half a panel is indistinguishable
    // from a panel about something else; and named rather than silently omitted,
    // because a feature that vanishes without comment is the failure this project
    // keeps writing down -- a key that does nothing and says nothing.
    class Column {
    public:
        Column(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
               float x, float y, float w, float bottom)
            : r2d_(r2d), font_(font), x_(x), y_(y), w_(w), bottom_(bottom),
              top_(y) {}

        // `draw(x, y, w, ink)` returns the height it occupies and must draw
        // nothing when `ink` is false. It is called twice; see Pen.
        template <class Draw>
        void Add(const char* name, Draw&& draw) {
            const float h = draw(x_, y_, w_, false);
            if (y_ + h <= bottom_) {
                draw(x_, y_, w_, true);
                y_ += h + kGutterPx;
                return;
            }
            if (!dropped_.empty()) dropped_ += ", ";
            dropped_ += name;
            // Everything after this one is further down and cannot fit either.
            // Left as a fact rather than a short-circuit: the measuring pass for
            // the panels below still runs, costs nothing visible, and keeps this
            // loop's shape the same whether or not anything was dropped.
            y_ = bottom_ + 1.0f;
        }

        // One line, at the very bottom, naming everything that went. Called after
        // the last Add. Silent when nothing was dropped -- which is the case at
        // every window size this mode is expected to run at.
        void Finish() {
            if (dropped_.empty()) return;
            const std::string say =
                dropped_ + " -- hidden. The window is too short to draw them "
                           "without painting over something else; make it taller.";
            const float h = font_.MeasureWrapped(say, w_, kSmallScale).y;
            const float y = bottom_ - h;
            if (y < top_) return;   // not even the apology fits; say nothing
            Pen pen(r2d_, font_, x_, y, w_);
            pen.line(say, kWarnCol);
        }

    private:
        MyCoreEngine::Renderer2D& r2d_;
        const MyCoreEngine::Font& font_;
        float x_, y_, w_, bottom_, top_;
        std::string dropped_;
    };

    // --- The move's frame data, as a strip -----------------------------------
    //
    // Three segments in proportion, with a cursor at Fighter::moveFrame. It is
    // the same three numbers the row above it prints, drawn so that "where am I
    // in this move" is one glance rather than a subtraction -- which is most of
    // what a training mode is for.
    //
    // The segment widths come from MoveDef and the cursor from moveFrame; the
    // total is cse::kernel::MoveDuration, CALLED, so a move whose authored
    // recovery is negative is exactly as long here as it is in the tick. The
    // segment colours are FightView's, so a strip and the box it describes are
    // the same colour on the same tick.
    //
    // THE ACTIVE SEGMENT STAYS RED EVEN WHILE THE WINDOW IS SPENT, and that is
    // the right reading rather than an omission. This strip is a picture of the
    // MOVE -- "two of these fourteen frames can hit" -- and that sentence is true
    // of every performance of it. Whether THIS performance has used its hit up is
    // a fact about the instance, and it is on the fighter, on the box, and on the
    // `state` row, which is where a reader looking for it will be.
    void moveStrip(MyCoreEngine::Renderer2D& r2d, float x, float y, float w,
                   const cse::kernel::MoveDef& move, std::int32_t frame) {
        const std::int32_t duration = cse::kernel::MoveDuration(move);
        if (duration <= 0 || w <= 0.0f) return;

        const float unit = w / static_cast<float>(duration);
        const std::int32_t parts[3] = {
            move.startup  > 0 ? move.startup  : 0,
            move.active   > 0 ? move.active   : 0,
            move.recovery > 0 ? move.recovery : 0,
        };
        const Phase phases[3] = { Phase::Startup, Phase::Active, Phase::Recovery };

        constexpr float kStripH = 9.0f;
        float cursorX = x;
        for (int i = 0; i < 3; ++i) {
            const float segW = unit * static_cast<float>(parts[i]);
            if (segW > 0.0f)
                r2d.DrawQuad({ cursorX, y }, { segW, kStripH },
                             PhaseColour(phases[i]), kLayerInk);
            cursorX += segW;
        }

        // The cursor sits on the LEFT edge of the frame it is on, so frame 0 is
        // at the start of the bar rather than one cell in.
        const float fx = x + unit * static_cast<float>(frame);
        r2d.DrawQuad({ fx - 1.0f, y - 3.0f }, { 2.0f, kStripH + 6.0f },
                     kTitleCol, kLayerTop);
    }

    // --- The advantage row's second line --------------------------------------

    // The working behind the number, in the two halves it was the difference of,
    // plus the tick it was taken on and the route it came by.
    //
    // IT IS ALWAYS DRAWN, including before anything has connected, so that the
    // panel does not change height on the tick a hit lands -- a readout that
    // shoves the panel below it down by a line every time the player connects is
    // its own small lie about what changed.
    std::string advantageWorking(const FightHudModel& model) {
        const LatchedAdvantage& latched = model.hitAdvantage;
        if (!latched.measured)
            return "   nothing has connected yet. This fills in on the tick a hit "
                   "lands and then holds still.";

        const cse::data::MoveIndexMap* const names = model.names[model.playerSlot];
        const std::string when = moveIdOf(names, latched.moveId) + " on tick " +
                                 std::to_string(latched.tick);
        const Advantage& adv = latched.advantage;
        if (!adv.known)
            return "   " + when + " inflicted no stun, so there is no advantage "
                   "to report for it.";

        std::string out = "   " + when + " -- they act in " +
                          std::to_string(adv.defenderTicks) + ", you in " +
                          std::to_string(adv.attackerTicks);
        switch (adv.attackerRoute) {
            case ActRoute::Cancel:
                out += " by cancelling into " + moveIdOf(names, adv.attackerCancelTo);
                break;
            case ActRoute::Recovery: out += " when the move recovers";        break;
            case ActRoute::Stun:     out += " when your own stun runs out";   break;
            case ActRoute::Free:     out += ", with nothing holding you";     break;
        }
        return out + ".";
    }

    // --- One fighter's readout ------------------------------------------------
    //
    // THE TWO SIDES GET DIFFERENT PANELS, AND THE ASYMMETRY IS THE CONTENT RATHER
    // THAN THE PIXELS. The dummy is fed neutral on every tick (the mode binds its
    // slot to nothing, deliberately and in writing), so its move, its frame, its
    // s/a/r and its frame advantage are `-`, `-`, `-` and `-` for the entire life
    // of the session: five rows of readout about a fighter that never acts, in a
    // column that had run off the bottom of the window. What a playtester
    // actually needs from the dummy is what state it is in, how much hitstun is
    // left, and what its health is doing, and that is what it gets.
    //
    // The move rows are not deleted, they are CONDITIONAL: if the dummy is ever
    // observed in a move -- a future dummy playback, a hand-set state, a bug --
    // that is the interesting event, and the panel grows the rows to show it
    // rather than reporting an idle fighter that is not idle.
    float fighterPanel(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
                       float x, float y, float w, const FightHudModel& model,
                       int slot, bool ink) {
        const cse::kernel::Fighter&     f    = model.state->p[slot];
        const cse::kernel::FighterData& data = model.data->p[slot];
        const cse::kernel::MoveDef* const move = cse::kernel::MoveAt(data, f.moveId);

        Pen pen(r2d, font, x + kPadPx, y + kPadPx, w - kPadPx * 2.0f, ink);

        const bool isPlayer = slot == model.playerSlot;
        pen.heading(std::string("P") + std::to_string(slot + 1) + "  " +
                        (isPlayer ? "YOU" : "TRAINING DUMMY"),
                    SlotColour(slot));

        const Phase phase = PhaseOf(data, f);
        pen.row("state", PhaseName(phase), PhaseColour(phase));

        // MOVE, FRAME and the frame data, every one of them read straight off the
        // state and off the MoveDef its id names.
        if (move != nullptr) {
            pen.row("move", moveIdOf(model.names[slot], f.moveId), kValueCol);
            // The authored prose, on the player's panel only: `model.character`
            // is the character the PLAYER's slot was built from, and putting its
            // labels under the other fighter's move ids would be a claim about a
            // file this model does not carry for that slot.
            if (isPlayer) {
                const std::string label = moveLabelOf(model.character, f.moveId);
                if (!label.empty()) pen.line("   " + label, kDimCol);
            }
            pen.row("frame", std::to_string(f.moveFrame) + " of " +
                                 std::to_string(cse::kernel::MoveDuration(*move)));
            pen.row("s/a/r", std::to_string(move->startup) + " / " +
                                 std::to_string(move->active) + " / " +
                                 std::to_string(move->recovery));
            if (ink)
                moveStrip(r2d, x + kPadPx, pen.y() + 2.0f, w - kPadPx * 2.0f, *move,
                          static_cast<std::int32_t>(f.moveFrame));
            pen.gap(17.0f);
        } else if (isPlayer) {
            pen.row("move",
                    f.moveId == 0 ? std::string("-")
                                  : "unknown id " + std::to_string(f.moveId),
                    f.moveId == 0 ? kDimCol : kWarnCol);
            pen.row("frame", "-", kDimCol);
            pen.row("s/a/r", "-", kDimCol);
        } else if (f.moveId != 0) {
            // An id this character's table does not describe, on the fighter that
            // is not supposed to be doing anything. Worth a row on its own.
            pen.row("move", "unknown id " + std::to_string(f.moveId), kWarnCol);
        }

        pen.row("hitstun", std::to_string(f.hitstun),
                f.hitstun > 0 ? PhaseColour(Phase::Hitstun) : kDimCol);

        // KNOCKDOWN GETS ITS OWN ROW, and it says what the state MEANS rather
        // than only how long it lasts. "Cannot be hit" is the half a tick count
        // does not convey, and it is the half that explains why an attack just
        // passed through somebody.
        if (f.knockdown > 0)
            pen.row("knockdown", std::to_string(f.knockdown) + "  (cannot be hit)",
                    PhaseColour(Phase::Knockdown));

        // FRAME ADVANTAGE, LATCHED, AND ONLY ON THE PLAYER'S PANEL.
        //
        // Only on the player's because there is one attacker in this mode and one
        // ComboWatcher judging them, and an advantage row on the dummy would be
        // permanently "-": it is `known` only while the OTHER fighter is in stun,
        // and nothing the dummy does can put the player there.
        //
        // Latched because the live answer means a different thing on every tick;
        // see LatchedAdvantage in FightHud.h, which is where the argument is. The
        // number here is READ out of the model -- this file no longer calls
        // FrameAdvantage at draw time at all, which is the whole fix: a value
        // drawn 144 times a second from live state cannot be about one hit.
        if (isPlayer) {
            const LatchedAdvantage& latched = model.hitAdvantage;
            const bool have = latched.measured && latched.advantage.known;
            pen.row("on hit",
                    have ? signedTicks(latched.advantage.ticks) + " ticks"
                         : std::string("-"),
                    !have ? kDimCol
                          : (latched.advantage.ticks >= 0 ? kGoodCol : kWarnCol));
            pen.line(advantageWorking(model), kDimCol);
        }

        pen.row("health", std::to_string(f.health) +
                              (f.res[0] != 0 ? "   res0 " + std::to_string(f.res[0])
                                            : std::string()));
        if (isPlayer)
            pen.row("pos", pixelsOf(f.posX) + (f.airborne ? "   airborne" : ""),
                    kDimCol, kSmallScale);

        const float h = pen.y() - y + kPadPx - kRowGapPx;
        if (ink) {
            panel(r2d, x, y, w, h, kPanelFill, kPanelEdge);
            // The slot accent, matching the bar under the body in the world view.
            r2d.DrawQuad({ x, y }, { 4.0f, h }, SlotColour(slot), kLayerInk);
        }
        return h;
    }

    // --- What the analysis said ----------------------------------------------

    // The build's own words about one named loss, or null. Quoting rather than
    // paraphrasing because the bridge already wrote the sentence, and a second
    // vocabulary for the same fact is a second thing to keep in step.
    const cse::data::BuildLoss* lossNamed(const cse::data::BuildReport* report,
                                          const char* field) {
        if (report == nullptr) return nullptr;
        for (const cse::data::BuildLoss& loss : report->losses)
            if (loss.field == field) return &loss;
        return nullptr;
    }

    float analysisPanel(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
                        float x, float y, float w, const FightHudModel& model,
                        bool ink) {
        Pen pen(r2d, font, x + kPadPx, y + kPadPx, w - kPadPx * 2.0f, ink);
        pen.heading("THE ANALYSIS", kHeadCol);

        if (model.character != nullptr) {
            pen.row("character", model.character->id, kValueCol);
            if (!model.character->name.empty())
                pen.line("   " + model.character->name, kDimCol);
        }

        const cse::data::ProverResult* const a = model.analysis;
        if (a == nullptr) {
            pen.row("verdict", "NOT RUN", kAlarmCol);
            if (model.analysisError != nullptr && !model.analysisError->empty())
                pen.line(*model.analysisError, kAlarmCol);
            const float h = pen.y() - y + kPadPx - kRowGapPx;
            if (ink) panel(r2d, x, y, w, h, kPanelFill, kPanelEdge);
            return h;
        }

        const bool infinite = a->status == cse::data::ProverStatus::Infinite;
        pen.row("verdict", cse::data::ProverStatusName(a->status),
                infinite ? kAlarmCol : kGoodCol);

        // BOTH STAGES, ALWAYS. ProverAdapter.h note 2: the procedure is
        // corner-only and the files declare their own stage, so a panel that
        // showed one without the other would be showing a coin flip. The dummy
        // starts in the corner for this reason, and the marker on the stage is
        // the other half of the same sentence.
        pen.row("stage",
                std::string("answered CORNER, file says ") +
                    (a->fileStage.empty() ? std::string("(nothing)") : a->fileStage),
                a->fileStage == "corner" ? kValueCol : kWarnCol, kSmallScale);

        if (a->status == cse::data::ProverStatus::Terminating) {
            pen.row("worst case", std::to_string(a->maxHits) + " hits, " +
                                      std::to_string(a->maxFrames) + " frames, " +
                                      std::to_string(a->maxDamageHundredths / 100) +
                                      " damage");
            if (a->hasRanking) {
                std::string order;
                for (std::size_t i = 0; i < a->rankingOrder.size(); ++i) {
                    if (i) order += " > ";
                    const cse::data::ResourceIndex ri = a->rankingOrder[i];
                    order += (model.character != nullptr &&
                              ri < model.character->resources.size())
                                 ? model.character->resources[ri].name
                                 : "resource " + std::to_string(ri);
                }
                pen.row("certificate", order + " runs down", kValueCol);

                // ===========================================================
                // THE LINE THIS WHOLE SCREEN IS FOR
                // ===========================================================
                // Two facts, both READ, joined:
                //
                //   1. ProverResult::rankingOrder -- the analysis certifies this
                //      character TERMINATING because that resource strictly runs
                //      down over every cycle.
                //   2. BuildReport::losses["resources"] -- the bridge's own count
                //      of declared resources that did not cross into the kernel,
                //      with its own note, in the direction KernelOmits.
                //
                // When (2) bites, the reason the tool says this character is safe
                // is not simulated in the match on screen. That is
                // ARCHITECTURE.md D8 in front of a playtester rather than in a
                // test, and it is why a green verdict up here is not permission
                // to stop watching down there.
                const cse::data::BuildLoss* const res =
                    lossNamed(model.build, "resources");
                if (res != nullptr && res->count > 0) {
                    pen.line("...AND THIS KERNEL DOES NOT SIMULATE IT.", kAlarmCol);
                    pen.line("build says: " + std::to_string(res->count) +
                                 " declared resource(s), " +
                                 cse::data::BuildLossDirectionName(res->direction) +
                                 " -- \"" + res->note + "\"",
                             kWarnCol);
                }
            } else {
                pen.row("certificate", "none",
                        a->rankingAbsence == cse::data::RankingAbsence::NothingLoops
                            ? kGoodCol
                            : kWarnCol);
                pen.line(cse::data::RankingAbsenceName(a->rankingAbsence), kDimCol);
            }
        } else if (infinite) {
            std::string loop;
            for (std::size_t i = 0; i < a->loop.size() && i < 6u; ++i) {
                if (i) loop += " > ";
                loop += (model.character != nullptr &&
                         a->loop[i] < model.character->moves.size())
                            ? model.character->moves[a->loop[i]].id
                            : "move " + std::to_string(a->loop[i]);
            }
            pen.row("printed loop", loop.empty() ? std::string("(none)") : loop,
                    kAlarmCol);
            if (!a->loopEntryKnown)
                pen.line("the opening move is omitted from both lists "
                         "(loopEntryKnown false), so the witness comparison is "
                         "unreliable on this character.", kWarnCol);
            if (a->ceilingReplayRan)
                pen.row("ceilings",
                        a->loopHoldsUnderCeilings ? "the loop holds under them"
                                                  : "NOT reproducible under them",
                        a->loopHoldsUnderCeilings ? kValueCol : kWarnCol,
                        kSmallScale);
        }

        pen.row("dead cancels",
                std::to_string(a->deadCancels.size()) + " at the settled stun, " +
                    std::to_string(a->deadCancelsPreDecay.size()) + " before decay",
                kValueCol, kSmallScale);
        if (a->soundnessAlarm)
            pen.line("SOUNDNESS ALARM: this projection contains data whose loss "
                     "could HIDE a real infinite.", kAlarmCol);

        // A green build reads as a faithful one, so the number that says it is not
        // gets its own line. BuildReport::playsAsAnalysed is computed rather than
        // remembered, which is what makes it worth showing at all.
        if (model.build != nullptr)
            pen.row("build", std::to_string(model.build->lossesThatBite) +
                                 " loss(es) bite; plays as analysed: " +
                                 (model.build->playsAsAnalysed ? "yes" : "NO"),
                    model.build->playsAsAnalysed ? kGoodCol : kWarnCol, kSmallScale);

        const float h = pen.y() - y + kPadPx - kRowGapPx;
        if (ink) panel(r2d, x, y, w, h, kPanelFill, kPanelEdge);
        return h;
    }

    // --- What the player just did --------------------------------------------

    // The string in progress, or the last one that finished. ComboWatcher.h is
    // explicit that both accessors describe the same ended string in the gap
    // between combos, so this picks the open one when there is one and the
    // finished one otherwise -- and every reader of it agrees, which is why it is
    // a function rather than four copies of the same ternary.
    const cse::game::ComboReport& shownCombo(const cse::game::ComboWatcher& watcher) {
        const cse::game::ComboReport& open = watcher.Current();
        return (open.open || open.hits > 0) ? open : watcher.Previous();
    }

    float comboPanel(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
                     float x, float y, float w, const FightHudModel& model,
                     bool ink) {
        Pen pen(r2d, font, x + kPadPx, y + kPadPx, w - kPadPx * 2.0f, ink);
        pen.heading("THE COMBO JUDGE", kHeadCol);

        const cse::game::ComboWatcher* const watcher = model.watcher;
        if (watcher == nullptr) {
            pen.line("no judge: there is no match to judge.", kDimCol);
        } else if (watcher->Stale()) {
            // Training mode never rolls back, so this is a real surprise and is
            // reported as one rather than hidden behind an empty panel.
            pen.line("STALE -- a re-simulated tick invalidated this object's "
                     "history, so it stopped reporting rather than describe a "
                     "timeline that no longer happened. R clears it.", kAlarmCol);
        } else {
            const cse::game::ComboReport& r = shownCombo(*watcher);
            if (r.hits == 0 && !r.open) {
                pen.line("nothing yet. " +
                             std::to_string(watcher->CompletedCombos()) +
                             " string(s) have ended since the last reset.",
                         kDimCol);
            } else {
                pen.row("state",
                        r.open ? std::string("OPEN")
                               : "ended on tick " + std::to_string(r.endTick),
                        r.open ? kWarnCol : kDimCol);
                pen.row("hits", std::to_string(r.hits) + "   for " +
                                    std::to_string(r.damage) + " damage");
                pen.row("combo",
                        r.TrueCombo() ? std::string("TRUE COMBO")
                                      : "gap on " + std::to_string(r.gapTicks) +
                                            " tick(s) -- not a true combo",
                        r.TrueCombo() ? kGoodCol : kWarnCol);
                if (r.whiffedStarts > 0)
                    pen.row("whiffs",
                            std::to_string(r.whiffedStarts) +
                                " start(s) never connected",
                            kDimCol, kSmallScale);

                // The chain, most recent last. Capped here as well as in the
                // watcher, because the case that fills its 256-move cap is
                // exactly the case this mode exists to produce.
                std::string chain;
                const std::size_t show = 5;
                const std::size_t from =
                    r.sequence.size() > show ? r.sequence.size() - show : std::size_t{ 0 };
                if (from > 0) chain += "... > ";
                for (std::size_t i = from; i < r.sequence.size(); ++i) {
                    if (i > from) chain += " > ";
                    chain += moveIdOf(model.names[model.playerSlot], r.sequence[i]);
                }
                pen.row("moves", chain, kValueCol, kSmallScale);

                if (r.onWitness)
                    pen.row("witness", "ON IT, " + std::to_string(r.witnessIndex) +
                                           " move(s) in", kGoodCol, kSmallScale);
                if (r.cycleRun > 0)
                    pen.row("cycle",
                            std::to_string(r.cycleRun) +
                                " connecting move(s) of the printed loop, " +
                                std::to_string(r.loopTurnsCompleted) +
                                " full turn(s)",
                            kAlarmCol, kSmallScale);
                if (r.witnessIncomplete)
                    pen.line("the analysis omitted the loop's opening move, so "
                             "the witness comparison is unreliable here.", kWarnCol);
            }
        }

        const float h = pen.y() - y + kPadPx - kRowGapPx;
        if (ink) panel(r2d, x, y, w, h, kPanelFill, kPanelEdge);
        return h;
    }

    // --- What Demonstrate will do ---------------------------------------------

    // Last in the right column, and therefore the panel that gives way first when
    // the window is too short. That is the correct order rather than an accident:
    // the analysis and the judge are both LIVE readouts a playtester watches, and
    // this one is a paragraph they read once. Nothing is lost when it goes -- the
    // key itself is still listed along the bottom, where every other control is.
    float demoPanel(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
                    float x, float y, float w, const FightHudModel& model,
                    bool ink) {
        Pen pen(r2d, font, x + kPadPx, y + kPadPx, w - kPadPx * 2.0f, ink);
        pen.heading("DEMONSTRATE  [TAB]", model.demoArmed ? kGoodCol : kDimCol);
        if (model.demoNote != nullptr && !model.demoNote->empty())
            pen.line(*model.demoNote, model.demoArmed ? kValueCol : kWarnCol);
        const float h = pen.y() - y + kPadPx - kRowGapPx;
        if (ink) panel(r2d, x, y, w, h, kPanelFill, kPanelEdge);
        return h;
    }

    // --- The loud line ---------------------------------------------------------

    // What, if anything, the game must say out loud about what just happened.
    //
    // ORDERED, most alarming first, and every one of them is a flag somebody else
    // computed: four straight off ComboReport, and one join of ComboReport::hits
    // with ProverResult::maxHits. Nothing here re-judges anything.
    struct Loud {
        bool        show = false;
        std::string headline;
        std::string detail;
    };

    Loud loudLine(const FightHudModel& model) {
        Loud out{};
        if (model.watcher == nullptr || model.watcher->Stale()) return out;

        const cse::game::ComboReport& r = shownCombo(*model.watcher);
        if (r.hits == 0) return out;

        // 1. THE MEASURED DISAGREEMENT. The analysis said the defender would be
        // free before this could land, and it landed anyway.
        if (r.deadEdgeConnected) {
            out.show     = true;
            out.headline = "A DEAD CANCEL JUST CONNECTED";
            out.detail   = "the analysis listed that edge as unable to land in "
                           "time, and it landed inside an open string. That is a "
                           "measured disagreement between the model and this "
                           "game: a finding, not a glitch.";
            return out;
        }

        // 2. The printed loop, performed in the running game.
        if (r.completedProverLoop) {
            out.show     = true;
            out.headline = "YOU PERFORMED THE PROVER'S PRINTED LOOP";
            out.detail   = "the decision procedure read this loop out of the "
                           "character file, and you have just completed " +
                           std::to_string(r.loopTurnsCompleted) +
                           " full turn(s) of it in the running game.";
            return out;
        }

        // 3. THE CERTIFICATE, EXCEEDED. Two read numbers: ComboReport::hits,
        // counted by the watcher off the kernel's own alreadyHitBits, against
        // ProverResult::maxHits, the worst case the decision procedure proved for
        // a character it certified TERMINATING. When the first passes the second,
        // this match has done something the certificate says cannot happen -- and
        // the reason is on the analysis panel: the certificate can rest on a
        // resource the kernel does not carry.
        //
        // THIS IS THE MOST VALUABLE THING A PLAYTESTER CAN TRIP OVER HERE, so it
        // is a banner and not a field. A tester who quietly does something
        // impossible and is never told has produced nothing.
        if (model.analysis != nullptr &&
            model.analysis->status == cse::data::ProverStatus::Terminating &&
            model.analysis->maxHits > 0 && r.hits > model.analysis->maxHits) {
            out.show     = true;
            out.headline = "PAST THE CERTIFIED WORST CASE: " +
                           std::to_string(r.hits) + " HITS vs " +
                           std::to_string(model.analysis->maxHits);
            out.detail   = "this character is CERTIFIED TERMINATING and the "
                           "analysis proved a worst case of " +
                           std::to_string(model.analysis->maxHits) +
                           " hits. This string has beaten it" +
                           (r.TrueCombo()
                                ? ", and the defender was never once actionable "
                                  "inside it."
                                : ".");
            return out;
        }

        // 4. An edge the analysis called dead, taken but not (yet) landed.
        if (r.performedDeadCancel) {
            out.show     = true;
            out.headline = "YOU TOOK A CANCEL THE ANALYSIS CALLED DEAD";
            out.detail   = "the kernel allowed an edge the model says cannot "
                           "connect in time. It has not landed inside this string "
                           "yet.";
            return out;
        }
        return out;
    }

    // --- Everything along the bottom edge -------------------------------------
    //
    // WHAT MOVED HERE AND WHY. The bindings used to be a PANEL at the foot of the
    // left column -- a heading, a line of directions and one line per bound move,
    // about 160 px of it, stacked under two fighter panels that already reached
    // y569 in a 720 px window. It was the panel that told a playtester which key
    // does what, and it was the panel that got destroyed.
    //
    // It is here now, as ONE LINE, and the collapse is what pays for the rest of
    // the screen fitting. The per-row prose it lost was the same six words six
    // times over ("J   stand_lp" told a reader nothing that "J stand_lp" does not),
    // and the two things that were NOT boilerplate -- a key bound to a move this
    // character does not have, and a key StepAttack's first-wins scan shadows --
    // still get a line each, in their own colour, because those are the cases the
    // column exists for. Neither fires on either shipped character today, so the
    // usual cost of the whole binding table is one line.
    //
    // The strip is MEASURED AND THEN PLACED FROM THE BOTTOM EDGE, which is the
    // other half of the same bug: it used to sit at `H - margin - 52`, and 52 was
    // a guess at the height of two lines of text that nobody could check.
    float bottomStrip(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
                      float x, float y, float w, const FightHudModel& model,
                      bool ink) {
        Pen pen(r2d, font, x, y, w, ink);

        // --- what to press ---------------------------------------------------
        {
            // The kernel reads exactly three direction bits (Simulate.cpp), so
            // those are the three that are bound and the three that are printed.
            std::string keys = "A / D walk    W jump";
            if (model.bindings != nullptr)
                for (const BindingRow& b : *model.bindings)
                    if (b.slot != 0)
                        keys += std::string("    ") + b.keyLabel + " " + b.moveId;
            pen.line(keys, kValueCol);

            // The two exceptions, each on its own line and only when it is real.
            if (model.bindings != nullptr) {
                for (const BindingRow& b : *model.bindings) {
                    if (b.slot == 0) {
                        pen.line(std::string(b.keyLabel) + " " + b.moveId +
                                     " -- unbound: this character has no such move",
                                 kDimCol);
                    } else if (b.shadowed) {
                        // See BindingRow::shadowed. An earlier slot's mask is a
                        // subset of this one's, so StepAttack takes that move
                        // first and this key can never start this one from
                        // neutral. It remains reachable by a cancel.
                        pen.line(std::string(b.keyLabel) + " " + b.moveId +
                                     " -- shadowed by an earlier move; cancel-only",
                                 kWarnCol);
                    }
                }
            }
        }

        // --- what the training controls do ------------------------------------
        pen.line("SPACE pause     . step one tick     , slow motion     R reset"
                 "     TAB demonstrate     C next character     V corner/midscreen"
                 "     ESC menu",
                 kDimCol);

        // WHERE THE FIGHTERS ARE STANDING, and what it costs, because the
        // verdict drawn above them is corner-only by construction.
        //
        // In the corner the two agree and the line is a statement of fact. At
        // midscreen they do not, and saying so is the whole point: a player who
        // moves the dummy out to watch knockback has to know that the TERMINATING
        // above their head stopped applying when they pressed V.
        if (model.stageMidscreen)
            pen.line("MIDSCREEN -- knockback and spacing are visible here, and the "
                     "verdict above is a CORNER verdict that does not describe "
                     "this position. [V] returns to the corner.",
                     kWarnCol);
        else
            pen.line("CORNER -- the position every verdict on this screen is "
                     "about. Knockback has nowhere to carry the dummy here; "
                     "[V] moves out to midscreen to watch it.",
                     kDimCol);

        // --- what the colours mean --------------------------------------------
        //
        // From the SAME table the boxes use, so the legend and the picture cannot
        // part company -- and that is now seven entries, because FightView draws
        // a spent window and a knocked-down fighter in their own colours.
        const Phase order[7] = { Phase::Idle,  Phase::Startup,  Phase::Active,
                                 Phase::Spent, Phase::Recovery, Phase::Hitstun,
                                 Phase::Knockdown };
        const float swatchY = pen.y();
        const float lineH   = font.Measure("", kSmallScale).y;
        float       swatchX = x;
        for (const Phase p : order) {
            if (ink)
                r2d.DrawQuad({ swatchX, swatchY + 3.0f }, { 11.0f, lineH - 6.0f },
                             PhaseColour(p), kLayerInk);
            const std::string name = PhaseName(p);
            if (ink)
                r2d.DrawText(font, name, { swatchX + 16.0f, swatchY }, kDimCol,
                             kLayerInk, kSmallScale);
            swatchX += 16.0f + font.Measure(name, kSmallScale).x + 18.0f;
        }

        // The two glosses that cannot be inferred from a colour, on the same row
        // as the swatches and WRAPPED to whatever is left of it. Wrapped rather
        // than trusted to fit: this is the one row on the screen whose width is
        // decided by six measured strings, and DrawText walks the pen with no
        // clipping of its own -- an unwrapped sentence here paints off the right
        // edge of the window at any font this file did not measure against.
        {
            const float tailX = swatchX + 8.0f;
            Pen tail(r2d, font, tailX, swatchY, w - (tailX - x), ink);
            tail.line("spent = it already connected.   orange bars = the CORNER, "
                      "the only stage position the analysis answers for.",
                      kWarnCol);
            // The row is as tall as the taller of the two things in it.
            const float tallest = tail.y() > swatchY + lineH + kRowGapPx
                                      ? tail.y()
                                      : swatchY + lineH + kRowGapPx;
            pen.gap(tallest - pen.y());
        }

        return pen.y() - y - kRowGapPx;
    }

} // namespace

// --- The two derivations ------------------------------------------------------

namespace {

    // The earliest tick a cancel out of this fighter's current move could open,
    // as an offset from the end of the observed tick, or 0 for "none can".
    //
    // THE WINDOW AND THE CONTACT GATE ARE cse::kernel::CancelIsOpen's, asked of a
    // COPY of the fighter with moveFrame moved forward to the candidate frame.
    // That delegation is the point: the window is inclusive at BOTH ends, an edge
    // whose earliest is past its latest is inert rather than malformed, and the
    // contact gate is `alreadyHitBits != 0` -- three rules that a comparison
    // written out here would have to carry and would eventually carry wrongly.
    //
    // The two skips are FindCancel's own, in its own order: an edge whose target
    // is outside the table, and a target with no button, are both unreachable and
    // are stepped over rather than counted.
    std::int32_t earliestCancel(const cse::kernel::FighterData& data,
                                const cse::kernel::Fighter& fighter,
                                std::int32_t within, std::uint16_t& toOut) {
        const std::int32_t frameNow = static_cast<std::int32_t>(fighter.moveFrame);
        std::int32_t       best     = within;   // nothing may beat the move ending
        toOut = 0;

        for (std::int32_t i = 0;
             i < data.cancelCount && i < cse::kernel::kMaxCancelsPerFighter; ++i) {
            const cse::kernel::CancelEdge& edge = data.cancels[i];

            const cse::kernel::MoveDef* const target =
                cse::kernel::MoveAt(data, edge.to);
            if (target == nullptr) continue;
            if (target->button == 0) continue;

            // FindCancel runs AFTER `++f.moveFrame`, so the soonest frame this
            // edge can be tested at is the next one -- never the one just
            // observed, however open the window is on it.
            std::int32_t frame = edge.earliestFrame;
            if (frame < frameNow + 1) frame = frameNow + 1;
            // Fighter::moveFrame is a uint16 and the probe below has to fit in
            // one. A move long enough to reach that is a file to fix rather than
            // a cast to truncate.
            if (frame > 0xFFFF) continue;

            cse::kernel::Fighter probe = fighter;
            probe.moveFrame = static_cast<std::uint16_t>(frame);
            if (!cse::kernel::CancelIsOpen(probe, edge)) continue;

            // Strictly less than, so a tie goes to the earlier edge -- the same
            // first-wins tie-break StepAttack and FindCancel use, and chosen here
            // for their reason: two readers of the same bytes must agree.
            const std::int32_t ticks = frame - frameNow;
            if (ticks < best) {
                best  = ticks;
                toOut = edge.to;
            }
        }
        return toOut == 0 ? 0 : best;
    }

} // namespace

Actionable TicksUntilActionable(const cse::kernel::FighterData& data,
                                const cse::kernel::Fighter& fighter) {
    Actionable out{};

    // --- how the move in progress ends ---------------------------------------
    const cse::kernel::MoveDef* const move =
        cse::kernel::MoveAt(data, fighter.moveId);
    if (move != nullptr) {
        // MoveDuration, CALLED. A move ends when moveFrame reaches it, in the
        // same StepAttack call that then runs the button scan, so a fighter who
        // waits it out can start something `duration - moveFrame` from here.
        out.ticks = cse::kernel::MoveDuration(*move) -
                    static_cast<std::int32_t>(fighter.moveFrame);
        out.route = ActRoute::Recovery;

        // ...OR IT DOES NOT END AT ALL, BECAUSE IT IS CANCELLED. StepAttack runs
        // the cancel test between the lifecycle and the button scan, so this is a
        // SECOND way to become able to act and it is usually the earlier one. On
        // fighter_a's stand_lp (14 ticks, window opening at frame 5) it is nine
        // ticks earlier, and leaving it out was the readout answering a question
        // about a fighting game with no cancels in it.
        std::uint16_t to = 0;
        const std::int32_t cancel = earliestCancel(data, fighter, out.ticks, to);
        if (to != 0) {
            out.ticks    = cancel;
            out.route    = ActRoute::Cancel;
            out.cancelTo = to;
        }
    }

    // --- and the stun that gates both of them --------------------------------
    //
    // Both stuns, because Simulate.cpp's actionable() is
    // `hitstun == 0 && blockstun == 0`, and writing only the half that is
    // currently populated is how this function becomes wrong the day the other
    // one is. It is applied LAST and as a maximum because StepAttack's
    // `if (!actionable) return` sits above the cancel test as well as the button
    // scan -- a fighter in stun cannot cancel out of anything either.
    std::int32_t stun = static_cast<std::int32_t>(fighter.hitstun);
    const std::int32_t block = static_cast<std::int32_t>(fighter.blockstun);
    if (block > stun) stun = block;
    if (stun > out.ticks) {
        out.ticks    = stun;
        out.route    = ActRoute::Stun;
        // The cancel is no longer the answer, so it is not reported as one.
        out.cancelTo = 0;
    }

    // At least one: the tick being observed has already run, so nothing can be
    // acted on earlier than the next one.
    if (out.ticks < 1) out.ticks = 1;

    // The freeze pauses ALL of the above (see the header): every clock the
    // three terms read stands still while StepPhysics burns hitstop down, so
    // the pause adds to whichever term won -- after the floor, because it
    // delays the next actionable tick too. FrameAdvantage is unmoved by this
    // line: ResolveHits freezes both fighters by the same amount, so the two
    // additions cancel in its subtraction.
    out.ticks += static_cast<std::int32_t>(fighter.hitstop);
    return out;
}

Advantage FrameAdvantage(const cse::kernel::MatchData& data,
                         const cse::kernel::GameState& state, int attackerSlot) {
    Advantage out{};
    if (attackerSlot < 0 || attackerSlot > 1) return out;
    const int defenderSlot = 1 - attackerSlot;

    const cse::kernel::Fighter& defender = state.p[defenderSlot];
    // Only asked while the defender is actually held still. See the header:
    // outside that the subtraction still has an answer and the answer is not
    // frame advantage.
    if (defender.hitstun == 0 && defender.blockstun == 0) return out;

    const Actionable def = TicksUntilActionable(data.p[defenderSlot], defender);
    const Actionable atk =
        TicksUntilActionable(data.p[attackerSlot], state.p[attackerSlot]);

    out.known            = true;
    out.defenderTicks    = def.ticks;
    out.attackerTicks    = atk.ticks;
    out.ticks            = def.ticks - atk.ticks;
    out.attackerRoute    = atk.route;
    out.attackerCancelTo = atk.cancelTo;
    return out;
}

// --- The screen ----------------------------------------------------------------

void DrawFightHud(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
                  int widthPx, int heightPx, const FightHudModel& model) {
    const float W = static_cast<float>(widthPx);
    const float H = static_cast<float>(heightPx);
    const float full = W - kMarginPx * 2.0f;

    Pen top(r2d, font, kMarginPx, kMarginPx, full);
    top.line("UNTITLED FIGHTING GAME  --  TRAINING", kTitleCol, kTitleScale);

    // One line of everything the HOST is doing, as distinct from everything the
    // simulation is doing. TICK moves only if a tick ran; CHECKSUM moves only if
    // cse::kernel::Simulate really ran over the state this session owns, so the
    // two disagreeing is a symptom worth being able to see at a glance.
    {
        std::string chips = "tick " + std::to_string(model.tick);
        if (model.highWater > model.tick)
            chips += " (high water " + std::to_string(model.highWater) + ")";
        chips += "    checksum " + hex32(model.checksum);
        chips += "    host " + oneDecimal(model.hostHz) + " Hz";
        chips += model.paused
                     ? std::string("    PAUSED")
                     : (model.slowDivisor > 1
                            ? "    SLOW 1/" + std::to_string(model.slowDivisor)
                            : std::string("    running"));
        if (model.speaking != nullptr)
            chips += std::string("    input: ") + model.speaking;
        if (model.demoRemaining > 0)
            chips += " (" + std::to_string(model.demoRemaining) +
                     " scripted tick(s) left)";

        // The GAP between the two BODIES, from cse::kernel::Hurtbox -- the same
        // placement ResolveHits tests with, so this is the distance that decides
        // whether anything connects rather than the distance between two origins.
        // Negative means the bodies overlap, which the kernel permits: it has no
        // pushboxes at all.
        //
        // THE MAXIMUM OF THE TWO SIGNED SEPARATIONS, which is the standard
        // interval formula and is not the same thing as picking a branch on which
        // body is left. The branch spelling -- `b0.x1 < b1.x0 ? b1.x0 - b0.x1
        // : b0.x0 - b1.x1` -- is right whenever the bodies are apart and wrong the
        // instant they are not, because its else clause is not the mirror of its
        // then clause: with the kernel's 13 px half-width it printed -52.0 on the
        // tick the bodies came to rest exactly touching, where the truth is 0, and
        // then IMPROVED toward -26 as they interpenetrated. Walking into the dummy
        // from the opening position at kWalkSpeed read 8, 6, 4, 2, -52. The
        // maximum has no branch to get backwards and gives 8, 6, 4, 2, 0.
        if (model.matchReady && model.state != nullptr && model.data != nullptr) {
            const cse::kernel::Box b0 =
                cse::kernel::Hurtbox(model.data->p[0], model.state->p[0]);
            const cse::kernel::Box b1 =
                cse::kernel::Hurtbox(model.data->p[1], model.state->p[1]);
            const std::int32_t right = b1.x0 - b0.x1;   // p1 is to the right
            const std::int32_t left  = b0.x0 - b1.x1;   // p1 is to the left
            chips += "    gap " + pixelsOf(right > left ? right : left);
        }
        top.line(chips, kDimCol);
    }

    // --- nothing loaded: say what broke, and stop ----------------------------
    if (!model.matchReady || model.state == nullptr || model.data == nullptr) {
        Pen pen(r2d, font, kMarginPx, top.y() + 14.0f, full);
        pen.row("match", "NOT STARTED", kAlarmCol);
        if (model.setupError != nullptr && !model.setupError->empty())
            pen.line(*model.setupError, kAlarmCol, kBodyScale);
        pen.gap(8.0f);
        pen.line("mode fixed ticks " + std::to_string(model.modeTicks) +
                     "   --  this counter moves even with no match, so a host "
                     "that is not calling this mode looks different from content "
                     "that did not load.",
                 kDimCol);
        pen.gap(8.0f);
        pen.line("ESC / BACK  return to the menu        C  next character",
                 kDimCol);
        return;
    }

    float columnTop = top.y() + 10.0f;

    // --- the loud line, above everything -------------------------------------
    {
        const Loud loud = loudLine(model);
        if (loud.show) {
            Pen pen(r2d, font, kMarginPx + kPadPx, columnTop + kPadPx,
                    full - kPadPx * 2.0f);
            pen.line(loud.headline, kLoudEdge, kBannerScale);
            pen.line(loud.detail, kValueCol, kSmallScale);
            const float h = pen.y() - columnTop + kPadPx - kRowGapPx;
            panel(r2d, kMarginPx, columnTop, full, h, kLoudFill, kLoudEdge);
            columnTop += h + kGutterPx;
        }
    }

    // --- the bottom edge, MEASURED AND THEN PLACED ---------------------------
    //
    // Laid out first even though it is drawn last in reading order, because its
    // height is what the two columns are allowed to occupy. It used to be pinned
    // at `H - kMarginPx - 52.0f` -- a constant standing in for the height of two
    // lines of text -- and the two columns had no bottom at all, which is how a
    // panel came to end at y730 in a 720 px window with the legend painted across
    // it. Both halves of that are gone: the strip knows how tall it is because it
    // laid itself out, and the columns are told where it starts.
    const float stripH =
        bottomStrip(r2d, font, kMarginPx, 0.0f, full, model, false);
    const float stripY = H - kMarginPx - stripH;
    bottomStrip(r2d, font, kMarginPx, stripY, full, model, true);

    const float columnBottom = stripY - kGutterPx;

    // --- left column: you, then the dummy ------------------------------------
    //
    // In that order because the player's panel is the one being read. See
    // fighterPanel for why the dummy's is shorter, and bottomStrip for where the
    // binding table went.
    {
        Column left(r2d, font, kMarginPx, columnTop, kLeftW, columnBottom);
        left.Add("your fighter", [&](float x, float y, float w, bool ink) {
            return fighterPanel(r2d, font, x, y, w, model, model.playerSlot, ink);
        });
        left.Add("the dummy", [&](float x, float y, float w, bool ink) {
            return fighterPanel(r2d, font, x, y, w, model, 1 - model.playerSlot,
                                ink);
        });
        left.Finish();
    }

    // --- right column: the analysis, the judge, and Demonstrate --------------
    //
    // DEMONSTRATE lives with the analysis because the analysis is what it
    // performs, and it goes LAST because it is the panel this screen can most
    // afford to lose -- see demoPanel. When there is no witness the key is NOT
    // left dead: the reason is printed, in the analysis's own vocabulary.
    {
        Column right(r2d, font, W - kMarginPx - kRightW, columnTop, kRightW,
                     columnBottom);
        right.Add("the analysis", [&](float x, float y, float w, bool ink) {
            return analysisPanel(r2d, font, x, y, w, model, ink);
        });
        right.Add("the combo judge", [&](float x, float y, float w, bool ink) {
            return comboPanel(r2d, font, x, y, w, model, ink);
        });
        right.Add("demonstrate", [&](float x, float y, float w, bool ink) {
            return demoPanel(r2d, font, x, y, w, model, ink);
        });
        right.Finish();
    }

    // A latching failure is fatal to the input log, so it takes the middle of the
    // screen rather than a corner of it. See LatchedInputSource::Latch.
    if (model.fatal != nullptr && !model.fatal->empty()) {
        const float y = H * 0.4f;
        Pen pen(r2d, font, kMarginPx + kPadPx, y + kPadPx, full - kPadPx * 2.0f);
        pen.line("THE MATCH STOPPED", kLoudEdge, kBannerScale);
        pen.line(*model.fatal, kValueCol, kSmallScale);
        panel(r2d, kMarginPx, y, full, pen.y() - y + kPadPx - kRowGapPx, kLoudFill,
              kLoudEdge);
    }
}

} // namespace untitledfighter
