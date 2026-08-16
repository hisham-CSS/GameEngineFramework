# The title's editor extension: what this game adds to a general-purpose editor.
#
# ---------------------------------------------------------------------------
# THE DIRECTION OF THE ARROW
# ---------------------------------------------------------------------------
# This library depends on the Editor's panel seam. The Editor does NOT depend on
# this, and that asymmetry is the whole reason the code moved here: the Combo
# Prover panel used to be compiled into Editor/ with
# `target_link_libraries(Editor PRIVATE CseData)` holding it up, which made a
# fighting-game authoring tool part of a tool that is meant to host more than one
# game. Games/UntitledFighter/CMakeLists.txt states the rule; this file is the
# editor's instance of it.
#
# Nothing under Editor/ names this target. The composition -- Editor links this
# -- is one line in the ROOT CMakeLists.txt, already the only file in the tree
# allowed to know a title exists.
#
# ---------------------------------------------------------------------------
# WHY A STATIC LIBRARY AND NOT A RUNTIME PLUGIN
# ---------------------------------------------------------------------------
# A DLL loaded at startup would let an editor pick up a title it was not built
# with, which sounds like the better seam and is not the one to build today: it
# needs a stable C ABI across the panel interface (ImGui state, std::string and
# std::unique_ptr all cross it here), per-platform loading code, and a version
# handshake -- a real subsystem, in service of a scenario nobody has yet, since
# this repository ships exactly one editor built from source alongside exactly
# one title. A static library gets the SOURCE boundary right, which is the part
# that actually rots, and leaves the link boundary as a build-time decision.
add_library(UntitledFighterEditor STATIC
    src/ComboProverPanel.h
    src/ComboProverPanel.cpp
    # The adapter: presents the panel as an editor::IEditorPanel and defines
    # editor::RegisterTitlePanels, which the Editor declares and never defines.
    src/UntitledFighterPanels.cpp
)

# The host's seam header. CMAKE_SOURCE_DIR rather than a relative hop, for the
# same reason Data/ and Game/ spell Engine/src/core that way: this reaches OUT of
# the title to a host at the repository root that belongs to no title, and
# ../../../Editor would tie this file to sitting exactly three directories deep.
#
# PRIVATE: the seam is how this library talks to its host, not something a
# consumer of this library needs. There are no consumers -- an executable links
# it, calls one function through a declaration it already owns, and is done.
#
# tests/CMakeLists.txt already reaches into Editor/src this way for
# UndoHistory.cpp, so it is the established shape here rather than a new idea.
target_include_directories(UntitledFighterEditor
    PRIVATE ${CMAKE_SOURCE_DIR}/Editor/src)

target_compile_features(UntitledFighterEditor PUBLIC cxx_std_17)
set_target_properties(UntitledFighterEditor PROPERTIES POSITION_INDEPENDENT_CODE ON)

# LINKING THIS LIBRARY IS THE REGISTRATION.
#
# PUBLIC, which is PRIVATE and INTERFACE, and both halves are load-bearing:
#
#   INTERFACE  turns on the Editor's declaration of editor::RegisterTitlePanels
#              and its call site. It has to ride the LINK LINE, because that is
#              what makes the link and the macro impossible to get out of step:
#              one line in the root does both, there is no second line anybody
#              could forget, and a host that links nothing compiles the hook out
#              entirely and still links clean.
#   PRIVATE    turns the same declaration on while compiling THIS library, so
#              the compiler CHECKS the definition in UntitledFighterPanels.cpp
#              against the editor's declaration. Without it the guard hides the
#              declaration here, a changed signature compiles happily into a
#              differently-mangled symbol, and the mismatch surfaces as an
#              unresolved external in the Editor naming a function the file you
#              are looking at appears to define.
#
# The remaining ways to be wrong are both loud: define without link is an
# unresolved external, and link without define would be a static library nobody
# references -- which the linker drops silently, showing up as a Window menu with
# no Combo Prover in it. Carrying the macro on the link line is what rules the
# silent one out.
target_compile_definitions(UntitledFighterEditor PUBLIC CSE_EDITOR_TITLE_PANELS=1)

# imgui: the panel IS ImGui code. PRIVATE, because nothing crosses the library
# boundary in an ImGui type -- editor::IEditorPanel is ImGui-free on purpose, so
# a host that draws with something else could still implement it.
find_package(imgui CONFIG REQUIRED)

# CseData: character loading, the prover adapter and MatchBuilder's loss table.
# PRIVATE for the same reason -- the panel's interface to the editor is
# IEditorPanel and a std::unique_ptr, and no cse::data type appears in it. That
# is what keeps the Editor executable's own sources free of this game: it links
# the archive, it never sees the headers.
#
# NOTE WHAT IS NOT HERE: CseKernel. The panel includes MatchBuilder.h for the
# projection-loss table, and MatchBuilder deliberately calls no kernel function
# so that no symbol is needed at link time (asserted in
# Games/UntitledFighter/Data/CMakeLists.txt). So what an editor acquires by
# linking this is character data and an analysis -- never the simulation.
target_link_libraries(UntitledFighterEditor PRIVATE CseData imgui::imgui)
