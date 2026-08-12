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
Two layers, because either alone has a hole:

1. The build configuration we author -- CMakeLists.txt, cmake/*.cmake,
   CMakePresets.json. Catches a flag at the moment it is written.
2. The GENERATED build files under out/build -- build.ninja and
   compile_commands.json. Catches a flag arriving from somewhere we did not
   write, which is not hypothetical here: a vcpkg dependency exporting an
   INTERFACE compile option propagates into our compile lines, and that exact
   mechanism already caused a real crash in this repository once when
   Jolt::Jolt's `_HAS_EXCEPTIONS=0` contaminated every engine TU.

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

AUTHORED_GLOBS = [
    "CMakeLists.txt",
    "*/CMakeLists.txt",
    "cmake/*.cmake",
    "CMakePresets.json",
    "scripts/linux-build.sh",
]

GENERATED_GLOBS = [
    "out/build/*/build.ninja",
    "out/build/*/compile_commands.json",
]


def scan_file(path: Path, repo: Path) -> list[tuple[str, int, str, str]]:
    """Return (relpath, lineno, needle, why) for each hit."""
    rel = str(path.relative_to(repo)).replace("\\", "/")
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


def collect(repo: Path, globs: list[str]) -> list[Path]:
    out: list[Path] = []
    for g in globs:
        out.extend(p for p in repo.glob(g) if p.is_file())
    return sorted(set(out))


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

    if failures:
        return 1
    print(f"self-test OK: all {len(FORBIDDEN)} patterns detect, det-ok suppresses, clean files pass")
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

    hits: list[tuple[str, int, str, str]] = []
    for p in authored + generated:
        hits.extend(scan_file(p, repo))

    print(f"determinism flag gate: {len(authored)} authored + {len(generated)} generated file(s) scanned")
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
    print("FAILED: a floating-point flag that breaks bit-identical simulation is present.")
    print("See docs/ARCHITECTURE.md D3 and docs/MAINTENANCE.md.")
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
