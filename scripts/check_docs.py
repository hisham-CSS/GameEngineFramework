#!/usr/bin/env python3
"""Fail the build when the documentation has rotted in a way a machine can see.

WHY THIS EXISTS
---------------
Before this script, zero documents were checked by anything. The drift that
produced was measured rather than guessed (docs/adr/ADR-010 section 1.2): six places
claimed to hold the roadmap, four claimed to hold the rules, seven of twelve
heavily-cited source paths did not resolve, and one of them -- `Engine/src/gameplay/`
-- had never existed at all while five documents cited it. None of that came from
neglect; 25 of 60 commits in the preceding week touched docs/. It came from
having six places to update.

The five-line rule (ADR-010 section 8.1) is the fix. This is the half of it a
machine can enforce.

WHAT IT CHECKS, AND WHAT IT DELIBERATELY DOES NOT
-------------------------------------------------
It does NOT try to detect a stale CLAIM. "The kernel simulates two fighters"
became false when it grew eight slots, and no script will ever notice. That needs
a reader, and MAINTENANCE.md's adversarial audit is the process for it.

What it detects is the mechanical RESIDUE a stale claim leaves behind -- the dead
link, the path that no longer exists, the missing stamp, the amendment bolted on
to a living document instead of folded into it. Every one of those is a fact
about the filesystem, and every one of them was found by hand in the audit that
motivated this file.

  Links          a relative [text](path.md#anchor) whose path does not resolve.
  Cited paths    a backticked token that looks like a repo path and does not
                 exist. `:line` and `:line-line` suffixes are stripped first.
  Stamps         a living doc with no `Verified: <date> @ <sha>` in its first ten
                 lines; an ADR with no `Status` line in its first ten.
  Markers        AMENDED / STRUCK / ~~strike~~ / "> **Amendment" / "Correction ("
                 in a living document. Rewrite the sentence instead; an ADR is
                 frozen and gets a new ADR, not an annotation.
  Archive        docs/archive/ still holds byte-for-byte what it was frozen with.

SKIPPING CODE IS LOAD-BEARING, NOT POLITE
-----------------------------------------
The link and marker checks ignore fenced blocks and inline code spans. This is
not tidiness: a C++ lambda in a code sample -- `[s, idx](UIEvent&) { ... }`, which
is really in docs/AUDIT_FINDINGS.md -- parses as a markdown link to a file named
`UIEvent&`, and ADR-010's own table contains `[text](path.md#anchor)` as an
EXAMPLE of the thing being checked. A gate that reports both is a gate everyone
learns to ignore.

The cited-path check does the opposite and reads inline code spans, because that
is exactly where a path is written. It skips fenced blocks, where a path is
usually part of a command someone is being shown rather than a claim about this
repository.

USAGE
-----
    python scripts/check_docs.py              # check the repo
    python scripts/check_docs.py --self-test  # prove every check detects

Suppress a genuine false positive with `docs-ok` in a comment on that line, and
say why -- mirroring `det-ok` in scripts/check_determinism_flags.py. That is for
the deliberately absent: a path a document names in order to say it does not
exist.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

# Directories whose markdown is not ours to keep current. docs/archive/ is here
# for the reason it exists: its contents are frozen snapshots full of dead paths
# and strike-throughs, and that is correct. Its INTEGRITY is checked separately,
# below, which is the only thing that should ever be true of it.
SKIP_DIRS = {"ThirdParty", "out", "Builds", ".git", ".vs", "vcpkg_installed"}
SKIP_RELPREFIXES = ("docs/archive/",)

# Documents that must carry `Verified: <date> @ <sha>`. Anything else -- an
# asset README, a fixture note -- is still link- and path-checked, but nobody
# re-reads it against the code on a schedule, so demanding a stamp would only
# teach people to write one without meaning it.
LIVING_GLOBS = ["README.md", "CLAUDE.md", "docs/*.md", "docs/manual/*.md"]

# An accepted ADR is frozen: it carries a Status line instead of a stamp, and it
# is allowed the annotation markers a living document is not, because its
# corrections arrive as later ADRs. Matched by NAME as well as by directory so
# this keeps working both before and after ADR-010 moves them into docs/adr/.
ADR_DIR = "docs/adr/"
ADR_NAME = re.compile(r"^ADR-\d+.*\.md$")

# The top-level directories a backticked token has to start with before this
# script will claim it is a repo path. Deliberately a closed list: `Engine/src/`
# is a path and `startup/active/recovery` is not, and no amount of cleverness
# distinguishes them as reliably as naming the eleven directories that exist.
PATH_PREFIXES = (
    "Engine/", "Editor/", "Player/", "Games/", "Net/", "Cooker/",
    "docs/", "tests/", "scripts/", "cmake/", "tools/", "ThirdParty/",
)

# Annotation markers. Each one is a way of correcting a document without
# rewriting it, which is how ARCHITECTURE.md ended up with seven amendment
# blockquotes stacked above the line numbers five ADRs cite into it.
MARKERS = [
    ("AMENDED",        "rewrite the sentence; a living document has no amendments"),
    ("STRUCK",         "rewrite the sentence; a living document has no strike-outs"),
    ("> **Amendment",  "fold the amendment into the prose it amends"),
    ("Correction (",   "correct the text itself, or write a new ADR if it is frozen"),
]
STRIKETHROUGH = re.compile(r"~~[^~]+~~")

STAMP = re.compile(r"Verified:\s*\d{4}-\d{2}-\d{2}\s*@\s*\S+")
STATUS = re.compile(r"^\s*\**Status\b", re.IGNORECASE)
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
CODE_SPAN = re.compile(r"`([^`]+)`")
STAMP_LINES = 10

# docs/archive/ frozen at the commit the consolidation started from. The hash is
# git's own blob hash, recomputed here in pure Python so this runs in a CI
# checkout with no history (actions/checkout defaults to fetch-depth 1, so
# `git rev-parse 99669cc:...` is not available there).
#
# WHY CHECK THIS AT ALL: five frozen ADRs cite ARCHITECTURE.md by line number,
# and docs/archive/README.md tells a reader to resolve those against the copy
# here. An edit to one of these files moves every one of those citations, and
# nothing else in this repository would notice.
ARCHIVE_MANIFEST = {
    "docs/archive/NORTHSTAR-2026-08-12.md":     "e6741647d3c6671b8d763cfee92b8e78cf6bbaad",
    "docs/archive/ARCHITECTURE-2026-08-12.md":  "6c79a0078d56d8e0392e2186fd411687d1499fe0",
    "docs/archive/AUDIT_FINDINGS-2026-08-11.md": "86bfece693d75845670583e418ebc48bebf92fc5",
    "docs/archive/ENGINE_AUDIT-2026-07.md":     "a6fdb43c05477d9006eefd178c63d23a972f4130",
}


def blob_hash(path: Path) -> str:
    """git's blob hash of a file, normalising CRLF to LF first.

    The normalisation is what makes this portable rather than decorative: this
    repository is authored on Windows with core.autocrlf=true, so the same file
    is CRLF in a Windows working tree and LF in a Linux one while git's blob --
    the thing the manifest records -- is LF either way. Hashing the raw bytes
    would pass in CI and fail on the author's machine.
    """
    data = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def strip_code(text: str) -> list[str]:
    """One string per line with fenced blocks and inline spans blanked out.

    Line numbers are preserved, because a finding that cannot be jumped to is a
    finding somebody has to grep for.
    """
    out: list[str] = []
    fenced = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
            out.append("")
            continue
        out.append("" if fenced else CODE_SPAN.sub("", line))
    return out


def code_spans(text: str) -> list[tuple[int, str]]:
    """(lineno, span-contents) for every inline code span outside a fence."""
    out: list[tuple[int, str]] = []
    fenced = False
    for n, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        out.extend((n, m.group(1)) for m in CODE_SPAN.finditer(line))
    return out


# A token that is a PATTERN rather than a path. All three of these are real
# spellings in this repository's documents, and reporting any of them would be
# reporting a file nobody claimed exists:
#
#   `docs/manual/{assets, performance, ...}.md`   a brace list of several pages
#   `Games/…`                                     an ellipsis standing for "and so on"
#   `Engine/src/core/FileWatch.{h,cpp}`           one name, two extensions
#
# Non-ASCII is the general form of the second: no path in this tree contains a
# character outside ASCII, so anything that does is prose.
PATTERN_CHARS = set("*?<>|{}")


def looks_like_path(token: str) -> str | None:
    """The repo-relative path a backticked token names, or None.

    Strips a `:line` or `:line-line` suffix, because citing code by path and line
    is normal here and the line is not part of the filename.
    """
    token = token.strip().strip(",.;:!?()[]{}\"'")
    if not token.startswith(PATH_PREFIXES):
        return None
    token = re.sub(r":\d+(-\d+)?$", "", token)
    if not token or any(c in token for c in PATTERN_CHARS):
        return None
    if not token.isascii():
        return None
    return token


def is_adr(rel: str) -> bool:
    return rel.startswith(ADR_DIR) or bool(ADR_NAME.match(rel.rsplit("/", 1)[-1]))


def is_living(rel: str, repo: Path) -> bool:
    if is_adr(rel):
        return False
    return any(rel == g or (g.endswith("/*.md") and rel.startswith(g[:-5] + "/")
                            and "/" not in rel[len(g) - 4:])
               for g in LIVING_GLOBS)


def markdown_files(repo: Path) -> list[Path]:
    out = []
    for p in sorted(repo.rglob("*.md")):
        rel = p.relative_to(repo).as_posix()
        if any(part in SKIP_DIRS for part in p.relative_to(repo).parts):
            continue
        if rel.startswith(SKIP_RELPREFIXES):
            continue
        out.append(p)
    return out


def check_file(path: Path, repo: Path) -> list[tuple[str, int, str, str]]:
    """Return (relpath, lineno, kind, detail) for every finding in one file."""
    rel = path.relative_to(repo).as_posix()
    text = path.read_text(encoding="utf-8", errors="replace")
    raw = text.splitlines()
    findings: list[tuple[str, int, str, str]] = []

    def exempt(n: int) -> bool:
        return n <= len(raw) and "docs-ok" in raw[n - 1]

    # --- links and markers: code is skipped -------------------------------
    for n, line in enumerate(strip_code(text), 1):
        if exempt(n):
            continue
        for m in LINK.finditer(line):
            target = m.group(1).split("#", 1)[0]
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            if not (path.parent / target).resolve().exists():
                findings.append((rel, n, "link", f"{target} does not resolve"))
        if not is_adr(rel):
            for needle, why in MARKERS:
                if needle in line:
                    findings.append((rel, n, "marker", f"{needle!r}: {why}"))
            if STRIKETHROUGH.search(line):
                findings.append((rel, n, "marker",
                                 "strike-through: rewrite the sentence instead"))

    # --- cited paths: code spans are exactly where these live -------------
    #
    # Skipped in an ADR, for the same reason markers are: an ADR is a record of
    # a decision at a moment, and its paths are a description of the tree AS IT
    # WAS. ADR-001 cites `Editor/src/Exported/Characters/`, which was where the
    # characters lived when it was written and is not where they live now.
    # Demanding that it resolve leaves exactly two options -- rewrite a frozen
    # document, or never let this gate go green -- and both are worse than
    # accepting that history describes history.
    if not is_adr(rel):
        for n, span in code_spans(text):
            if exempt(n):
                continue
            for token in span.split():
                target = looks_like_path(token)
                if target and not (repo / target).exists():
                    findings.append((rel, n, "path", f"`{target}` does not exist"))

    # --- stamps ------------------------------------------------------------
    head = "\n".join(raw[:STAMP_LINES])
    if is_adr(rel):
        if not any(STATUS.match(l) for l in raw[:STAMP_LINES]):
            findings.append((rel, 1, "stamp",
                             "an ADR needs a Status line in its first "
                             f"{STAMP_LINES} lines"))
    elif is_living(rel, repo) and not STAMP.search(head):
        findings.append((rel, 1, "stamp",
                         "a living document needs `Verified: YYYY-MM-DD @ sha` "
                         f"in its first {STAMP_LINES} lines"))
    return findings


def check_archive(repo: Path) -> list[tuple[str, int, str, str]]:
    out = []
    for rel, want in sorted(ARCHIVE_MANIFEST.items()):
        p = repo / rel
        if not p.is_file():
            out.append((rel, 1, "archive", "missing: the archive is what five "
                                           "frozen ADRs cite into"))
            continue
        got = blob_hash(p)
        if got != want:
            out.append((rel, 1, "archive",
                        f"edited since it was frozen ({got[:12]} != {want[:12]}). "
                        "An archive is never edited"))
    return out


def self_test() -> int:
    """Prove every check detects. A gate nobody has watched fail is not a gate."""
    import tempfile

    failures = 0

    def expect(fixture: str, kind: str | None, name: str, rel: str = "docs/x.md",
               alongside: tuple[str, ...] = ()):
        nonlocal failures
        with tempfile.TemporaryDirectory() as td:
            repo = Path(td)
            for other in alongside:
                q = repo / other
                q.parent.mkdir(parents=True, exist_ok=True)
                q.write_text("target\n", encoding="utf-8")
            p = repo / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(fixture, encoding="utf-8")
            kinds = {f[2] for f in check_file(p, repo)}
            if kind is None and kinds - {"stamp"}:
                print(f"SELF-TEST FAILED: {name} was flagged as {sorted(kinds)}")
                failures += 1
            elif kind is not None and kind not in kinds:
                print(f"SELF-TEST FAILED: {name} produced {sorted(kinds)}, "
                      f"expected a {kind!r} finding")
                failures += 1

    stamp = "Verified: 2026-01-01 @ abc1234\n"

    expect(stamp + "see [it](nope.md)\n", "link", "a dead relative link")
    expect(stamp + "see [it](../DETERMINISM.md#rules)\n", None,
           "a link that resolves, anchor and all",
           rel="docs/manual/x.md", alongside=("docs/DETERMINISM.md",))
    expect(stamp + "see [it](https://example.com/nope.md)\n", None, "an http link")
    expect(stamp + "`[text](path.md#anchor)` as an example\n", None,
           "a link inside an inline code span")
    expect(stamp + "```\n[s, idx](UIEvent&) { }\n```\n", None,
           "a C++ lambda inside a fenced block")

    expect(stamp + "the file `Engine/src/nope.cpp` does the thing\n", "path",
           "a cited path that does not exist")
    expect(stamp + "the file `Engine/src/nope.cpp:42` does the thing\n", "path",
           "a cited path with a :line suffix")
    expect(stamp + "`Engine/src/nope.cpp` <!-- docs-ok: deliberately absent -->\n",
           None, "a cited path exempted with docs-ok")
    expect(stamp + "`startup/active/recovery` frames\n", None,
           "a backticked token that is not a path")
    expect(stamp + "`docs/manual/{assets, performance}.md` need stamps\n", None,
           "a brace list of several pages")
    expect(stamp + "`Engine/src/core/FileWatch.{h,cpp}`\n", None,
           "one name with two extensions")
    expect(stamp + "a token like `Games/…` in a prefix list\n", None,
           "an ellipsis standing for the rest of a tree")
    expect(stamp + "run `ctest --preset x64-relwithdebinfo-tests -LE \"perf|gl\"`\n",
           None, "a command line in a code span")

    expect(stamp + "> **Amendment** — later\n", "marker", "an amendment block")
    expect(stamp + "this was AMENDED in July\n", "marker", "an AMENDED marker")
    expect(stamp + "~~no longer true~~\n", "marker", "a strike-through")
    expect(stamp + "`~~this is a code sample~~`\n", None,
           "a strike-through inside a code span")

    expect("no stamp here\n", "stamp", "a living doc with no stamp")
    expect(stamp + "fine\n", None, "a living doc with a stamp")
    expect("**Status.** Accepted\n~~struck~~\n`Engine/src/gone/` was here\n", None,
           "a frozen ADR: no stamp needed, markers and historical paths allowed",
           rel="docs/adr/ADR-001-x.md")
    expect("no status line\n", "stamp", "an ADR with no Status line",
           rel="docs/adr/ADR-001-x.md")
    expect("**Status.** Accepted\n", None, "an ADR named in the old location",
           rel="docs/adr/ADR-001-x.md")

    # The archive check, both ways. Hand-built rather than reusing the real
    # manifest, because the point is that an EDIT is caught.
    with tempfile.TemporaryDirectory() as td:
        repo = Path(td)
        p = repo / "docs/archive/frozen.md"
        p.parent.mkdir(parents=True, exist_ok=True)
        # write_bytes, not write_text: text mode translates "\n" to "\r\n" on
        # Windows, so the CRLF case below would be testing "\r\r\n" and this
        # probe would prove nothing about the thing it is named after.
        p.write_bytes(b"original\n")
        good = blob_hash(p)
        saved = dict(ARCHIVE_MANIFEST)
        try:
            ARCHIVE_MANIFEST.clear()
            ARCHIVE_MANIFEST["docs/archive/frozen.md"] = good
            if check_archive(repo):
                print("SELF-TEST FAILED: an unedited archive file was flagged")
                failures += 1
            p.write_bytes(b"original\r\n")
            if check_archive(repo):
                print("SELF-TEST FAILED: a CRLF checkout of an unedited archive "
                      "file was flagged, so this gate would be red on Windows "
                      "and green in CI")
                failures += 1
            p.write_bytes(b"edited\n")
            if not check_archive(repo):
                print("SELF-TEST FAILED: an edited archive file was not flagged")
                failures += 1
            p.unlink()
            if not check_archive(repo):
                print("SELF-TEST FAILED: a missing archive file was not flagged")
                failures += 1
        finally:
            ARCHIVE_MANIFEST.clear()
            ARCHIVE_MANIFEST.update(saved)

    # And that the real manifest names files that are actually there. A manifest
    # whose paths have drifted checks nothing and says so nowhere.
    repo = Path(__file__).resolve().parent.parent
    for rel in ARCHIVE_MANIFEST:
        if not (repo / rel).is_file():
            print(f"SELF-TEST FAILED: the archive manifest names {rel}, which "
                  f"does not exist")
            failures += 1

    if failures:
        return 1
    print(f"self-test OK: dead links, cited paths, stamps, Status lines, "
          f"{len(MARKERS) + 1} annotation markers and archive integrity all "
          f"detect; code spans and fenced blocks are skipped for links and "
          f"markers and read for paths; docs-ok suppresses")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true",
                    help="prove every check detects, then exit")
    ap.add_argument("--repo", default=None, help="repository root")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    repo = Path(args.repo) if args.repo else Path(__file__).resolve().parent.parent
    files = markdown_files(repo)

    findings: list[tuple[str, int, str, str]] = []
    for p in files:
        findings.extend(check_file(p, repo))
    findings.extend(check_archive(repo))

    living = sum(1 for p in files if is_living(p.relative_to(repo).as_posix(), repo))
    adrs = sum(1 for p in files if is_adr(p.relative_to(repo).as_posix()))
    print(f"docs gate: {len(files)} markdown file(s) scanned "
          f"({living} living, {adrs} ADR), "
          f"{len(ARCHIVE_MANIFEST)} archived file(s) verified")

    if not files:
        print("  WARNING: no markdown found. If docs/ moved, this gate checks nothing.")

    if not findings:
        print("  OK: no dead link, no missing path, no missing stamp, no annotation.")
        return 0

    by_kind: dict[str, int] = {}
    for _, _, kind, _ in findings:
        by_kind[kind] = by_kind.get(kind, 0) + 1
    print()
    print("FAILED: " + ", ".join(f"{n} {k}" for k, n in sorted(by_kind.items())))
    print("The rule is docs/adr/ADR-010-one-roadmap-one-rule.md section 8.1: one home "
          "per fact, fixed in the same commit, living docs rewritten and ADRs "
          "frozen, cited by anchor, stamped.")
    print()
    for rel, n, kind, detail in findings:
        print(f"  {rel}:{n}: {kind}: {detail}")
    print()
    print("If a finding is genuinely intentional -- a path named in order to say "
          "it does not exist -- put `docs-ok` in a comment on that line with the "
          "reason, so the exemption shows up in review.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
