# A GENERAL-PURPOSE EDITOR. Nothing in this directory names a game.
#
# It used to: src/panels/ComboProverPanel.{h,cpp} was a fighting-game authoring
# tool compiled in here, and the target_link_libraries(Editor PRIVATE CseData)
# that used to sit further down existed only to make it build. Both are gone.
# The panel now lives with the title that owns it, in
# Games/UntitledFighter/Editor/, and reaches this executable through the
# registry in src/EditorPanel.h. The one line that composes the two is in the
# ROOT CMakeLists.txt, which is already the only file allowed to know a title
# exists.
add_executable(Editor
    src/EditorApplication.cpp
    src/EditorApplication.h
    src/EditorImGuiLayer.cpp
    src/EditorImGuiLayer.h
    src/EditorTitleBar.cpp
    src/EditorTitleBar.h
    src/Subprocess.cpp
    src/Subprocess.h
    src/ImGuiInputMap.h
    src/UndoHistory.cpp
    src/UndoHistory.h
    # The panel seam a title plugs into. Not under panels/, deliberately: that
    # directory is the editor's own panels, and this is the hole one goes in.
    src/EditorPanel.h
    src/EditorPanel.cpp
    src/panels/AssetBrowserPanel.h
    src/panels/AssetBrowserPanel.cpp
    # What ships, and the button that produces it. General-purpose: it knows
    # about an ordered scene list, an output directory and a profile, and names
    # no game -- the same bar every other file in this directory has to clear.
    src/panels/BuildSettingsPanel.h
    src/panels/BuildSettingsPanel.cpp
    src/panels/SceneHierarchyPanel.h "src/panels/SceneHierarchyPanel.cpp" "src/panels/InspectorPanel.h" "src/panels/InspectorPanel.cpp" "src/EditorMain.cpp")


# App icon (see cse_add_app_icon in the root CMakeLists; it also declares the
# .ico as a real build input so regenerating it actually relinks).
cse_add_app_icon(Editor)

find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)

# Link the Engine library to the Editor executable
target_link_libraries(Editor PRIVATE Engine)
target_link_libraries(Editor PRIVATE imgui::imgui)
target_link_libraries(Editor PRIVATE imguizmo::imguizmo)

# NO GAME LIBRARIES HERE, and the absence is the point.
#
# This used to read `target_link_libraries(Editor PRIVATE CseData)` -- character
# data and the combo prover, linked so that a fighting-game panel could compile
# into a general-purpose editor. That is what this whole change was for. If you
# are about to add a game library back to this line, the panel that needs it
# belongs in that game's own Games/<title>/Editor/ instead; see src/EditorPanel.h
# for the seam and Games/UntitledFighter/Editor/ for a worked example.
#
# The editor keeps the ADR-001 research contribution #9 property either way --
# the designer sees the verdict on the character they are authoring, computed
# from the file that ships, with no export step in between. It is now delivered
# by the title rather than baked into the tool.

# Set different output directories for Debug and Release
set_target_properties(Editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/bin/RelWithDebInfo
)

# Stage runtime assets (shaders/models + seed scene) next to the exes.
# ONE staging target shared by Editor and both Players: concurrent copies
# into the same directory race under Ninja (intermittent sharing violations
# — same lesson as tests' test_runtime_deps), and a blind copy_directory
# clobbered editor-saved scenes with the checked-in copy every build. The
# script overwrites static assets but only seeds authored .json files.
#
# THE TARGET ITSELF MOVED TO THE ROOT CMakeLists.txt, and `src/Exported` moved
# with it -- from being THE asset root to being the FIRST of an ordered list the
# staging step layers, with each linked title's root going on top. That list is
# composition, composition means knowing which titles exist, and this file may
# not: it is the general-purpose editor, and the same rule that keeps a game
# library off the link line above keeps a game's asset root out of here. See the
# content-composition block in the root, beside the link composition it belongs
# with.
#
# The dependency stays here, where the target that needs it is declared. It is a
# forward reference to a target defined later in the top-level scope, which CMake
# resolves at generate time.
add_dependencies(Editor runtime_assets)

