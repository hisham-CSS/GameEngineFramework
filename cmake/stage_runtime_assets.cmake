# Stages runtime assets next to the executables. Invoked as:
#   cmake "-DROOTS=<root>[;<root>...]" -DDST=<runtime Exported dir> -P stage_runtime_assets.cmake
#
# ---------------------------------------------------------------------------
# ONE RUNTIME DIRECTORY, ASSEMBLED FROM SEVERAL SOURCE TREES
# ---------------------------------------------------------------------------
# ROOTS is ORDERED AND LATER WINS. The engine's own assets are the first root
# and a title's are layered on top of them, so the runtime Exported/ is engine
# plus title flattened into one directory and every relative path that already
# works goes on working -- `Exported/Shaders/frag.glsl`, `Characters/fighter_a.json`,
# a UIDocument's `Exported/UI/menu.cxml` -- with nothing in the engine, the
# editor or the player learning a second place to look.
#
# THIS IS THE INTERIM AND IT IS NOT THE FIX. The real fix is an asset SEARCH
# PATH: a list of roots the LOADER tries in turn at runtime, so a title's content
# never has to be copied next to the engine's at all and a shipped game can carry
# its own root wherever it likes. That is a change to every path resolution in
# the engine; this is a change to one build step, and it buys the SOURCE
# separation now -- a title's content lives under Games/<title>/ and nothing of
# it sits in the engine's asset root any more. When the search path lands, this
# script goes back to staging one root and the roots list moves into the loader.
#
# QUOTE THE ARGUMENT AND PASS VERBATIM AT EVERY CALL SITE. An unquoted
# `-DROOTS=a;b` is split by CMake into two arguments -- `-DROOTS=a` and `b` --
# before the command line is ever built, so this script would receive ONE root
# and mirror against it. That is precisely the deletion trap described below,
# arriving through the build system rather than through the script, and it
# configures and builds perfectly happily. Verified by experiment rather than by
# reading: the quoted form survives both the Ninja and the Visual Studio
# generators, and VERBATIM is what escapes the `;` for /bin/sh on Linux, where an
# unescaped one is a command separator.
#
# Two classes of content, handled differently:
#
# - STATIC assets live in SUBDIRECTORIES (Model/, Shaders/, Scripts/, Env/...).
#   They are owned by the source tree and re-copied every build so edits show
#   up. Every subdirectory is copied, deliberately: this list used to be
#   hardcoded, and adding Scripts/ and then Env/ each silently shipped a
#   feature whose assets never reached the runtime directory. The symptom is
#   always the same and always misleading -- the file is visibly right there
#   in the source tree, and the engine reports it missing.
#
# - AUTHORED files are the *.json at the TOP LEVEL (scene.json, project.json --
#   anything the EDITOR writes back into this same directory). Seeded only when
#   missing. A blind copy here silently reverted editor-saved scenes to the
#   checked-in copy on every build, which also meant the packaged game shipped
#   a stale scene.
# ROOTS AND DST ARE MADE ABSOLUTE BEFORE ANYTHING LOOKS AT THEM. `if(EXISTS)` and
# `if(IS_DIRECTORY)` are documented as well-defined only for full paths, and with
# a relative one they simply answer false -- so the mirror below quietly did
# nothing at all when this script was invoked by hand with repo-relative
# arguments. It removed no stale file and printed no message, which is the worst
# available failure for a step whose whole job is removing things. The build
# always passes absolute paths and was never affected; this makes the script
# behave the same way regardless of who calls it.
if(NOT DEFINED ROOTS OR NOT DEFINED DST)
    message(FATAL_ERROR
        "stage_runtime_assets.cmake needs \"-DROOTS=<root>[;<root>...]\" "
        "-DDST=<runtime Exported>.\n"
        "  ROOTS replaced the old single -DSRC=<source Exported>: staging now "
        "layers an engine asset root and a title's on top of each other, and a "
        "one-root spelling that silently ignored the rest would delete the "
        "title's content on every build. A single root is a one-element ROOTS.")
endif()
get_filename_component(DST "${DST}" ABSOLUTE)

