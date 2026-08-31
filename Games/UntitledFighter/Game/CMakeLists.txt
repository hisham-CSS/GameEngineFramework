# The headless game core: input sources, the fight session, replay capture and
# the live combo judge.
#
# WHAT THIS TARGET IS FOR. Everything between "a character file was loaded" and
# "a fight happened": where a tick's bits come from, the one object that calls
# Simulate, the recording, and the judgement of what the player just did against
# what the combo prover said they could do. It is the first playable — training
# mode — minus every pixel of it.
#
# ---------------------------------------------------------------------------
# WHY IT LINKS EXACTLY TWO THINGS
# ---------------------------------------------------------------------------
# CseGame is the first library in this tree that both READS CHARACTER DATA and
# LINKS THE SIMULATION, and that is legitimate precisely because it is a CALLER.
# Games/UntitledFighter/Data/CMakeLists.txt asserts that CseData never acquires
# CseKernel and says why:
# "the dependency runs the other way: a future match-setup layer reads character
# data and fills in kernel state." This target is that layer. It is the caller
# both libraries were kept apart in anticipation of, and it is the only place in
# the tree where linking both is not a smell.
#
# NO ENGINE. NO GL. NO RENDERER. Rendering and the HUD are somebody else's job
# and go above this. Keeping this target Engine-free is not a preference, it is
# what makes every claim it makes testable in a unit test with no window and no
# GL context — which is how every other proof in this repository was earned. The
# ground-truth loop was executed in a test; the gap between the model and the
# game was measured in a test; a training mode whose session could only be
# exercised by launching an editor would be the first part of this argument that
# nobody could re-run.
#
# The assertion at the bottom makes that a configure-time failure rather than a
# convention somebody remembers, in the same spirit as the two CMakeLists.txt
# beside this one — Games/UntitledFighter/Kernel/ ("CseKernel calls
# target_link_libraries exactly once, with nothing") and
# Games/UntitledFighter/Data/.

add_library(CseGame STATIC
    # One translation unit per public header, and deliberately so: three agents
    # implement these in parallel against the headers, and a file-per-header
    # split is the one arrangement in which they cannot collide.
    src/InputSource.cpp     # IInputSource + Scripted/Latched/Fallback
    src/FightSession.cpp    # FightSession, ValidateSetup, BuildDemonstration
    src/Replay.cpp          # HashMatchData, the format, recorder, reader, verifier
    src/ComboWatcher.cpp    # the live judge
    src/WitnessCursor.cpp   # THE witness cursor; the five copies died for it (M1.3g)
)

target_include_directories(CseGame
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include

    # PathSandbox.h + Core.h only. Replay.cpp is the one file that needs it: a
    # replay path is untrusted on the READ side (a stranger hands you the file)
    # and on the WRITE side (a playtester types the name), and
    # docs/MAINTENANCE.md's rule admits no exceptions.
    #
    # PRIVATE, because no consumer of cse/game/Replay.h needs the sandbox on its
    # include path — the header names no filesystem type and takes its paths as
    # strings, exactly as CharacterData.h does.
    #
    # Still CMAKE_SOURCE_DIR now the title lives under Games/: this reaches OUT
    # of the title to the engine at the repo root, which belongs to no title.
    # ../../../Engine would tie this file to sitting exactly three deep.
    PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src/core
)

target_compile_features(CseGame PUBLIC cxx_std_17)
set_target_properties(CseGame PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Both PUBLIC, and both genuinely needed by consumers:
#   CseKernel — FightSession.h hands back a GameState and a MatchData, and this
#               target CALLS Simulate/ResetMatch/Checksum/MoveAt/MoveDuration.
#               That is the difference from CseData, which uses the kernel's
#               structs and deliberately calls none of its functions so that it
#               needs no symbol at link time.
#   CseData   — ComboWatcher.h hands back judgements spelled in MoveIndexMap and
#               ProverResult, so cse/data headers are on a consumer's path.
target_link_libraries(CseGame PUBLIC CseKernel CseData)

# --- PathIsContained comes from CseData's archive, deliberately -------------
#
# Games/UntitledFighter/Data/CMakeLists.txt compiles Engine/src/core/PathSandbox.cpp
# into CseData rather than linking the whole Engine (which would give character data a
# transitive dependency on GL, Jolt and Lua). CseGame links CseData, so the
# symbol is already on its link line and this target must NOT compile that file
# again: two archives each defining PathIsContained is an LNK2005 waiting for the
# first executable whose link order pulls both members.
#
# The coupling is real and is recorded rather than hidden: if CseData ever stops
# compiling PathSandbox.cpp, Replay.cpp loses its definition. The tidy fix is the
# one that file already names — a tiny PathSandbox target that Engine,
# CseData and this all link. It touches Engine/CMakeLists.txt, so it is a
# separate change; recorded here so the next person has the option in front of
# them rather than rediscovering it from a linker error.

# --- The guarantee, enforced ------------------------------------------------
#
# An exact whitelist rather than a blacklist of forbidden names. A blacklist is a
# list of the mistakes somebody thought of; this fires on the one nobody thought
# of, which is the only kind that gets made.
#
# If you are here because this fired: adding a dependency to the game core is a
# real architectural decision. Engine drags in GL, Jolt, Lua and a window, and
# the moment any of them is reachable from here, this module's tests need a
# context and the evidence they produce stops being cheap to re-run. Make the
# decision deliberately — amend docs/ARCHITECTURE.md, then add the name below
# with the reason.
get_target_property(_game_links CseGame LINK_LIBRARIES)
if (NOT _game_links STREQUAL "CseKernel;CseData")
    message(FATAL_ERROR
        "CseGame links something other than CseKernel and CseData: ${_game_links}\n"
        "The headless game core must stay headless. No Engine, no GL, no "
        "renderer, no window — that is what makes its claims testable without a "
        "GL context, which is how every other proof in this repository was "
        "earned. See the note above this check.")
endif()
unset(_game_links)
