# The title's presentation library, with no GL in it (ROADMAP M3.4a; ADR-019 D9).
#
# WHAT THIS TARGET IS FOR. Everything that turns a GameState into a PICTURE
# without touching a renderer: which frame of which clip each fighter wears
# (composed on top of CseGame's SelectPose), the stateless cycle phases, the
# clip table (M3.4b) and the matrices, camera framing and look the mode hands
# to the scene (M3.4c). The mode (Games/UntitledFighter/Modes/) stays the thin
# thing that OWNS a window and CALLS this.
#
# WHY IT IS A SEPARATE LIBRARY AND NOT PART OF THE MODE. The mode links Engine,
# and Engine drags GL, a window, Jolt and Lua onto the link line, so nothing the
# mode contains can be asserted in a test without a context. Every claim the 3D
# presentation makes -- pose is a pure function of sim state, nothing survives a
# Restore, a box never moves with the pose, the fight camera and the scene camera
# agree to half a pixel -- is arithmetic over GameState and MatchData, and
# arithmetic is provable headless. Keeping that arithmetic here, where Engine is
# not, is what keeps DETERMINISM.md P4 a test rather than a sentence.
#
# THIS IS ALSO THE SPIKE. M3.4a lands this library with one translation unit so
# that the link -- into UntitledFighterModes, and into a headless test in all
# three CI jobs -- is proven BEFORE five later work packages ride on it.
add_library(UntitledFighterPresentation STATIC
    src/CycleFrame.cpp     # floor-div/floor-mod cycle phases; half the stage is negative
    src/FighterClips.cpp   # the (kind, move slot) -> clip table, bound by move id on every adopt (M3.4b)
    src/FightPresentation.cpp # the frame: pose -> clip -> frame -> matrix, the camera framing, the look (M3.4c)
)

target_include_directories(UntitledFighterPresentation
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(UntitledFighterPresentation PUBLIC cxx_std_17)
set_target_properties(UntitledFighterPresentation PROPERTIES POSITION_INDEPENDENT_CODE ON)

# PUBLIC, because FightPresentation (M3.4c) composes SelectPose and hands back
# kernel-typed positions; a consumer needs cse/game and cse/kernel on its path.
#
# TWO HEADER-ONLY LIBRARIES ADMITTED IN M3.4c, each for one reason. glm: the
# model matrix and the two camera projections ARE the arithmetic this library
# exists to prove (a yaw with determinant +1, the same pixel through both
# cameras), and glm is header-only and GL-free -- the note below said it would
# be admissible for exactly this. PUBLIC because FrameComposition carries
# glm::mat4. nlohmann: fight_look.json is read here so the test that proves the
# committed file parses it through the SAME function the mode does; PRIVATE,
# as CseData holds it, so no consumer inherits the JSON library. Neither is
# Engine, and Engine is still never here. Games/ is configured before Engine/,
# so both packages are found here rather than borrowed.
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(UntitledFighterPresentation PUBLIC CseGame glm::glm)
target_link_libraries(UntitledFighterPresentation PRIVATE nlohmann_json::nlohmann_json)

# --- The guarantee, enforced ------------------------------------------------
#
# The same shape as CseGame's whitelist, for the same reason: a blacklist is the
# list of mistakes somebody thought of. If this fires because a later WP needs
# glm for matrices, that is a real decision -- a glm-only target is header-only
# and GL-free, so it is admissible; Engine is not, and never will be here.
# Record the reason beside the new name.
get_target_property(_pres_links UntitledFighterPresentation LINK_LIBRARIES)
if (NOT _pres_links STREQUAL "CseGame;glm::glm;nlohmann_json::nlohmann_json")
    message(FATAL_ERROR
        "UntitledFighterPresentation links something other than CseGame, glm and nlohmann: ${_pres_links}\n"
        "The presentation library must stay GL-free and Engine-free -- that is "
        "what lets DETERMINISM.md P4 be asserted in a headless test. See the "
        "note above this check.")
endif()
unset(_pres_links)
