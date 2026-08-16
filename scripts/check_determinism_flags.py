#!/usr/bin/env python3
"""Fail the build if a floating-point flag that breaks determinism is present.

WHY THIS EXISTS
---------------
The fighting game this engine is being built toward needs bit-identical
simulation across machines (docs/ARCHITECTURE.md D3). Every guarantee that rests
on IEEE 754 evaporates the moment someone adds `/fp:fast` or `-ffast-math`,
because those licence the compiler to reassociate, to contract multiply-add into
FMA (one rounding instead of two), and to assume no NaNs. Nothing fails loudly
when that happens: the build succeeds, every local test passes, and two players
on different machines drift apart.

It is also the cheapest thing in the world to add by accident, usually with the
comment "// perf".

WHAT IT CHECKS
--------------
Three layers, because no one of them closes the hole on its own:

1. The build configuration we author -- CMakeLists.txt, cmake/*.cmake,
   CMakePresets.json. Catches a flag at the moment it is written.
2. The GENERATED build files under out/build -- build.ninja and
   compile_commands.json. Catches a flag arriving from somewhere we did not
   write, which is not hypothetical here: a vcpkg dependency exporting an
   INTERFACE compile option propagates into our compile lines, and that exact
   mechanism already caused a real crash in this repository once when
   Jolt::Jolt's `_HAS_EXCEPTIONS=0` contaminated every engine TU.
3. The SOURCES on the path from an input to a checksum (KERNEL_GLOBS: the
   fighting game's Kernel/ and Game/ modules, under Games/UntitledFighter/).
   The flags above are about how the compiler treats float; this is
   about float, libm, a clock and global rand() not being there at all. A build
   flag cannot make integer arithmetic drift, so layers 1 and 2 say nothing
   about a `float speed = 0.5f` typed straight into the simulation.

WHAT IT DOES NOT CHECK
----------------------
That `/fp:precise` is ON. MSVC defaults to it and CMake therefore emits no flag
at all, so there is nothing to grep for -- the generated build files contain no
`/fp:` anything. This tool proves the ABSENCE of the dangerous flags, which is
the property we actually need, and says so rather than implying more.

It also does not check vcpkg's own dependency builds, which happen in vcpkg's
buildtrees and never appear here. Jolt in particular compiles its own TUs with
its own flags; what matters for us is what reaches OUR compile lines. Whether
Jolt itself is built cross-platform-deterministically is a separate question,
answered by JPH_CROSS_PLATFORM_DETERMINISTIC and tracked in ARCHITECTURE.md D3.

USAGE
-----
    python scripts/check_determinism_flags.py             # check the repo
    python scripts/check_determinism_flags.py --self-test # prove the detector works

Suppress a genuine false positive by putting `det-ok` in a comment on that line,
and say why.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

# Flags that license the compiler to change floating-point RESULTS. Each entry
# is (needle, why-it-matters) so a failure explains itself rather than just
# naming a string.
FORBIDDEN = [
    ("/fp:fast",                     "MSVC: reassociation + FMA contraction + no-NaN assumptions"),
    ("-ffast-math",                  "GCC/Clang: implies the whole unsafe-math family"),
    ("-Ofast",                       "GCC/Clang: -O3 plus -ffast-math"),
    ("/Ofast",                       "the MSVC-style spelling of the same idea"),
    ("-ffp-contract=fast",           "allows a*b+c to contract to FMA: one rounding, not two"),
    ("-funsafe-math-optimizations",  "reassociation and reciprocal substitution"),
    ("-fassociative-math",           "(a+b)+c may become a+(b+c); they are not equal in FP"),
    ("-freciprocal-math",            "x/y may become x*(1/y)"),
    ("-ffinite-math-only",           "assumes no NaN/Inf; a NaN then behaves differently per target"),
    ("/Qfast_transcendentals",       "MSVC: swaps in approximate sin/cos/exp"),
    ("-mfma",                        "enables FMA contraction; see ARCHITECTURE.md D3 on Jolt and NEON"),
]

# Paths whose contents are NOT ours to control. Kept as a mechanism with a
# comment rather than deleted: if a dependency ever does export a fast-math
# INTERFACE option, the right response is a deliberate, visible entry here --
# not silently widening the scan.
ALLOWLIST_SUBSTRINGS = [
    # (none today -- verified 2026-08-12: no forbidden flag reaches our build files)
]

# Floating point has no business in the simulation at all -- KERNEL_GLOBS below
# is the list of files that sentence is about. The flags above are about how the
# compiler treats float; this is about float not being there.
#
# ARCHITECTURE.md D2 says cross-platform bit-identity is a property of integer
# arithmetic rather than of a build flag -- which is only true while those files
# contain no float. One `float speed = 0.5f` and Windows<->Linux crossplay
# (NORTHSTAR Q1) stops being free and starts being a research project. The
# CMake side of the guarantee is in Games/UntitledFighter/Kernel/CMakeLists.txt,
# which asserts the target links nothing; this is the source side.
KERNEL_FORBIDDEN = [
    ("float",        "the simulation is integer-only: use sub-units, 1 px = 256"),
    ("double",       "the simulation is integer-only: use sub-units, 1 px = 256"),
    ("<cmath>",      "libm results are not bit-identical across platforms"),
    ("<math.h>",     "libm results are not bit-identical across platforms"),
    ("<random>",     "the standard engines are not specified to match across implementations"),
    ("<chrono>",     "a clock in the simulation makes a tick unreproducible"),
    ("rand(",        "unseeded global RNG: not part of GameState, so rollback cannot restore it"),
]

# WHERE THE SIMULATION LIVES, as ONE constant, because two things are derived
# from it and they must not drift: the globs that decide which files are
# scanned, and the module a failure message names.
#
# It has moved once. These were `Kernel/` and `Game/` at the top level until the
# fighting game was gathered under Games/UntitledFighter/ -- this engine is
# meant to host more than one title, so a title's code lives under Games/ and
# only general-purpose code sits at the root. A gate that had kept its old globs
# across that move would have matched zero files and printed OK, which is the
# exact failure this whole file exists to prevent. The per-glob warning in
# main() and the probe in self_test() are both here because of it.
KERNEL_MODULE_ROOTS = [
    "Games/UntitledFighter/Kernel/",
    "Games/UntitledFighter/Game/",
]

# The sources held to that rule. "Kernel" is the historical name and the list is
# wider than the kernel module, because the property is not "this directory is
# tidy" -- it is that everything on the path from an input to a checksum is
# integer.
#
# WHY THE GAME CORE IS HELD TO IT TOO, in three parts, because that module does
# three things and each fails differently:
#
#   1. FightSession is the only caller of Simulate and computes the InputPair it
#      hands over. A float anywhere in that path changes the BITS fed to a kernel
#      that is itself still perfectly deterministic.
#   2. Replay encodes the input stream and the per-checkpoint checksums into a
#      FILE. A float there is a file that decodes differently on the machine that
#      did not write it, which is the one failure this format exists to catch.
#   3. ComboWatcher judges. Its arithmetic never reaches GameState, so a float in
#      it would leave the simulation bit-identical and make the VERDICT drift --
#      which is worse rather than better, because the run really would be
#      reproducible and only the sentence shown to a playtester would not be.
#
# tests/ is deliberately NOT here. strip_comments() blanks comments and not
# string literals, and the test file quotes these very words in failure messages
# ("double-count every hit"), so adding it would report a false positive on the
# first run and teach everyone to ignore this gate.
KERNEL_GLOBS = [
    "Games/UntitledFighter/Kernel/include/cse/kernel/*.h",
    "Games/UntitledFighter/Kernel/src/*.cpp",
    "Games/UntitledFighter/Game/include/cse/game/*.h",
    "Games/UntitledFighter/Game/src/*.cpp",
]

AUTHORED_GLOBS = [
    "CMakeLists.txt",
    "*/CMakeLists.txt",
    # The titles, named explicitly. `*/CMakeLists.txt` above reaches exactly ONE
    # level, and it stopped covering the game's build files the moment they
    # moved to Games/<title>/<library>/ -- three CMakeLists.txt dropping out of
    # the fast-math scan while the file count stayed large enough that nobody
    # would look twice. Two entries because a title has a build file of its own
    # as well as one per library.
    #
    # Not `**/CMakeLists.txt`, which would be shorter and wrong: it recurses
    # into ThirdParty/GekkoNet (a pinned submodule -- see ALLOWLIST_SUBSTRINGS
    # on scanning what is not ours) and into out/build/.
    "Games/*/CMakeLists.txt",
    "Games/*/*/CMakeLists.txt",
    "cmake/*.cmake",
    "CMakePresets.json",
    "scripts/linux-build.sh",
]

GENERATED_GLOBS = [
    "out/build/*/build.ninja",
    "out/build/*/compile_commands.json",
]


def rel_of(path: Path, repo: Path) -> str:
    """The repo-relative path, forward-slashed on every platform.

    One function because the string is an IDENTITY here, not just a label: the
    allowlist matches on it and main() decides which advice a hit gets by
    comparing it against the scanned set. Two spellings of the same path would
    silently mean two different files.
    """
    return str(path.relative_to(repo)).replace("\\", "/")


def scan_file(path: Path, repo: Path) -> list[tuple[str, int, str, str]]:
    """Return (relpath, lineno, needle, why) for each hit."""
    rel = rel_of(path, repo)
    if any(a in rel for a in ALLOWLIST_SUBSTRINGS):
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    hits = []
    for n, line in enumerate(text.splitlines(), 1):
        if "det-ok" in line:            # deliberate, explained exemption
            continue
        for needle, why in FORBIDDEN:
            if needle in line:
                hits.append((rel, n, needle, why))
    return hits


def strip_comments(text: str) -> list[str]:
    """Blank out comment content, keeping line numbers intact.

    Necessary rather than fastidious: the kernel headers EXPLAIN that they
    contain no float, so a naive grep flags the very comment documenting the
    rule. Returns one string per source line with comment text removed.
    """
    out: list[str] = []
    in_block = False
    for line in text.splitlines():
        buf = []
        i = 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end == -1:
                    i = len(line)
                else:
                    in_block = False
                    i = end + 2
                continue
            if line.startswith("//", i):
                break                      # rest of the line is a comment
            if line.startswith("/*", i):
                in_block = True
                i += 2
                continue
            buf.append(line[i])
            i += 1
        out.append("".join(buf))
    return out


def scan_kernel(path: Path, repo: Path) -> list[tuple[str, int, str, str]]:
    """Return (relpath, lineno, needle, why) for float/libm/clock use in code."""
    rel = rel_of(path, repo)
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    hits = []
    raw_lines = text.splitlines()
    for n, code in enumerate(strip_comments(text), 1):
        if "det-ok" in raw_lines[n - 1]:     # deliberate, explained exemption
            continue
        for needle, why in KERNEL_FORBIDDEN:
            if needle in code:
                hits.append((rel, n, needle, why))
    return hits


def collect(repo: Path, globs: list[str]) -> list[Path]:
    out: list[Path] = []
    for g in globs:
        out.extend(p for p in repo.glob(g) if p.is_file())
    return sorted(set(out))


def split_hits(hits: list[tuple[str, int, str, str]],
               purity_rel: set[str]) -> tuple[list, list]:
    """Split hits into (purity, flag) by WHICH SCAN produced them.

    Membership in the scanned set rather than a path-prefix test. The purity
    rule covers two modules, and a prefix test would send whoever wrote `float`
    in the game core to read the KERNEL's CMakeLists.txt -- the wrong file, with
    the wrong instruction in it.
    """
    purity = [h for h in hits if h[0] in purity_rel]
    flag = [h for h in hits if h[0] not in purity_rel]
    return purity, flag


def module_of(rel: str) -> str:
    """The module a purity hit belongs to, for the sentence that reports it.

    This was `rel.split("/")[0] + "/"`, which named the top-level directory, and
    that stopped meaning anything the moment the game moved under
    Games/UntitledFighter/: every purity hit now begins `Games/`, so a float in
    the replay writer and a float in the simulation would both be reported
    against "Games/" and neither author would learn which of the two rules they
    had broken. Name the MODULE, not the tree it happens to sit in.

    Falls back to the top-level directory for anything unclassified, because a
    slightly vague sentence beats a KeyError in a build gate.
    """
    for root in KERNEL_MODULE_ROOTS:
        if rel.startswith(root):
            return root
    return rel.split("/")[0] + "/"


def self_test() -> int:
    """Prove the detector detects. A gate nobody has seen fail is not a gate."""
    import tempfile

    failures = 0
    with tempfile.TemporaryDirectory() as td:
        repo = Path(td)
        for needle, _ in FORBIDDEN:
            f = repo / "CMakeLists.txt"
            f.write_text(f'add_compile_options({needle})\n', encoding="utf-8")
            if not scan_file(f, repo):
                print(f"SELF-TEST FAILED: {needle!r} is in the table but was not detected")
                failures += 1

        # ...and that the suppression marker works, or an exemption would be a
        # silent hole rather than a visible one.
        f = repo / "CMakeLists.txt"
        f.write_text('add_compile_options(/fp:fast)  # det-ok: explained\n', encoding="utf-8")
        if scan_file(f, repo):
            print("SELF-TEST FAILED: the det-ok marker did not suppress a hit")
            failures += 1

        # ...and that an innocent file is not flagged.
        f = repo / "CMakeLists.txt"
        f.write_text('set(CMAKE_CXX_STANDARD 17)\n', encoding="utf-8")
        if scan_file(f, repo):
            print("SELF-TEST FAILED: a clean file was flagged")
            failures += 1

        # --- the kernel-purity layer -------------------------------------
        k = repo / "k.cpp"
        for needle, _ in KERNEL_FORBIDDEN:
            k.write_text(f"int f() {{ {needle} ; }}\n", encoding="utf-8")
            if not scan_kernel(k, repo):
                print(f"SELF-TEST FAILED: kernel pattern {needle!r} was not detected")
                failures += 1

        # The comment-stripping is the part most likely to be wrong, and getting
        # it wrong in the LENIENT direction hides real float. Test both ways.
        k.write_text('// this file contains no float and no double\nint x = 1;\n',
                     encoding="utf-8")
        if scan_kernel(k, repo):
            print("SELF-TEST FAILED: a comment mentioning float was flagged")
            failures += 1

        k.write_text('/* a block comment naming double */\nint y = 2;\n', encoding="utf-8")
        if scan_kernel(k, repo):
            print("SELF-TEST FAILED: a block comment mentioning double was flagged")
            failures += 1

        k.write_text('int a = 1; // ok\nfloat sneaky = 0.5f;\n', encoding="utf-8")
        hits = scan_kernel(k, repo)
        if not hits:
            print("SELF-TEST FAILED: real float after a comment line was missed")
            failures += 1
        elif hits[0][1] != 2:
            print(f"SELF-TEST FAILED: comment stripping shifted line numbers "
                  f"(reported {hits[0][1]}, expected 2)")
            failures += 1

        # --- WHICH FILES THE PURITY LAYER ACTUALLY REACHES -----------------
        #
        # A typo in a glob is the failure this file is most exposed to: it scans
        # nothing, finds nothing, and prints OK. main() warns about a glob that
        # matches nothing in the real repo; this proves the other half -- that
        # each pattern's SHAPE names a file that can exist, and that a `double`
        # sitting at that path is caught rather than walked past.
        for g in KERNEL_GLOBS:
            parts = g.split("/")
            if any("*" in part for part in parts[:-1]):
                print(f"SELF-TEST FAILED: purity glob {g!r} wildcards a DIRECTORY, "
                      f"and this probe only knows how to fill in a file name")
                failures += 1
                continue
            probe = repo.joinpath(*parts[:-1], parts[-1].replace("*", "probe"))
            probe.parent.mkdir(parents=True, exist_ok=True)
            probe.write_text("double sneaky = 0.5;\n", encoding="utf-8")

            if not collect(repo, [g]):
                print(f"SELF-TEST FAILED: purity glob {g!r} matched nothing with a "
                      f"file laid down at exactly that path")
                failures += 1

        for p in collect(repo, KERNEL_GLOBS):
            if not scan_kernel(p, repo):
                print(f"SELF-TEST FAILED: a double at {rel_of(p, repo)} was scanned "
                      f"and not flagged")
                failures += 1

        # --- ...AND THAT THE GLOBS AND THE MODULE NAMES STILL AGREE ---------
        #
        # KERNEL_GLOBS decides what is scanned; KERNEL_MODULE_ROOTS decides what
        # a hit in it is called. Two spellings of one location, so a move that
        # updates one and forgets the other leaves the gate scanning files it
        # cannot name -- and module_of()'s fallback would then answer `Games/`,
        # which looks like a real module and is not one. That is precisely the
        # kind of wrong answer that survives a review, so assert it here.
        for g in KERNEL_GLOBS:
            if not any(g.startswith(r) for r in KERNEL_MODULE_ROOTS):
                print(f"SELF-TEST FAILED: purity glob {g!r} lies under no entry in "
                      f"KERNEL_MODULE_ROOTS, so a hit in it would be reported "
                      f"against the wrong module")
                failures += 1

        named = {module_of(g) for g in KERNEL_GLOBS}
        if named != set(KERNEL_MODULE_ROOTS):
            print(f"SELF-TEST FAILED: the purity globs name {sorted(named)} but "
                  f"KERNEL_MODULE_ROOTS lists {sorted(KERNEL_MODULE_ROOTS)}; a "
                  f"failure message would either collapse two modules into one "
                  f"name or name a module nothing scans")
            failures += 1

        # --- AND THAT A HIT GETS THE ADVICE THAT FITS ITS FILE -------------
        #
        # The two halves print different instructions, and the split was once
        # `startswith("Kernel/")`. Under that rule a float in the game core was
        # reported as a build-flag problem and its author was sent to read the
        # kernel's CMakeLists.txt, which has nothing to do with it.
        sim = KERNEL_MODULE_ROOTS[0] + "src/Simulate.cpp"
        fight = KERNEL_MODULE_ROOTS[1] + "src/FightSession.cpp"
        scanned = {sim, fight}
        rows = [(sim, 1, "float", "why"),
                (fight, 2, "double", "why"),
                ("CMakeLists.txt", 3, "/fp:fast", "why")]
        purity, flag = split_hits(rows, scanned)
        if [h[0] for h in purity] != [sim, fight]:
            print("SELF-TEST FAILED: a purity hit outside the kernel module was "
                  "classified as a build-flag hit, so its author would be sent to "
                  "CMakeLists.txt")
            failures += 1
        if [h[0] for h in flag] != ["CMakeLists.txt"]:
            print("SELF-TEST FAILED: a build-flag hit was classified as non-integer "
                  "code, so its author would be told to use sub-units")
            failures += 1

        # ...and that the two purity hits are reported against DIFFERENT
        # modules. Both now live under Games/UntitledFighter/, so the classifier
        # that used to work by top-level directory would name them identically
        # and the sentence "X is supposed to be integer-only" would point at a
        # directory containing the whole title.
        if module_of(sim) == module_of(fight):
            print(f"SELF-TEST FAILED: the simulation and the game core both report "
                  f"as {module_of(sim)!r}, so a failure names the title rather than "
                  f"the module that broke the rule")
            failures += 1

    if failures:
        return 1
    print(f"self-test OK: all {len(FORBIDDEN)} flag patterns and "
          f"{len(KERNEL_FORBIDDEN)} kernel patterns detect, all "
          f"{len(KERNEL_GLOBS)} purity globs match, are scanned and name one of "
          f"the {len(KERNEL_MODULE_ROOTS)} modules, each hit gets the advice for "
          f"its own file, det-ok suppresses, comments are not flagged, clean "
          f"files pass")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true",
                    help="prove the patterns actually match, then exit")
    ap.add_argument("--repo", default=None, help="repository root (default: this script's parent)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    repo = Path(args.repo) if args.repo else Path(__file__).resolve().parent.parent

    authored = collect(repo, AUTHORED_GLOBS)
    generated = collect(repo, GENERATED_GLOBS)

    kernel = collect(repo, KERNEL_GLOBS)

    hits: list[tuple[str, int, str, str]] = []
    for p in authored + generated:
        hits.extend(scan_file(p, repo))
    for p in kernel:
        hits.extend(scan_kernel(p, repo))

    print(f"determinism gate: {len(authored)} authored + {len(generated)} generated "
          f"build file(s), {len(kernel)} simulation source file(s) scanned")
    if not kernel:
        print("  WARNING: no simulation sources found. If the gameplay kernel has")
        print("  moved, update KERNEL_GLOBS or this half checks nothing.")
    else:
        # PER GLOB, not just for the list as a whole. Once the list covers more
        # than one module, a typo in ONE entry leaves the total non-zero and the
        # warning above silent, so the module nobody noticed had stopped being
        # scanned goes on reporting green.
        empty = [g for g in KERNEL_GLOBS if not collect(repo, [g])]
        for g in empty:
            print(f"  WARNING: `{g}` matched no files, so whatever lives there is")
            print("  not being checked. Fix the glob or drop it.")

    # The same check on the authored side, against the same mistake. These globs
    # name DIRECTORIES, and a directory that moves takes its build files out of
    # the flag scan while the total stays large enough that nobody looks -- which
    # is not hypothetical: `*/CMakeLists.txt` is one level deep and stopped
    # covering the fighting game the day it moved under Games/.
    for g in AUTHORED_GLOBS:
        if not collect(repo, [g]):
            print(f"  WARNING: `{g}` matched no authored build file. If something")
            print("  moved, the flag scan is quietly smaller than it used to be.")

    if not generated:
        # Not fatal on its own, but say so loudly: a green result that scanned
        # nothing is the failure mode this whole file exists to avoid.
        print("  WARNING: no generated build files found under out/build/.")
        print("  Configure a preset first, or this gate only covers what we author.")

    if not hits:
        print("  OK: no floating-point flag that would break determinism.")
        print("  NOTE: this proves ABSENCE of fast-math. /fp:precise is MSVC's default")
        print("        and is not emitted, so there is nothing to assert positively.")
        return 0

    print()
    # Two different failures land here and they want different words. Saying
    # "flag" at someone who wrote `float x` sends them to look at CMakeLists,
    # which is the wrong file.
    kernel_hits, flag_hits = split_hits(hits, {rel_of(p, repo) for p in kernel})
    if flag_hits and kernel_hits:
        print("FAILED: a fast-math build flag AND non-integer code in the simulation.")
    elif kernel_hits:
        # The modules are named from the hits rather than written out, so this
        # says the game core on a game-core hit instead of sending its author to
        # the kernel. See module_of() for why that is not a split("/")[0].
        modules = sorted({module_of(h[0]) for h in kernel_hits})
        print(f"FAILED: {', '.join(modules)} is supposed to be integer-only, and is not.")
        print("Cross-platform bit-identity is a property of integer arithmetic")
        print("(ARCHITECTURE.md D2). Use sub-units: 1 pixel = 256.")
    else:
        print("FAILED: a floating-point flag that breaks bit-identical simulation is present.")
    print("See docs/ARCHITECTURE.md D2/D3 and docs/MAINTENANCE.md.")
    print()
    for rel, n, needle, why in hits:
        print(f"  {rel}:{n}: {needle}")
        print(f"      {why}")
    print()
    print("If this is genuinely intentional, put `det-ok` in a comment on that line")
    print("with the reason -- so the exemption is visible rather than accidental.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