set(absRoots "")
foreach(root ${ROOTS})
    get_filename_component(root "${root}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${root}")
        # LOUD, and it has to be. A root that does not exist is either a typo or
        # a title whose assets were never created, and both of those stage
        # NOTHING for that layer -- which looks exactly like a game that shipped
        # with its front end missing. The caller is expected to decide whether a
        # root exists before naming it (see the composition block in the root
        # CMakeLists.txt: a title need not ship assets at all).
        message(FATAL_ERROR "stage_runtime_assets.cmake: root is not a directory: ${root}")
    endif()
    list(APPEND absRoots "${root}")
endforeach()
# The same root named twice would announce every one of its own files as
# overriding itself, which is a screen of noise describing nothing.
list(REMOVE_DUPLICATES absRoots)
if(NOT absRoots)
    message(FATAL_ERROR "stage_runtime_assets.cmake: ROOTS is empty")
endif()

# ---------------------------------------------------------------------------
# COPY, IN ORDER, ANNOUNCING WHAT ONE ROOT TAKES FROM ANOTHER
# ---------------------------------------------------------------------------
# An OVERRIDE -- the same relative path in two roots -- is ALLOWED and is
# ANNOUNCED, and the two halves are one decision.
#
# Allowed, because "the game owns the whole front end" is the point of layering:
# a title that wants its own Fonts/Roboto.ttf, its own Icon/icon.png or its own
# UI/ art must be able to have them, and a rule that forbade it would forbid the
# feature this exists for.
#
# Announced, for the same reason a removal is. A silent override is the worst
# failure this directory can produce: the engine's file is visibly right there in
# the engine's source tree, the running game disagrees with it, and nothing
# anywhere says why. That is the same shape as the KEEPING message below, which
# was added after a menu.json setting that "did nothing" cost a debugging
# session -- and an override is harder, because there the two files at least had
# the same path.
#
# AND THE ANNOUNCEMENT WOULD OTHERWISE HAVE BEEN A LIE, which is how this was
# found. `file(COPY)` "optimizes out a file if it exists at the destination with
# the same timestamp", and CMake's idea of the same timestamp is far coarser than
# a file system's: two files written 8 MILLISECONDS apart compared equal in the
# fixture that caught this, so the later root's copy was skipped and the EARLIER
# root's content stayed staged -- underneath a log line saying the later one had
# won. Every fresh `git clone` writes both roots in one burst, so this was not a
# rare race; it was the default outcome on any machine that had not built before.
# Removing the staged loser first leaves the directory copy below nothing to
# optimize against, which is a one-line fix and keeps the fast timestamp path for
# the thousands of files that are NOT overridden.
#
# THE REMAINING HOLE, named rather than hidden: AN OVERRIDE THAT STOPS EXISTING.
# Delete the overriding file from the title's root -- or drop the title's whole
# root by removing its add_subdirectory -- and the path is no longer in two
# roots, so nothing above removes the staged copy and the engine's own file may
# be skipped by that same timestamp comparison. Measured, not assumed: after
# dropping the title's root from a two-root fixture the staged shader was still
# the title's. The title's own DIRECTORIES are pruned correctly in that case (see
# the prune below, which is what makes a title-less build honest); it is only a
# path the title had taken over from the engine that can persist.
#
# Detecting it needs the PREVIOUS build's override set, i.e. a stamp file living
# in a directory that gets packaged, and that is a poor trade: the symptom is one
# stale asset in one developer's build tree, the fix is deleting that file (or
# the staged Exported/) and building again, and the case only arises for a title
# that overrode an engine asset in the first place -- which the message above
# makes impossible to do by accident. The search-path system this whole layering
# is interim to removes the case entirely, because nothing is copied over
# anything.
set(earlierRoots "")
set(sourceDirs "")
foreach(root IN LISTS absRoots)
    file(GLOB children RELATIVE "${root}" "${root}/*")
    foreach(child ${children})
        if(NOT IS_DIRECTORY "${root}/${child}")
            continue()
        endif()
        # Only ever walked for a root that has something BEFORE it, so the
        # engine's own root -- much the largest -- costs nothing here.
        if(earlierRoots)
            file(GLOB_RECURSE incoming RELATIVE "${root}/${child}" "${root}/${child}/*")
            foreach(rel ${incoming})
                set(loser "")
                foreach(prev IN LISTS earlierRoots)
                    if(EXISTS "${prev}/${child}/${rel}")
                        set(loser "${prev}")   # last one wins the report, as it lost the file
                    endif()
                endforeach()
                if(loser)
                    message(STATUS
                        "[stage] OVERRIDE ${child}/${rel}: ${root} wins; the copy in "
                        "${loser} is not staged.")
                    # See above: without this the copy below silently keeps the
                    # loser, because the two source files' timestamps compare
                    # equal on any tree checked out in one go.
                    file(REMOVE "${DST}/${child}/${rel}")
                endif()
            endforeach()
        endif()
        file(COPY "${root}/${child}" DESTINATION "${DST}")
        list(APPEND sourceDirs "${child}")
    endforeach()
    list(APPEND earlierRoots "${root}")
