# Standalone player: Engine only, no ImGui/editor dependencies.
# Built twice from the same source:
#   PlayerDebug   — console subsystem: keeps the terminal window for logs (dev)
#   PlayerShipping — WIN32 subsystem, no console; OUTPUT_NAME "Player" so the
#                    packaged bundle ships a clean Player.exe
add_executable(PlayerDebug src/PlayerMain.cpp)
add_executable(PlayerShipping src/PlayerMain.cpp)

foreach(tgt PlayerDebug PlayerShipping)
    target_link_libraries(${tgt} PRIVATE Engine)
    target_compile_definitions(${tgt} PRIVATE GLFW_INCLUDE_NONE)

    # Same output dir as the Editor so they share Exported/ (a scene saved in
    # the editor is immediately runnable in the player).
    set_target_properties(${tgt} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${tgt} PRIVATE -g)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${tgt} PRIVATE /Zi)
    endif()
endforeach()

set_target_properties(PlayerShipping PROPERTIES
    OUTPUT_NAME "Player"
    WIN32_EXECUTABLE TRUE)   # /SUBSYSTEM:WINDOWS — no console window
target_compile_definitions(PlayerShipping PRIVATE MYCE_SHIPPING=1)
if (MSVC)
    # keep the plain main() from Main.h despite the WINDOWS subsystem
    target_link_options(PlayerShipping PRIVATE "/ENTRY:mainCRTStartup")
endif()

# Runtime assets are staged by the shared runtime_assets target (defined in
# Editor/CMakeLists.txt — the players share the same output directory). One
# writer, no copy races, and editor-authored files are never clobbered.
add_dependencies(PlayerDebug runtime_assets)
add_dependencies(PlayerShipping runtime_assets)

# --- Packaging: the standalone game bundle ships the shipping player ---
# .import sidecars are editor-only metadata (like Unity .meta): the player
# never reads them, so they stay out of the shipped bundle.
install(TARGETS PlayerShipping RUNTIME DESTINATION .)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Editor/src/Exported/ DESTINATION Exported
        PATTERN "*.import" EXCLUDE)
# Editor-authored content (saved scenes, project.json with the startup scene)
# lands in the RUNTIME Exported/ next to the exes, not in the source tree —
# layer it on top so the shipped bundle actually contains what the editor
# configured. OPTIONAL: absent for never-run or non-Release builds.
install(DIRECTORY ${CMAKE_BINARY_DIR}/build/bin/Release/Exported/
        DESTINATION Exported OPTIONAL
        PATTERN "*.import" EXCLUDE)
