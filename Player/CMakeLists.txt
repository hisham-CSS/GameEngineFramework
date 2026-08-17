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

    # App icon (see cse_add_app_icon in the root CMakeLists)
    cse_add_app_icon(${tgt})

    # Same output dir as the Editor so they share Exported/ (a scene saved in
    # the editor is immediately runnable in the player).
    set_target_properties(${tgt} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/bin/RelWithDebInfo
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

# --- Packaging: the standalone game bundle ships ONE player -------------
#
# TWO TARGETS, THREE PROFILES, AND EXACTLY ONE EXECUTABLE PER BUNDLE.
# BuildSettings.h defines a build profile as a pair of axes this build already
# has -- a CMake configuration and a player target:
#
#   Debug        (Debug,          PlayerDebug)
#   Development  (RelWithDebInfo, PlayerDebug)
#   Shipping     (Release,        PlayerShipping)
#
# Only PlayerShipping used to have an install rule, so `cmake --install` for a
# Debug or Development profile assembled a bundle holding Engine.dll, the whole
# asset root, and NO GAME IN IT. Both targets need one.
#
# THE SECOND RULE CANNOT BE UNCONDITIONAL. Installing both targets always would
# put PlayerDebug.exe -- console window, no MYCE_SHIPPING, so a startup failure
# exits silently instead of raising a message box -- beside Player.exe in the
# bundle you hand to somebody, and "which one do I double-click" is not a
# question a shipped game may ask. So each rule is restricted to the
# configurations its profile selects, and MinSizeRel installs neither because no
# profile names it.
#
# CONFIGURATIONS RATHER THAN COMPONENT, decided on the other install rules in
# this tree rather than on taste. `cmake --install --component <name>` installs
# ONLY that component, and every other rule here is in the default Unspecified
# one: Engine (Engine/CMakeLists.txt:377), the lua-cpp DLL (:306), and the two
# content rules below. A component-selected install would therefore produce a
# bundle with a player in it and no Engine.dll. Making components work means
# tagging every rule across three files with the same name, and one missed rule
# ships a game that will not start.
#
# The configuration axis needs none of that. It is ALREADY what a profile
# selects, and BuildPipeline.cpp already passes `--config <Config>` to
# `cmake --install` -- it has to, because the install(CODE) block below reads
# CMAKE_INSTALL_CONFIG_NAME. So the rule and the pipeline agree by construction
# instead of by a second name both sides have to spell identically.
#
# AND THE PREFLIGHT CAN READ IT WITH NOTHING BUILT, which is the other half of
# the requirement: CMake wraps each rule in
#   if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
# in the generated <binaryDir>/Player/cmake_install.cmake, on single- and
# multi-config generators alike. "Would this profile install a player" therefore
# has an answer sitting in the build tree before any process starts, which is
# what BuildPipeline.cpp's ProfileHasInstallRule reads.
#
# `cpack` FOLLOWS THE SAME RULE, and that is a deliberate change rather than
# fallout: the `package` target on a RelWithDebInfo tree now zips
# PlayerDebug.exe instead of an exe named Player.exe that was compiled with
# RelWithDebInfo flags. A shipping package comes from a Release tree, where it
# still produces Player.exe.
#
# THE vcpkg APPLOCAL HOOK IS NOT CONFIGURATION-RESTRICTED AND DOES NOT NEED TO
# BE. X_VCPKG_APPLOCAL_DEPS_INSTALL (root CMakeLists.txt:6) wraps install() and
# appends one unconditional install(CODE) per installed target, running
# applocal.ps1 against <prefix>/<that target's file name>. In a configuration
# where the exe was not installed, applocal's `resolve` cannot Resolve-Path it
# and returns immediately, so the hook is a no-op rather than a failure. Its
# only trace is a line in the install log -- "Installing app dependencies for
# PlayerShipping..." during a Development build -- which is the hook announcing
# itself, not a rule firing.
install(TARGETS PlayerShipping RUNTIME DESTINATION .
        CONFIGURATIONS Release)
install(TARGETS PlayerDebug RUNTIME DESTINATION .
        CONFIGURATIONS Debug RelWithDebInfo)

# EVERY DLL THE GAME NEEDS, copied from the directory that already has them.
#
# This replaces vcpkg's install-time applocal hook, which the root CMakeLists.txt
# now turns OFF and says why at length: it shells out to `dumpbin`, dumpbin is
# only on PATH in a VS developer environment, the editor spawns `cmake --install`
# from a GUI process that has no such environment, and applocal's failure is a
# Write-Error that does not fail the install. The result was a bundle holding
# Engine.dll, lua-c++.dll and nothing else, reported as a successful build, which
# died on launch with "glfw3.dll was not found".
#
# THE SOURCE IS build/bin/<CONFIG>, NOT vcpkg's bin, and that is the whole point.
# app_runtime_deps has already staged that directory with everything the engine
# needs to RUN -- including the DLLs no import table mentions, which is the
# PhysXCommon_64 lesson from cmake/stage_runtime_dlls.cmake. Copying it is the
# same rule stated a third time rather than a fourth mechanism.
#
# Resolved at INSTALL time via CMAKE_INSTALL_CONFIG_NAME for the same reason the
# authored-content block below does it: install(DIRECTORY) fixes its source at
# configure time and cannot see $<CONFIG>, and getting that wrong is how the
# bundle silently ships the wrong configuration's files.
#
# GLOB IS CORRECT HERE and is not the usual CMake glob hazard. That hazard is
# about globbing SOURCES at configure time and missing files added later; this
# runs during install, long after the staging target has populated the directory,
# and its whole purpose is to copy whatever is there rather than a list that can
# drift. A hand-written list is what stage_runtime_dlls.cmake already records
# being wrong by four DLLs.
install(CODE "
  set(_dlldir \"${CMAKE_BINARY_DIR}/build/bin/\${CMAKE_INSTALL_CONFIG_NAME}\")
  file(GLOB _runtime_dlls \"\${_dlldir}/*.dll\")
  if(_runtime_dlls)
    list(LENGTH _runtime_dlls _n)
    message(STATUS \"Bundling \${_n} runtime DLL(s) from \${_dlldir}\")
    file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}\" TYPE SHARED_LIBRARY
         FILES \${_runtime_dlls})
  else()
    message(FATAL_ERROR
      \"No runtime DLLs in \${_dlldir}, so this bundle would not start: the \"
      \"loader would fail on glfw3.dll or assimp before main() runs. That \"
      \"directory is populated by the app_runtime_deps target, so the usual \"
      \"cause is installing a configuration that was never built. Build \"
      \"configuration '\${CMAKE_INSTALL_CONFIG_NAME}' first.\")
  endif()
")

# The CONTENT rules stay unconditional: every profile's bundle needs the same
# assets, and only the executable differs between them.
#
# .import sidecars are editor-only metadata (like Unity .meta): the player
# never reads them, so they stay out of the shipped bundle.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Editor/src/Exported/ DESTINATION Exported
        PATTERN "*.import" EXCLUDE)
# Editor-authored content (saved scenes, project.json with the startup scene)
# lands in the RUNTIME Exported/ next to the exes, not in the source tree —
# layer it on top so the shipped bundle actually contains what the editor
# configured.
#
# The config is resolved at INSTALL time, not hardcoded. This path used to be
# .../bin/Release/Exported, so installing any other configuration (notably
# RelWithDebInfo) found nothing and silently shipped the source-tree defaults
# instead of the user's saved scene -- a packaged game that ignored their
# authoring, with no error to explain it. install(DIRECTORY) resolves its
# source at configure time and cannot use $<CONFIG>, hence install(CODE).
#
# TWO THINGS ARE EXCLUDED, for one reason: a shipped game never reads either.
# `*.import` are the asset sidecars the rule above already drops. `build.json`
# is this editor's build settings -- BuildSettings::DefaultPath(), staged beside
# project.json because the editor writes it into its own runtime Exported/, and
# stated in BuildSettings.h to be editor metadata that no bundle carries. It is
# excluded HERE rather than only in the pipeline's prune step so that `cpack`
# produces the same bundle the Build action does; the prune keeps removing it as
# an announced backstop for any third route into a bundle.
#
# EIGHT BACKSLASHES, and they are not a typo. The regex has to reach the
# GENERATED script as "\\.import$": this argument is unescaped once when this
# file is parsed and once more when install(CODE) writes the script, so each
# backslash needs four here. Written as four, the script gets "\.import$" and
# every `cmake --install` prints a CMP0010 "Invalid escape sequence \."
# developer warning into the build log the editor's Build panel shows.
install(CODE "
  set(_authored \"${CMAKE_BINARY_DIR}/build/bin/\${CMAKE_INSTALL_CONFIG_NAME}/Exported\")
  if(EXISTS \"\${_authored}\")
    message(STATUS \"Bundling editor-authored content from \${_authored}\")
    file(GLOB_RECURSE _authored_files RELATIVE \"\${_authored}\" \"\${_authored}/*\")
    foreach(_f IN LISTS _authored_files)
      if(NOT _f MATCHES \"\\\\\\\\.import$\" AND NOT _f STREQUAL \"build.json\")
        get_filename_component(_sub \"\${_f}\" DIRECTORY)
        file(COPY \"\${_authored}/\${_f}\"
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Exported/\${_sub}\")
      endif()
    endforeach()
  else()
    message(WARNING
      \"No editor-authored Exported/ for configuration '\${CMAKE_INSTALL_CONFIG_NAME}' \"
      \"(looked in \${_authored}). The package ships the source-tree defaults, NOT \"
      \"your saved scene. Run the editor in this configuration and save first.\")
  endif()
")
