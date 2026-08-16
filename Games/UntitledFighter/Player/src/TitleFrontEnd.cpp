// The title's half of the FRONT-END seam: which scene a host boots when this
// game is in the build.
//
// One function, in its own translation unit beside UntitledFighterModes.cpp and
// for the same reason: this is the file somebody opens to answer "what does the
// game boot into", and a one-line answer buried at the bottom of a 250-line mode
// implementation is a one-line answer somebody edits the wrong copy of. The two
// seams are two files because they are two questions -- what the host can BE IN
// (a mode) and what the host STARTS AT (a scene).
//
// DECLARED by the engine (Engine/src/core/TitleFrontEnd.h, reached through
// Engine.h), DEFINED here. The CSE_HOST_TITLE_FRONT_END definition rides in on
// this target's INTERFACE, so linking this library is what turns the host's call
// site on and the link and the macro cannot get out of step. See
// ../CMakeLists.txt, which spells out both halves.
//
// Engine.h rather than a four-level relative climb into Engine/src: only
// Engine/include is on this target's include path (Engine/CMakeLists.txt exports
// exactly one PUBLIC directory), and every other file in this library already
// includes it. A ../../../../ path would compile today and would be the first
// thing to break the day anything moves.
#include "Engine.h"

#include <string>

namespace MyCoreEngine {

// ---------------------------------------------------------------------------
// WHAT THIS RETURNS, AND WHAT EACH PART OF IT MEANS
// ---------------------------------------------------------------------------
// CONTENT-ROOT-RELATIVE, with no "Exported/" on the front. The host joins it to
// whatever its content root is (PlayerMain.cpp does it in one line), because the
// host is the party that will still know where content lives once the asset
// SEARCH PATH lands and "the content root" stops being a single directory. It is
// also the convention this title's own code already uses everywhere it names
// content -- "Characters/fighter_a.json" in the mode and in the Combo Prover
// panel -- so "what is a content path around here" keeps one answer.
//
// UNDER A DIRECTORY OF THE TITLE'S OWN, which is not decoration:
//
//   * Games/UntitledFighter/Assets/ is layered ON TOP of the engine's asset root
//     into the single runtime Exported/ (cmake/stage_runtime_assets.cmake). Same
//     relative path in two roots means the title WINS and the engine's file is
//     not staged. Putting this menu at the top level would therefore have taken
//     over Exported/menu.json -- the ENGINE DEMO's front end, which a title-less
//     build must still boot and which tests/test_ui_menu.cpp asserts on. The
//     game owning its front end must not mean the game deleting the engine's.
//
//   * A top-level .json is SEED-ONLY: copied when missing and never pruned. So
//     one here would not merely collide by name, it would SURVIVE this title
//     being dropped from the build -- a game's menu left behind in a build tree
//     that no longer contains the game. Everything of this title's lives in
//     UntitledFighter/, which the staging mirror owns end to end and removes in
//     full the moment the title's add_subdirectory goes away.
//
// The string is COMPILED IN, not authored, so it is not the untrusted input
// PathSandbox exists for. A title that ever reads its front-end path out of a
// file of its own is the party that owes that check.
std::string TitleFrontEndScene() {
    return "UntitledFighter/menu.json";
}

} // namespace MyCoreEngine
