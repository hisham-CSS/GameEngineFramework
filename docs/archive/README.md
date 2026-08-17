# Archive — history, not current

**Nothing in this directory is current.** These are verbatim copies of documents
that were rewritten, split or superseded. They are never edited and never
carry a `Verified:` stamp, because nobody re-reads them against the code. If a
sentence here contradicts a living document, the living document wins; if it
contradicts the code, the code wins.

Where the current facts live is [ADR-010 §5](../ADR-010-one-roadmap-one-rule.md)'s
one-fact-one-home table. In short: what is done and what is next is
[ROADMAP.md](../ROADMAP.md); the rules the simulation, build and data must obey
are `docs/DETERMINISM.md`; why a decision was made is its ADR.

## What is here, and where it came from

Every copy below is byte-identical to its original at `99669cc` — the commit the
consolidation started from. Verify with:

```bash
git rev-parse 99669cc:docs/ARCHITECTURE.md && git hash-object docs/archive/ARCHITECTURE-2026-08-12.md
```

| Archived copy | Was | Last changed by | Why it was archived |
|---|---|---|---|
| `NORTHSTAR-2026-08-12.md` | `docs/NORTHSTAR.md` | `a952865`, 2026-08-13 (written `b193d91`, 2026-08-12) | §2's four properties survive as the rewritten `docs/NORTHSTAR.md`; the inventory, the blockers, the roadmap and the appendix moved to [ROADMAP.md](../ROADMAP.md), `docs/DETERMINISM.md` and `ARCHITECTURE.md` §2 |
| `ARCHITECTURE-2026-08-12.md` | `docs/ARCHITECTURE.md` | `a31e498`, 2026-08-13 (written `b193d91`, 2026-08-12) | rewritten so the seven amendment blockquotes and three strike-throughs become prose; the build order, the determinism contract, the "first week" and the adjudication appendix left it |
| `AUDIT_FINDINGS-2026-08-11.md` | `docs/AUDIT_FINDINGS.md` | `8c5ad20`, 2026-08-11 | all 52 findings fixed. Kept because the WHY sections are the best surviving record of what actually goes wrong in this repository; the durable lessons live in [MAINTENANCE.md](../MAINTENANCE.md) |
| `ENGINE_AUDIT-2026-07.md` | `docs/ENGINE_AUDIT_2026-07.md` | `055b199`, 2026-08-12 (audit period July 2026) | a status document from before the fighting game existed. Its laptop measurements moved to `docs/manual/performance.md`; its two open ledger rows to [ROADMAP.md](../ROADMAP.md) |

The date in each filename is the vintage of the *content* — when the document
was written and last substantively edited — not the date it was archived.

## Reading a frozen ADR's line citations

`ADR-001`, `ADR-003`, `ADR-005`, `ADR-006` and `ADR-007` cite
`ARCHITECTURE.md:<line>`. Read those against `ARCHITECTURE-2026-08-12.md` here,
**not** against the living `docs/ARCHITECTURE.md`, which is a different document.

Expect to land near the target rather than exactly on it, and read the
surrounding section rather than the numbered line: some citations were already
off when this snapshot was taken, because amendments were inserted *above* them
after they were written. `ADR-001` cites Phase 0 as `ARCHITECTURE.md:282-294`,
which in this copy is CHOICE C — Phase 0 is roughly 40 lines further down;
`ADR-005` cites `ARCHITECTURE.md:441` for "frame-indexed animation", which is on
line 443. Freezing the file stops that gap growing; it does not close it. This
is why [ADR-010 §8.1](../ADR-010-one-roadmap-one-rule.md) rule 4 says docs are
cited by anchor from here on, never by line.
