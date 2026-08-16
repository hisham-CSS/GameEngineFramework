# The title's FRONT END: which scene a host BOOTS when this game is in the build.
#
# ---------------------------------------------------------------------------
# WHY THIS IS ITS OWN LIBRARY AND NOT THREE LINES IN ../Modes/
# ---------------------------------------------------------------------------
# It was three lines in the same library, and it made a host take both seams or
# neither. That is precisely the thing Engine/src/core/TitleFrontEnd.h spends a
# paragraph ruling out: CSE_HOST_TITLE_FRONT_END is a SEPARATE guard from
# CSE_HOST_TITLE_MODES "so that a title can have modes and no front end ... or a
# front end and no modes". Two independent switches on one library is one switch
# wearing two names -- the macros stayed apart and the LINK could not.
#
# The case that forced it is real and is in this build today. The EDITOR wants
# the modes, because entering a mode in the Game view is how the editor previews
# what ships. It does NOT want the front end: an editor OPENS a scene, it does
# not BOOT one, and TitleFrontEndScene answers a question ("what does this host
# start at") that an editor never asks. Handing it the front-end guard as well
# would define a macro that turns on a call site the Editor does not have, which
# is a definition that means nothing -- and the day the Editor grew such a call
# site it would boot the game's menu instead of the scene the designer opened.
#
# So: ONE LIBRARY PER SEAM, and the host composes.
#
#   UntitledFighterModes      MyCoreEngine::RegisterTitleGameModes  (both hosts)
#   UntitledFighterFrontEnd   MyCoreEngine::TitleFrontEndScene      (players only)
#   UntitledFighterEditor     editor::RegisterTitlePanels           (editor only)
#
# ---------------------------------------------------------------------------
# ONE FUNCTION IS ENOUGH TO BE A LIBRARY
# ---------------------------------------------------------------------------
# This archive holds a single translation unit returning a single string, and
# that is not a sign it should be folded into something bigger. Its size is not
# what makes it a unit -- being SEPARATELY COMPOSABLE is, and a library is the
# only granularity at which a host can take one seam and leave another. Every
# other directory under this title is also exactly one library, so this is the
# established shape here rather than a new idea.
#
# It will not grow much, and that is by design too: a front end that needed C++
# would be the wrong front end. The menu is a UIDocument -- markup, a stylesheet,
# flexbox, bindings, gamepad nav -- and it lives in ../Assets/UntitledFighter/.
# TitleFrontEnd.h argues at length why the front end is a SCENE PATH rather than
# a game mode; the consequence for this file is that the code half stays one line
# and the authored half is where the work happens.
add_library(UntitledFighterFrontEnd STATIC
    # Which scene the host boots when this game is in the build. The scene it
    # names, and the markup, stylesheet and art behind it, are in
    # ../Assets/UntitledFighter/.
    src/TitleFrontEnd.cpp
)

target_compile_features(UntitledFighterFrontEnd PUBLIC cxx_std_17)
set_target_properties(UntitledFighterFrontEnd PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Engine.h reaches GLFW through Window.h, and GLFW must be told not to drag in
# its own GL headers -- glad owns those. This library includes Engine.h for one
# declaration and needs the flag exactly as much as one that uses the whole API;
# TitleFrontEnd.cpp says why it is Engine.h rather than a narrower header (only
# Engine/include is a PUBLIC include directory, so there is no narrower one to
# write).
target_compile_definitions(UntitledFighterFrontEnd PRIVATE GLFW_INCLUDE_NONE)

# LINKING THIS LIBRARY IS THE REGISTRATION, on exactly the terms
# ../Modes/CMakeLists.txt spells out for the other seam.
#
#   INTERFACE  turns on the ENGINE's declaration of MyCoreEngine::TitleFrontEndScene
#              and the host's call to it. It rides the LINK LINE so the link and
#              the macro cannot get out of step.
#   PRIVATE    turns the same declaration on while compiling src/TitleFrontEnd.cpp,
#              so the compiler CHECKS this library's definition against the
#              engine's declaration rather than letting a changed signature
#              compile into a differently-mangled symbol.
#
# THE CONTENT IS NOT COMPOSED HERE. The scene this switch makes the host boot
# lives in ../Assets/, which the ROOT CMakeLists layers onto the engine's asset
# root -- by the convention `Games/<title>/Assets`, gated on this title being in
# the build. So there is no asset path in this file and no build rule to keep in
# step with one: linking the library is what brings the code, and the title's one
# add_subdirectory is what brings the content.
target_compile_definitions(UntitledFighterFrontEnd PUBLIC CSE_HOST_TITLE_FRONT_END=1)

# Engine, PRIVATE, for the include path to Engine.h and for nothing else -- this
# library calls no engine function and returns a std::string. PRIVATE means a
# host that links this gains no include directories on account of it, which is
# the same statement ../Modes/CMakeLists.txt makes about its own two links.
target_link_libraries(UntitledFighterFrontEnd PRIVATE Engine)
