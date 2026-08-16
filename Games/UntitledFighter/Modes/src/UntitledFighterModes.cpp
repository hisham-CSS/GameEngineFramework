// The title's half of the game-mode seam: which modes this game contributes to
// a general-purpose host, and in what order.
//
// One function, kept in its own translation unit rather than at the bottom of
// UntitledFighterMode.cpp, because it is the file a second mode gets added to
// and it should be a file you can read in ten seconds -- a registration list
// buried under 250 lines of a mode's implementation is a registration list
// somebody edits the wrong copy of.
#include "UntitledFighterMode.h"

#include <memory>

namespace MyCoreEngine {

// DECLARED by the engine (Engine/src/core/GameMode.h), DEFINED here. Linking
// this library is what turns a host's call site on -- the CSE_HOST_TITLE_MODES
// definition rides in on this target's INTERFACE, so the link and the macro
// cannot get out of step. See Games/UntitledFighter/Modes/CMakeLists.txt.
//
// TWO HOSTS RUN THIS FUNCTION, not one: the shipped Player, and the Editor,
// which enters a mode in its Game view so that the preview and the shipped
// build cannot show different menus. Nothing below has to know which -- the
// registry it fills is the host's, and every mode registered here is handed a
// GameModeContext by whoever entered it (Engine/src/core/GameMode.h).
void RegisterTitleGameModes(GameModeRegistry& registry) {
    // REGISTRATION ORDER IS MENU ORDER (GameMode.h), and this is the whole
    // reason "Untitled Fighting Game" is the first thing on the main menu: the
    // menu markup names no game and holds four anonymous slots, so which one is
    // at the top is decided here, by the title, and by nothing in Engine/,
    // Player/ or Editor/. That is also why the editor's Game view shows the same
    // menu in the same order as the shipped build without either host holding a
    // list: both read this registry.
    registry.Add(std::make_unique<untitledfighter::UntitledFighterMode>());

    // The modes that follow this one are already named in the plan and are NOT
    // stubbed here, because a menu entry that enters an empty screen is worse
    // than one that is absent: REPLAY (drive a session from a ReplayInputSource
    // instead of a controller -- FightSession already takes one) and VERSUS over
    // CseNet's ISession. Both are the same shape as the mode above, both reuse
    // the same FightSession, and neither needs a change to this seam.
}

} // namespace MyCoreEngine