endforeach()
# A directory present in several roots is one staged directory, and the prune
# below does a list(FIND) against this.
list(REMOVE_DUPLICATES sourceDirs)

# COPYING IS ONLY HALF OF "OWNED BY THE SOURCE TREE". A copy adds and overwrites;
# it never REMOVES, so a file deleted from the source tree lived on in every
# build directory that had ever seen it -- invisible, because the source tree
# looks correct and nobody greps a build output.
#
# That is not a tidiness problem. `Player/CMakeLists.txt:63-81` packages the game by
# walking this staged directory and copying whatever it finds, so a stale file
# here is a file that SHIPS. The case that found this: three characters
# transcribed from third-party MUGEN sources were moved out of the asset root
# precisely because they may not be distributed, and every existing build
# directory went on holding them, ready to be bundled by the next `install`.
#
# So the static half now MIRRORS rather than merely copies: a staged file whose
# source counterpart is gone is deleted, and so is a whole staged subdirectory
# whose source counterpart is gone. Every removal is announced, because a build
# that silently deletes files is its own kind of trap.
#
# ---------------------------------------------------------------------------
# "GONE" MEANS ABSENT FROM EVERY ROOT, AND THIS IS THE WHOLE DIFFICULTY
# ---------------------------------------------------------------------------
# The single-root mirror asked one question -- does SRC still have this file? --
# and with several roots the naive port asks it of the FIRST root only. Every
# file a title contributed is then absent from the engine's asset root, judged
# stale, announced and DELETED, on every build. The game would disappear from the
# runtime directory immediately after being copied into it, and the build log
# would be a hundred lines of the removal message that exists to make deletions
# trustworthy -- the staging bug commit 5d12ac1 fixed, running in reverse and
# with a clean conscience.
#
# Hence one loop over `absRoots` per staged file, and hence the prune reads the
# UNION `sourceDirs` rather than any one root's children. It is also why this
# cannot be two invocations of this script, one per root: each would prune the
# other's content, and no ordering saves it.
#
# ONE EXCEPTION, and it is the same exception the .json rule below is made of:
# `*.import` sidecars are written by the EDITOR into this tree (ImportSettings.h
# -- "foo.png" -> "foo.png.import"), so they legitimately exist here with no
# source counterpart. Deleting them would throw away a designer's import
# settings on every build. They are also excluded from packaging by both install
# rules, so keeping them costs the bundle nothing.
#
# THE PRUNE IS ALSO HOW A TITLE IS UN-COMPOSED. Drop the title's
# add_subdirectory from the root CMakeLists and its root stops being passed
# here; on the very next build every file it staged is absent from every root,
# so the whole title directory is removed from the runtime Exported/ and
# announced. A general engine that still builds AND runs is the property the
# boundary assertion exists to protect, and this is the content half of it --
# without the prune, a build tree that had ever seen a title would go on booting
# that title's front end forever.
if(EXISTS "${DST}")
    file(GLOB stagedChildren RELATIVE "${DST}" "${DST}/*")
    foreach(child ${stagedChildren})
        if(NOT IS_DIRECTORY "${DST}/${child}")
            continue()
        endif()
        list(FIND sourceDirs "${child}" foundAt)
        if(foundAt EQUAL -1)
            message(STATUS
                "[stage] REMOVING staged ${child}/: no source root has it.")
            file(REMOVE_RECURSE "${DST}/${child}")
            continue()
        endif()
        file(GLOB_RECURSE stagedFiles RELATIVE "${DST}/${child}" "${DST}/${child}/*")
        foreach(rel ${stagedFiles})
            if(rel MATCHES "\\.import$")
                continue()
            endif()
            set(stillSourced FALSE)
            foreach(root IN LISTS absRoots)
                if(EXISTS "${root}/${child}/${rel}")
                    set(stillSourced TRUE)
                    break()
                endif()
            endforeach()
            if(NOT stillSourced)
                message(STATUS
                    "[stage] REMOVING staged ${child}/${rel}: no source root has "
                    "it any more.")
                file(REMOVE "${DST}/${child}/${rel}")
            endif()
        endforeach()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# THE AUTHORED TOP-LEVEL .json, RESOLVED ACROSS THE ROOTS FIRST
