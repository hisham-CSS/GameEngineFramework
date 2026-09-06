// The title's half of the game-mode seam: which modes this game contributes to
// a general-purpose host, and in what order.
//
// One function, kept in its own translation unit rather than at the bottom of
// UntitledFighterMode.cpp, because it is the file the menu order lives in and
// it should be a file you can read in ten seconds -- a registration list
// buried under 1500 lines of a mode's implementation is a registration list
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
    using untitledfighter::ModeIntent;
    using untitledfighter::UntitledFighterMode;

    // REGISTRATION ORDER IS MENU ORDER (GameMode.h), and this is the whole
    // reason "Untitled Fighting Game" is the first thing on the main menu: the
    // engine's menu markup names no game and holds four anonymous slots, so
    // which one is at the top is decided here, by the title, and by nothing in
    // Engine/, Player/ or Editor/. That is also why the editor's Game view shows
    // the same menu in the same order as the shipped build without either host
    // holding a list: both read this registry.
    //
    // THREE ENTRIES, ONE CLASS (ADR-022 D1). Training, replay and versus are
    // the same fight with the second slot's bits from a different place -- a
    // silent dummy, a replay file, a peer over CseNet -- so they are one mode
    // constructed three times with a ModeIntent, not three modes. The title's
    // own front end types three verbs and wires them to slots 0, 1 and 2 in
    // THIS order (Assets/UntitledFighter/UI/menu.cxml); reorder these lines and
    // that file's on-click slots move with them. The registry test
    // (FightMode.ThreeRegistryEntriesShareOneModeAndOnePresentation) pins the
    // count and the order.
    registry.Add(std::make_unique<UntitledFighterMode>(ModeIntent::Training));
    registry.Add(std::make_unique<UntitledFighterMode>(ModeIntent::Replay));
    registry.Add(std::make_unique<UntitledFighterMode>(ModeIntent::Versus));
}

} // namespace MyCoreEngine
