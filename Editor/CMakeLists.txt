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