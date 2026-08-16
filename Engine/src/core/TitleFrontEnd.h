#pragma once
// The seam a TITLE plugs its FRONT END into: which scene a host boots when this
// build has a game in it.
//
// ---------------------------------------------------------------------------
// WHY A HOST NEEDED TO BE TOLD THIS AT ALL
// ---------------------------------------------------------------------------
// The engine ships a demo, and it is a good demo: a main menu with a name field,
// a quality tier, a volume slider and a NEW GAME that loads a room with a
// backpack in it. The fighting game arrived as ONE MORE VERB ON THAT MENU --
// a button beside Quality and Volume, on a scene the engine owns, in markup the
// engine owns, living in the engine's asset root.
//
// That is backwards, and it is worth saying exactly how: the demo is the ENGINE
// showing off, and a shipped title was renting a button on it. Nobody sells a
// game whose title screen belongs to the toolkit it was built with. So the game
// owns the whole front end -- its own menu scene, its own markup, its own art --
// and this file is the one thing a general host has to be told in order to hand
// it over.
//
// A TITLE-LESS BUILD IS UNCHANGED, and that is not a nicety. Remove the title's
// add_subdirectory from the root CMakeLists and the guard below goes away with
// the library that carried it, the host compiles its call out, and the player
// boots the engine demo menu exactly as it does today. That property is what the
// boundary assertion in the root CMakeLists exists to protect, and a seam that
// broke it would be worse than no seam.
//
// ---------------------------------------------------------------------------
// A SCENE PATH, NOT A GAME MODE, AND THE OTHER WAY ROUND IS WORSE
// ---------------------------------------------------------------------------
// The cheap answer is next door: GameMode.h already has a registry, the Player
// already enters modes, and a title already registers them. Register the front
// end as mode zero, enter it at boot, done -- one seam instead of two. Four
// things say no, and the first one is on its own decisive.
//
//   * THE REGISTRY IS SINGLE-SLOT ON PURPOSE. GameModeRegistry::Enter leaves the
//     current mode first, because "two modes each believing they own the screen
//     is the failure this single-slot design exists to make impossible". A front
//     end that were a mode would therefore be EVICTED the moment the player
//     started a fight -- and when the fight asked to leave, DrainExitRequest
//     would call Leave() and land on nothing at all. Making that work needs a
//     mode stack, or a default mode the registry falls back to: a change to the
//     registry's core invariant, made to carry a thing modes are not for.
//   * A MODE DRAWS THROUGH Renderer2D. IGameMode::Draw is handed a 2D renderer
//     and a pixel size, which is right for a fight and useless for a front end:
//     the menu is a UIDocument -- markup, a stylesheet, flexbox, two-way
//     bindings, gamepad nav, a camera entity. A front-end mode would have to
//     re-implement all of that in C++, which means the title's front end could
//     no longer be AUTHORED. That is the opposite of the thing being asked for.
//   * OwnsScreen() == true SUPPRESSES THE SCENE'S UI DOCUMENTS. It has to: it is
//     what stops a live menu sitting underneath a fight taking clicks nobody can
//     see. A front end delivered as a mode would be suppressing the exact
//     mechanism it is made of.
//   * GAMEMODE.H ALREADY SAYS SO, in its first paragraph. A mode exists because
//     A FIGHT IS NOT A SCENE -- "none of them can be expressed as 'a .json with
//     a camera in it'". A front end is precisely a .json with a camera in it.
//     It is the case that seam was defined AGAINST, and pushing it through
//     anyway would make the sentence stop being true of its own header.
//
// So a title declares a SCENE, and the host loads it through the SceneSerializer
// path it already used for `settings.startupScene`. No new loading code, no new
// lifetime, no new ownership: the fight remains a mode ENTERED FROM that scene,
// exactly the way it is entered from the demo menu today. The two seams stay
// what they each are -- this one says what the host boots, GameMode.h says what
// the host can be in.
//
// ---------------------------------------------------------------------------
// THE PATH IS RELATIVE TO THE CONTENT ROOT, AND THE HOST DOES THE JOIN
// ---------------------------------------------------------------------------
// Return "UntitledFighter/menu.json", not "Exported/UntitledFighter/menu.json".
// The host knows where its content root is -- it is the same string it puts in
// GameModeContext::contentRoot -- and it is the only party that will still know
// once the asset SEARCH PATH lands, at which point "the content root" stops
// being one directory and this seam does not have to change. It is also the
// convention the title's own code already uses everywhere it names content
// ("Characters/fighter_a.json" in the mode and in the Combo Prover panel), so
// the answer to "what is a content path around here" stays one answer.
//
// The returned path is COMPILED-IN, not authored, so it is not the untrusted
// input PathSandbox exists for -- a title that wants to read the name out of a
// file of its own is the party that owes the sandbox check.
#include <string>

namespace MyCoreEngine {

#ifdef CSE_HOST_TITLE_FRONT_END
    // DEFINED BY THE TITLE, not by the engine and not by any host -- the same
    // idiom as MyCoreEngine::RegisterTitleGameModes and editor::RegisterTitlePanels,
    // and for the same reason: the engine declares, a game defines, and the LINK
    // is where the two meet. NOT ENGINE_API, because it lives in whatever archive
    // the executable links rather than in the engine DLL, and marking it
    // dllimport would send the linker looking in the wrong place.
    //
    // A SEPARATE GUARD FROM CSE_HOST_TITLE_MODES, deliberately. Both ride in as
    // INTERFACE compile definitions on a title's own player library, so linking
    // is what turns each on; keeping them apart is what lets a title have modes
    // and no front end (which is what this one had until its menu moved out of
    // the engine's asset root) or a front end and no modes (a scene-driven game
    // with a title screen, which is most games). Folding them into one macro
    // would force every title to define both functions or fail to link, and the
    // second one would be a stub returning nothing -- a definition that exists to
    // satisfy a linker teaches a reader nothing.
    //
    // Called ONCE, during host startup, before the first scene load.
    //
    // EMPTY MEANS "no front end of my own": the host boots whatever it would
    // have booted, and says nothing about it. That is not the same as the guard
    // being off -- it is the escape for a title that decides at runtime it has
    // nothing to show, and it costs the host no extra branch because the empty
    // string already falls through the precedence chain.
    //
    // WHO WINS is the HOST's decision and it is documented where it is made,
    // in Player/src/PlayerMain.cpp: a scene named on the command line beats
    // this, this beats project.json's startupScene, and the losing case is
    // reported rather than silently ignored.
    std::string TitleFrontEndScene();
#endif

} // namespace MyCoreEngine
