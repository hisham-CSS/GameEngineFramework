# The title's runtime modes: what this game contributes to a general-purpose
# host that can enter, tick, draw and leave a mode.
#
# ---------------------------------------------------------------------------
# THE DIRECTION OF THE ARROW
# ---------------------------------------------------------------------------
# This library depends on the Engine, on the headless game core, and on the
# game-mode seam in Engine/src/core/GameMode.h. Nothing under Player/ or Engine/
# depends on it. Games/UntitledFighter/CMakeLists.txt states the rule -- a title
# may depend on the engine, the engine may never depend on a title -- and this is
# the runtime host's instance of it, the same shape as the Editor's in
# ../Editor/CMakeLists.txt.
#
# Player/src/PlayerMain.cpp names no game. The composition -- both players link
# this -- is one place in the ROOT CMakeLists.txt, already the only file in the
# tree allowed to know a title exists.
#
# ---------------------------------------------------------------------------
# WHY THIS IS NOT PART OF CseGame
# ---------------------------------------------------------------------------
# Games/UntitledFighter/Game/CMakeLists.txt asserts, at configure time, that
# CseGame links exactly CseKernel and CseData -- no Engine, no GL, no renderer --
# because that is what makes every claim it proves re-runnable in a unit test
# with no window. This library is the first thing above that line: it renders, it
# reads a controller, it holds a Font. Putting a mode in CseGame would trip that
# assertion, and it should: the headless core stays headless and the presentation
# sits on top of it.
add_library(UntitledFighterPlayer STATIC
    src/UntitledFighterMode.h
    src/UntitledFighterMode.cpp
    # The registration list, and the definition of the hook the engine declares.
    src/UntitledFighterModes.cpp
)

target_compile_features(UntitledFighterPlayer PUBLIC cxx_std_17)
set_target_properties(UntitledFighterPlayer PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Engine.h reaches GLFW through Window.h, and GLFW must be told not to drag in
# its own GL headers -- glad owns those. Both hosts define this for the same
# reason; a library that includes Engine.h needs it just as much.
target_compile_definitions(UntitledFighterPlayer PRIVATE GLFW_INCLUDE_NONE)

# LINKING THIS LIBRARY IS THE REGISTRATION.
#
# PUBLIC, which is PRIVATE and INTERFACE, and both halves are load-bearing:
#
#   INTERFACE  turns on the ENGINE's declaration of
#              MyCoreEngine::RegisterTitleGameModes and the HOST's call to it.
#              It has to ride the LINK LINE, because that is what makes the link
#              and the macro impossible to get out of step: one place in the root
#              does both, and a host that links nothing compiles the hook out and
#              still links clean.
#   PRIVATE    turns the same declaration on while compiling THIS library, so the
#              compiler CHECKS the definition in UntitledFighterModes.cpp against
#              the engine's declaration. Without it the guard hides the
#              declaration here, a changed signature compiles happily into a
#              differently-mangled symbol, and the mismatch surfaces as an
#              unresolved external in the player naming a function the file you
#              are looking at appears to define.
#
# The one remaining way to be wrong -- define without link -- is an unresolved
# external, which is loud. The silent one (link without define, so the archive is
# referenced by nothing and the linker drops it) is what the INTERFACE half rules
# out.
target_compile_definitions(UntitledFighterPlayer PUBLIC CSE_HOST_TITLE_MODES=1)

# BOTH PRIVATE, and that is a statement about this library rather than a default.
#
# THIS TARGET EXPORTS NO HEADERS AT ALL -- there is no target_include_directories
# with a PUBLIC entry above, deliberately. UntitledFighterMode.h is internal;
# the entire surface a host sees is MyCoreEngine::RegisterTitleGameModes, which
# the ENGINE declares. So a host that links this needs neither Engine.h paths nor
# cse/ paths on account of this library, and PRIVATE says exactly that. (Both
# still reach the consumer's LINK line -- CMake propagates a static library's
# private dependencies as link-only -- so nothing is missing at link time; it is
# only the include paths and the compile definitions that stop here.)
#
#   Engine   the mode derives from IGameMode, draws through Renderer2D, measures
#            with Font and reads Application's InputMap.
#   CseGame  the fight itself. PUBLIC CseKernel CseData in its own file, so this
#            one line brings the simulation, the character loader and
#            MatchBuilder with it.
#
# What crosses from here to the host is an IGameMode pointer and one function
# call. Nothing of cse::game, cse::data or cse::kernel appears in it -- which is
# what lets a general Player executable link this archive and still name no
# fighter, no hitbox and no move anywhere in Player/src.
target_link_libraries(UntitledFighterPlayer PRIVATE Engine CseGame)
