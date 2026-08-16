#include "UntitledFighterMode.h"

#include <cstdio>
#include <string>

namespace untitledfighter {

namespace {

    // The character both sides fight with. ONE FILE, MIRRORED, because a mirror
    // match is the setup in which nothing that happens can be blamed on the two
    // sides having different data -- the same reason tests/test_game_core.cpp's
    // harness mirrors. Which characters a real training session picks is a
    // training-mode decision and is not made here.
    //
    // Relative to the host's content root, resolved through CseData's path
    // sandbox: LoadCharacterFile refuses absolute paths, drive/UNC roots and any
    // ".." component lexically, before touching the filesystem. It is a constant
    // here rather than something a player types, and it goes through the sandbox
    // anyway, because the rule in docs/MAINTENANCE.md has no exceptions and a
    // constant today is a config field tomorrow.
    constexpr const char* kCharacterFile = "Characters/fighter_a.json";

    // Screen furniture. Named rather than inlined so the numbers below read as a
    // layout rather than as magic.
    constexpr float kMarginPx     = 48.0f;
    constexpr float kLineGapPx    = 8.0f;
    // Multipliers on the font's baked atlas height (kUIFontAtlasPixels, 48px), so
    // these are ~34px and ~18px. kUIFontBaseScale IS "ordinary text" and is used
    // as such rather than being re-derived.
    constexpr float kTitleScale   = 0.70f;
    constexpr float kBodyScale    = MyCoreEngine::kUIFontBaseScale;

    const glm::vec4 kBackdrop{ 0.05f, 0.05f, 0.07f, 1.0f };
    const glm::vec4 kTitleCol{ 0.96f, 0.96f, 0.98f, 1.0f };
    const glm::vec4 kLabelCol{ 0.55f, 0.57f, 0.64f, 1.0f };
    const glm::vec4 kValueCol{ 0.86f, 0.88f, 0.92f, 1.0f };
    const glm::vec4 kWarnCol { 0.94f, 0.55f, 0.35f, 1.0f };
    const glm::vec4 kHintCol { 0.45f, 0.47f, 0.54f, 1.0f };

