// "Untitled Fighting Game", as a mode a general-purpose host can enter.
//
// ---------------------------------------------------------------------------
// WHAT THIS IS, AND HONESTLY WHAT IT IS NOT
// ---------------------------------------------------------------------------
// This is the WIRING, and only the wiring. It builds a match out of the shipped
// character file, starts a FightSession, ticks it from the host's fixed step and
// draws what it can prove: the tick index, the state checksum, and whether the
// content loaded. There is no box view, no frame-data HUD, no Demonstrate
// button, no input mapping and no second player -- the training mode itself is
// the next piece of work and it goes on top of this, not instead of it.
//
// A mode that drew nothing until the training UI existed would leave the whole
// chain -- menu verb -> registry -> Enter -> fixed tick -> UI pass -- unprovable
// until the very end, which is how you find out three seams at once are wrong.
// What is on screen is chosen so that every link in that chain has a symptom if
// it is broken: the tick index moves only if FixedTick is being called, and the
// CHECKSUM moves only if cse::kernel::Simulate is actually running over a state
// the session owns.
//
// ---------------------------------------------------------------------------
// WHY THE FIGHT IS NOT A SCENE, WHICH IS THE WHOLE REASON MODES EXIST
// ---------------------------------------------------------------------------
// The Player's other trick is "load a saved scene and run it". A fight cannot be
// expressed that way: there is no entity, no Transform and no camera in it. Its
// world is an 80-byte POD of integers advanced by a pure function, and its
// picture is drawn in screen space by a 2D renderer. Engine/src/core/GameMode.h
// is the seam that makes room for that beside a scene without either one having
// to pretend to be the other.
#pragma once

// The whole engine surface through one header, the way both hosts include it --
// only Engine/include is a PUBLIC include directory, so there is no narrower
// include to write, and inventing one for a title would mean widening the
// engine's exported paths for a consumer's convenience.
#include "Engine.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/game/FightSession.h"

#include <cstdint>
#include <string>

namespace untitledfighter {

class UntitledFighterMode final : public MyCoreEngine::IGameMode {
public:
    // EXACTLY the string the main menu shows. The host draws it verbatim -- no
    // uppercasing, no truncation -- so this is the one place the game's name is
    // written, and changing it changes the menu.
    const char* DisplayName() const override { return "Untitled Fighting Game"; }

    bool Enter(const MyCoreEngine::GameModeContext& ctx, std::string& error) override;
    void Exit() override;
    void FixedTick(float dt) override;
    void Update(float dt) override;
    void Draw(MyCoreEngine::Renderer2D& r2d, int widthPx, int heightPx,
              float dt) override;

private:
    void drawLine_(MyCoreEngine::Renderer2D& r2d, const char* label,
                   const std::string& value, float x, float& y, float scale,
                   bool warn) const;

    MyCoreEngine::GameModeContext ctx_{};

    // BOTH SIDES OF THE MATCH, held for the session's whole life.
    //
    // FightSetup::data is BORROWED, not copied, and FightSession keeps that
    // pointer for every tick including the ones a rollback re-simulates. So the
    // MatchBuild has to outlive the session, which is why it is a member here
    // rather than a local in Enter -- a MatchData built on the stack and handed
    // over would be a dangling pointer by the first tick.
    cse::data::CharacterData character_{};
    cse::data::MatchBuild    build_{};
    cse::game::FightSession  session_{};

    // Why there is no match, in the loader's or the builder's OWN words. Drawn
    // verbatim: this mode has no better sentence to offer than the one the layer
    // that failed already wrote, and paraphrasing it would be a second error
    // vocabulary to keep in step with two others.
    std::string setupError_;
    bool        matchReady_ = false;

    // Frames this mode has been ticked, as distinct from the session's tick
    // index. TWO COUNTERS ON PURPOSE: without a match the session never ticks,
    // and a single counter frozen at zero cannot tell "the host is not calling
    // me" apart from "the content did not load". They separate those.
    std::uint64_t modeTicks_ = 0;
};

} // namespace untitledfighter
