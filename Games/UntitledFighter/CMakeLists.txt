# UntitledFighter — the first title, and the one that proves the engine.
#
# ---------------------------------------------------------------------------
# THE RULE THIS DIRECTORY EXISTS TO ENFORCE
# ---------------------------------------------------------------------------
# A TITLE MAY DEPEND ON THE ENGINE. THE ENGINE MAY NOT DEPEND ON A TITLE.
#
# One direction, and it is the reason Games/ is plural. This engine is being
# built for more than fighting games; the fighting game is the first one, and
# its job is to prove the engine rather than to become part of it. The moment
# Engine/ or Editor/ names a fighter, a hitbox or a move, the SECOND title
# starts by deleting the first one's code out of a shared library, and every
# guarantee the engine earned for game one has to be re-earned for game two.
#
# Held by inspection today rather than by a check: Engine/ references no
# cse::kernel, cse::data or cse::game name at all, and nothing under this
# directory appears on an engine target's link line. If you are about to add
# one, that is an architectural decision and not a build fix — the arrow goes
# the other way, and code that knows what a fighting game is belongs here.
#
# ---------------------------------------------------------------------------
# WHY THE TITLE IS ONE add_subdirectory AND NOT THREE
# ---------------------------------------------------------------------------
# The root reaches this whole title in a single line, and which libraries it
# decomposes into is this file's business rather than the root's. That is the
# shape that survives a second game: adding one costs the root ONE line, not
# three more entries in a list that would otherwise interleave engine
# directories with fighting-game directories until nobody can tell which is
# which. It also means the three below can be split, renamed or merged without
# the root noticing.
#
# ---------------------------------------------------------------------------
# WHY Net/ DELIBERATELY STAYED BEHIND
# ---------------------------------------------------------------------------
# Rollback is INFRASTRUCTURE, not fighting-game content. CseNet's ISession
# takes an opaque state blob and a byte of input per player: it names no
# fighter, no hitbox and no move, and any deterministic game wants it — a
# platform fighter, a puzzle game, a lockstep RTS. So it stays at the top level
# with the engine. The test for "does this belong under Games/" is not "was it
# written for the fighting game", which would drag Net/ and half of Renderer2D
# down here; it is "does it MENTION one".
#
# ThirdParty/comboprover stays put for a different reason: it is vendored from
# another repository, and vendored code lives in ThirdParty/ regardless of who
# consumes it. CseData links it.
#
# ---------------------------------------------------------------------------
# ORDERING
# ---------------------------------------------------------------------------
# The root adds this AFTER ThirdParty/, because CseData links ComboProver, and
# BEFORE Engine/ and tests/, because either may come to depend on the title
# while the title depends on neither.

# The authoritative fighting-game simulation. It links NOTHING, and its own
# CMakeLists.txt makes that a configure-time failure rather than a convention
# somebody remembers (docs/ARCHITECTURE.md D2, made structural by
# docs/ADR-002-open-decisions.md CHOICE D). That is also why it has never been
# a directory inside Engine/: there it would inherit Engine's entire link
# interface and the guarantee would be worth nothing.
add_subdirectory(Kernel)

# Character data (schema v2 -> memory), its projection into the combo prover,
# and the bridge that fills in kernel state. Asserts it never acquires
# CseKernel: the dependency runs data -> a kernel-shaped POD, never the reverse.
add_subdirectory(Data)

# The headless game core: input sources, the fight session, replay capture and
# the live combo judge. Links CseKernel and CseData and nothing else — asserted
# at configure time, because being Engine-free is what keeps its claims
# testable without a window and a GL context.
add_subdirectory(Game)

# ---------------------------------------------------------------------------
# THE TITLE'S HALVES OF THE HOST SEAMS
# ---------------------------------------------------------------------------
# The three libraries above are the GAME: they know nothing about a window, a
# renderer or an editor, and two of them spend a configure-time assertion each
# keeping it that way. The two below are how that game REACHES a host, and they
# exist because the hosts are general-purpose and must stay that way.
#
# The pattern is the same in both, and it is the pattern a second title copies:
#
#   * the HOST declares a hook and never defines it -- editor::RegisterTitlePanels
#     in Editor/src/EditorPanel.h, MyCoreEngine::RegisterTitleGameModes in
#     Engine/src/core/GameMode.h. Same idiom as MyCoreEngine::CreateApplication:
#     the link is where the two meet.
#   * the TITLE defines it, in a library whose INTERFACE carries the compile
#     definition that turns the host's call site on. Linking the library IS the
#     registration, so the link and the macro cannot get out of step.
#   * the ROOT CMakeLists.txt composes the two, in the one file already allowed
#     to know a title exists.
#
# Neither is added to CseGame. That target asserts it links exactly CseKernel and
# CseData, and it should: these two render, hold an ImGui window and read a
# controller, and the headless core stays headless.

# The Combo Prover panel, plugged into the general editor. Was compiled straight
# into Editor/ with target_link_libraries(Editor PRIVATE CseData) holding it up.
add_subdirectory(Editor)

# "Untitled Fighting Game" as a mode the Player can enter, tick, draw and leave.
# A fight is not a scene -- no entities, no camera, its own fixed tick -- which
# is the whole reason the host needed a concept above the scene at all.
add_subdirectory(Player)