# ---------------------------------------------------------------------------
# Resolved BY NAME across every root before anything is seeded, rather than root
# by root. Seeding root-by-root would copy the FIRST root's project.json into an
# empty destination and then compare the SECOND root's against the file it had
# just lost to -- printing the KEEPING message below, which says "these are
# editor-writable so a build must not overwrite them", about a file no editor has
# ever touched. A correct-looking message describing the wrong mechanism is worse
# than none.
#
# Walked in REVERSE so the first root to claim a name is the winning one, which
# is the same "later wins" rule as the copy above with the loop turned around.
#
# NOTE FOR WHOEVER LAYS OUT A TITLE'S CONTENT: a top-level .json here is
# seed-only and is therefore NOT pruned when its root goes away, so a title that
# puts one beside the engine's own gets two things it probably did not want --
# a name collision with `scene.json`/`menu.json`/`project.json`, and a file that
# survives the title being removed from the build. A title's content belongs in
# a subdirectory of its own, which the mirror above owns end to end.
set(seedNames "")
set(seedPaths "")
set(reversedRoots ${absRoots})
list(REVERSE reversedRoots)
foreach(root IN LISTS reversedRoots)
    file(GLOB rootSeeds RELATIVE "${root}" "${root}/*.json")
    foreach(name ${rootSeeds})
        list(FIND seedNames "${name}" at)
        if(at EQUAL -1)
            list(APPEND seedNames "${name}")
            list(APPEND seedPaths "${root}/${name}")
        else()
            list(GET seedPaths ${at} winner)
            message(STATUS
                "[stage] OVERRIDE ${name}: ${winner} is the copy that would seed; "
                "${root}/${name} is not staged.")
        endif()
    endforeach()
endforeach()

list(LENGTH seedNames seedCount)
if(seedCount GREATER 0)
    math(EXPR lastSeed "${seedCount} - 1")
    foreach(i RANGE ${lastSeed})
        list(GET seedNames ${i} name)
        list(GET seedPaths ${i} f)
        if(NOT EXISTS "${DST}/${name}")
            file(COPY "${f}" DESTINATION "${DST}")
        else()
            # SEEDED-ONLY IS SAFE BUT SILENT, and the silence is its own bug: an
            # authored change to one of these in the source tree never reaches a
            # tree that has already been built once, so the file is visibly right
            # in the repo and the running game disagrees with it. That cost a whole
            # debugging session on a menu.json scale setting that "did nothing".
            #
            # Still not copied -- overwriting is what reverts editor-saved scenes,
            # which is worse. Just say so, and only when they actually differ.
            file(SHA256 "${f}" srcHash)
            file(SHA256 "${DST}/${name}" dstHash)
            if(NOT srcHash STREQUAL dstHash)
                message(STATUS
                    "[stage] KEEPING the staged ${name}: it differs from the source "
                    "copy, and these are editor-writable so a build must not "
                    "overwrite them. If the SOURCE one is the one you want, delete "
                    "${DST}/${name} and build again.")
            endif()
        endif()
    endforeach()
endif()