# NOTE: Player/ and Cooker/ each have a debug-info block here that adds -g//Zi to
# their OWN target. This directory deliberately has none. It used to have one,
# but it named `Engine` rather than `Editor` -- a copy-paste slip that made it
# inert in both directions: Engine already sets /Zi itself
# (Engine/CMakeLists.txt:342-347, where the /we4062 beside it is load-bearing),
# and CMake drops the duplicate, while the Editor target received nothing at all.
# Verified in the generated build.ninja before removing it: Engine's flags ended
# `-MD /Zi /we4062` and Editor's ended `-MD`, and deleting the block changed
# neither.
#
# There IS a real consequence, and it predates this removal rather than being
# caused by it: in Release, where nothing supplies /Zi globally, Engine, Player
# and Cooker all carry it from their own blocks and the Editor carries none. So
# the Editor is the one host that ships without PDBs. If you want them, add the
# block back naming `Editor` -- that is a one-line, deliberate decision about
# shipping debug info, which is exactly what it was not while it read `Engine`.
# RelWithDebInfo is unaffected either way: CMAKE_CXX_FLAGS_RELWITHDEBINFO gives
# every target /Zi there.

#target_compile_definitions(Editor PRIVATE MYCE_ENABLE_ENTRY=1)
target_compile_definitions(Editor PRIVATE GLFW_INCLUDE_NONE)

# WHERE THIS TREE IS, so the editor can build the player out of it.
#
# MyCoreEngine::BuildEnvironment (Engine/src/core/BuildPipeline.h) is "a plain
# struct the host fills, with no discovery hidden inside it", and it says why: a
# running executable does not know its CMake binary directory, and deriving it
# from the exe path (exeDir/../../..) is a layout assumption that breaks silently
# the day somebody changes an output directory. Only the build system knows, so
# the build system says.
#
# DEFINITIONS RATHER THAN THE CONFIGURE-TIME JSON THAT HEADER PROPOSES, and the
# deviation is deliberate. Its stated objection is that "baking absolute
# build-machine paths into Engine.dll puts them in every shipped game" -- an
# argument about Engine.dll. This is Editor.exe, which nothing installs (the only
# install rules in the tree are Engine's and PlayerShipping's), so there is no
# bundle for these to leak into. Two things also make definitions the better fit
# here specifically:
#   * $<CONFIG>. A file written by configure_file cannot carry it, and exeDir is
#     per-configuration; a generate-time file would work but is a second
#     mechanism for one string.
#   * The Editor links no JSON library. Reading a JSON environment file would
#     mean either adding nlohmann to this target or hand-parsing, to hold four
#     strings and two flags.
# An editor configured before this block existed simply leaves them undefined;
# EditorApplication::buildEnvironmentProblem_ notices and the Build Settings
# panel says "reconfigure with CMake" instead of failing further in.
get_property(_cse_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
target_compile_definitions(Editor PRIVATE
    CSE_BUILD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CSE_BUILD_BINARY_DIR="${CMAKE_BINARY_DIR}"
    # Matches the RUNTIME_OUTPUT_DIRECTORY_* settings above, which is where all
    # four applications and the staged runtime assets land.
    CSE_BUILD_EXE_DIR="${CMAKE_BINARY_DIR}/build/bin/$<CONFIG>"
    # The cmake that configured this tree, not whatever "cmake" a PATH search
    # would find. Visual Studio ships its own, and building a player with a
    # different CMake than the one that generated the tree is a class of failure
    # nobody enjoys diagnosing.
    CSE_BUILD_CMAKE_COMMAND="${CMAKE_COMMAND}"
    # Meaningful only on a single-config generator, which is the DEFAULT case
    # here: CMakePresets.json uses Ninja and so does CI. A profile whose
    # configuration differs from this one is a request this tree cannot satisfy,
    # and preflight refuses it by name rather than silently building the wrong
    # configuration under the right filename.
    CSE_BUILD_CONFIG_TYPE="${CMAKE_BUILD_TYPE}"
    # $<BOOL:> already yields exactly 1 or 0, including for an unset property.
    CSE_BUILD_MULTI_CONFIG=$<BOOL:${_cse_multi_config}>
)