    std::string hex32(std::uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", v);
        return std::string(buf);
    }

} // namespace

bool UntitledFighterMode::Enter(const MyCoreEngine::GameModeContext& ctx,
                                std::string& error) {
    ctx_ = ctx;
    modeTicks_ = 0;
    matchReady_ = false;
    setupError_.clear();

    // ENTER SUCCEEDS EVEN WHEN THE CONTENT DOES NOT LOAD, and that is a
    // deliberate reading of IGameMode::Enter's contract rather than laziness.
    // Returning false would bounce the player back to a menu with one truncated
    // line of status, and would make the whole chain this mode exists to prove
    // -- menu verb, registry, fixed tick, UI pass -- unexercisable on any
    // machine where the character file did not stage. A mode that can still show
    // something honest should show it, and what is honest here is the loader's
    // own error, full width, next to a tick counter that proves the host is
    // calling us regardless.
    //
    // `error` is therefore left empty and this returns true on every path below.
    cse::data::LoadReport loadReport{};
    cse::data::LoadOptions loadOptions{};
    // expectedResources deliberately empty: assertion A03 is a CROSS-FILE rule
    // about a whole build's resource ORDER, and this mode loads one file. Naming
    // an order here would be this file inventing a build-wide contract. The
    // check is SKIPPED rather than passed, and CharacterData.h records that as a
    // warning for exactly this reason.

    if (!cse::data::LoadCharacterFile(ctx_.contentRoot, kCharacterFile,
                                      loadOptions, character_, loadReport)) {
        setupError_ = "load " + std::string(kCharacterFile) + ": " +
                      (loadReport.rule.empty() ? "" : loadReport.rule + ": ") +
                      loadReport.error;
        return true;
    }

    cse::data::BuildOptions options{};
    // The documented defaults, PASSED BY NAME rather than left at zero.
    //
    // Omitting them gets the same numbers plus a warning, which would be fine if
    // anything drew the warning. Naming them puts the decision at the call site
    // where the next person can see there is one to make: a body size is a
    // training-mode design choice (it decides what "in range" means on screen)
    // and it belongs to whoever builds the box view, not to the wiring.
    options.body.halfWidthSub = cse::data::kDefaultBodyHalfWidthSub;
    options.body.heightSub    = cse::data::kDefaultBodyHeightSub;
    // NO BUTTON BINDINGS. The schema carries no input notation (MatchBuilder.h),
    // so which button starts which move is supplied by the caller -- and this
    // mode reads no controller yet, so binding moves nothing could press would
    // be a table with no reader. It arrives with the input mapping.

    if (!cse::data::BuildMatchData(character_, options, character_, options,
                                   build_)) {
        // Both sides are the same character, so a per-side report cannot
        // disagree; take whichever one actually says something.
        const std::string& e0 = build_.report[0].error;
        setupError_ = "build match: " + (e0.empty() ? build_.report[1].error : e0);
        return true;
    }

    cse::game::FightSetup setup{};
    // BORROWED for the session's whole life -- build_ is a member for exactly
    // this reason (see the header).
    setup.data = &build_.data;
    // MatchStart's own defaults: -100px / +100px, a 174px body-to-body gap that
    // is further than anything either shipped character reaches. Left alone
    // deliberately. Choosing a training distance is a decision about what the
    // mode is TEACHING and it belongs with the box view and the frame-data HUD;
    // a number invented here would be one the next agent has to find and undo.
    // Nothing connects at this range, which is exactly what the screen says.

    std::string beginError;
    if (!session_.Begin(setup, beginError)) {
        setupError_ = "begin match: " + beginError;
        return true;
    }

    matchReady_ = true;
    return true;
}

void UntitledFighterMode::Exit() {
    // Safe after a FAILED Enter, which IGameMode::Exit requires: every member
    // touched here is default-constructible and was default-constructed, and
    // nothing below cares how far Enter got.
    //
    // The session is NOT re-Begun here. FightSession::Begin is itself the
    // restart (it memsets the state and resets the tick index), so leaving and
    // re-entering starts a fresh match through one code path rather than two.
    matchReady_ = false;
    setupError_.clear();
    modeTicks_ = 0;
    ctx_ = MyCoreEngine::GameModeContext{};
}

void UntitledFighterMode::FixedTick(float dt) {
    (void)dt;   // the session takes no dt at all: it is a tick index, not a clock
    ++modeTicks_;
    if (!matchReady_) return;

    // ONE TICK PER FIXED STEP, and the caveat is written down because it is the
    // thing that will be wrong first.
    //
    // FightSession.h disqualifies Application's FixedTimestep from driving a
    // session in general: it caps at 8 steps per frame and then ZEROES the
    // accumulator, so a frame that stalls silently DROPS the backlog. For this
    // mode, today, that is a hitch -- a local training session with no opponent
    // and no recording has nothing that a missing tick can desync against.
    //
    // IT STOPS BEING ACCEPTABLE THE MOMENT ANY OF THESE ARRIVE, and each is
    // planned: a replay being recorded (the file would claim a tick count the
    // inputs do not account for), a replay being verified (the checksums walk),
    // or a rollback session (a dropped tick is a lost input and an
    // unrecoverable desync). At that point this call must stop trusting the host
    // to arrive the right number of times and drive its own accumulator, or take
    // its ticks from ISession's Advance events. Nothing about this seam has to
    // change for that -- FixedTick is still where the host says "a step's worth
    // of time passed"; only what this function does with it does.
    //
    // No input source is bound, so both fighters are fed NEUTRAL every tick.
    // Neutral and never "repeat the last tick": FightSession.h explains why at
    // length, and it means the state advances but nobody moves -- which is
    // precisely what makes the tick index and the checksum a clean test of
    // whether Simulate is running at all.
    session_.Tick();
}

void UntitledFighterMode::Update(float dt) {
    (void)dt;
    if (!ctx_.app) return;

    // Escape (and gamepad BACK) leaves the mode.
    //
    // THE MODE CONSUMES THIS, NOT THE HOST, and the host must be suppressing its
    // own handling of it while a mode owns the screen -- "Quit" is the same
    // action RunLoop closes the window on, so a mode that owned the screen
    // without the host standing down would find Back-to-menu quitting the game.
    // IGameMode::OwnsScreen documents that pairing; this is the half of it that
    // lives in the mode.
    //
    // It is read here rather than in FixedTick because leaving is presentation,
    // not simulation: it must not be able to land on a different tick depending
    // on how many fixed steps a frame happened to run.
    if (ctx_.app->input().wasPressed("Quit")) requestExit();
}

void UntitledFighterMode::drawLine_(MyCoreEngine::Renderer2D& r2d,
                                    const char* label, const std::string& value,
                                    float x, float& y, float scale,
                                    bool warn) const {
    // Non-null by construction: Draw returns early when the host's font failed
    // to bake, and Draw is the only caller. Stated rather than re-checked --
    // a second null test here would suggest there is a path that reaches this
    // without one, and there must not be.
    const MyCoreEngine::Font& font = *ctx_.font;
    // A fixed label column, so the values line up and the block reads as a
    // table rather than as prose. FIXED and not measured: aligning to the widest
    // label means measuring every label before drawing any of them, which is a
    // layout pass, and there is a layout engine one directory over for anything
    // that needs one. 190px clears the longest label here at this scale.
    constexpr float kLabelColumnPx = 190.0f;
    r2d.DrawText(font, label, { x, y }, kLabelCol, 1, scale);
    r2d.DrawText(font, value, { x + kLabelColumnPx, y },
                 warn ? kWarnCol : kValueCol, 1, scale);
    // An empty value still advances a full row: Font::Measure returns
    // (0, lineHeight) for "" precisely so a caller does not have to special-case
    // it, and a blank line in a table is still a line.
    y += font.Measure(value, scale).y + kLineGapPx;
}

void UntitledFighterMode::Draw(MyCoreEngine::Renderer2D& r2d, int widthPx,
                               int heightPx, float dt) {
    (void)dt;

    // Opaque, full screen. This mode owns the screen (OwnsScreen), and the 3D
    // pass still ran over whatever scene the host had loaded, so without this
    // the fight's readout would be painted over a menu backdrop it has nothing
    // to do with.
    r2d.DrawQuad({ 0.0f, 0.0f },
                 { static_cast<float>(widthPx), static_cast<float>(heightPx) },
                 kBackdrop, 0);

    // A font that failed to bake leaves the host drawing nothing at all rather
    // than crashing -- Font is documented as safe-but-inert in that state, but
    // dereferencing a null one is not, and the host's own font load is allowed
    // to fail (it prints and carries on). The backdrop above is still drawn, so
    // "the mode is up but the font is missing" looks different from "nothing
    // happened".
    if (!ctx_.font) return;

    const MyCoreEngine::Font& font = *ctx_.font;
    float x = kMarginPx;
    float y = kMarginPx;

    // The name, from ONE place: whatever the menu button said, this says.
    r2d.DrawText(font, DisplayName(), { x, y }, kTitleCol, 1, kTitleScale);
    y += font.Measure(DisplayName(), kTitleScale).y + kLineGapPx * 3.0f;

    if (matchReady_) {
        drawLine_(r2d, "character", character_.id, x, y, kBodyScale, false);
        // The two numbers that make this a test rather than a splash screen.
        //
        // TICK moves only if the host is calling FixedTick. CHECKSUM is FNV-1a
        // over the state's bytes and moves only if cse::kernel::Simulate really
        // ran over a state this session owns -- a mode that faked its tick
        // counter would show a frozen checksum beside a climbing index, and the
        // two disagreeing is exactly the symptom worth being able to see.
        drawLine_(r2d, "session tick",
                  std::to_string(session_.CurrentTick()), x, y, kBodyScale, false);
        drawLine_(r2d, "state checksum", hex32(session_.Checksum()), x, y,
                  kBodyScale, false);
    } else {
        drawLine_(r2d, "match", "NOT STARTED", x, y, kBodyScale, true);
        // Verbatim, and wrapped by nothing: it is one line from a layer that
        // already wrote a good sentence, and truncating it here would hide the
        // filename, which is usually the whole answer.
        drawLine_(r2d, "", setupError_, x, y, kBodyScale, true);
    }

    // Always drawn, match or no match. Without a match the session never ticks,
    // so this is the counter that separates "the content did not load" from "the
    // host is not calling this mode at all" -- two failures that look identical
    // on a screen with one number on it.
    drawLine_(r2d, "mode fixed ticks", std::to_string(modeTicks_), x, y,
              kBodyScale, false);
    if (ctx_.app) {
        char hz[32];
        std::snprintf(hz, sizeof(hz), "%.1f Hz", ctx_.app->fixedTimestepHz());
        // The host's rate, not an assumption about it. A fight is authored in
        // 1/60 frames; a host running this at some other rate is a real bug and
        // this is where it is visible instead of being felt as "the game is
        // slightly wrong".
        drawLine_(r2d, "host fixed rate", hz, x, y, kBodyScale, false);
    }

    y += kLineGapPx * 2.0f;
    // Say what this screen is. Somebody will see it before the training mode
    // exists and a screen that looked like a finished feature would read as a
    // broken one.
    //
    // A local rather than an inline literal so the ADVANCE below measures the
    // string that was actually drawn -- '\n' starts a new line in DrawText, and
    // measuring a stand-in with the same line count is one edit away from being
    // wrong about a text it no longer resembles.
    const std::string what =
        "This mode is the plumbing, not the game: the fight ticks and\n"
        "nothing is drawn of it yet. Box view, frame-data HUD and\n"
        "Demonstrate come next.";
    r2d.DrawText(font, what, { x, y }, kHintCol, 1, kBodyScale);
    y += font.Measure(what, kBodyScale).y + kLineGapPx * 2.0f;

    r2d.DrawText(font, "ESC / BACK  return to the menu", { x, y }, kHintCol, 1,
                 kBodyScale);
}

} // namespace untitledfighter
