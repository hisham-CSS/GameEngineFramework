# Phase 0 corpus — test fixtures, not game content

Three characters transcribed from open MUGEN/Ikemen `.cns` sources during Phase 0
(see [`docs/adr/ADR-001-fighting-core.md`](../../../docs/adr/ADR-001-fighting-core.md)).
They exist to answer one question, and they answered it: **can a real
fighting-game character be expressed in the declarative fragment the combo prover
decides over?** 59 moves, 247 cancels, and the measured answer is in the ADR.

## Why they live here and not in `Games/UntitledFighter/Assets/Characters/`

**They are not ours to ship.** The frame data was derived from third-party MUGEN
characters, and this project holds no licence to them. Frame numbers may well be
uncopyrightable facts, but the names are not, the move sets are not, and a game
that shipped `kung_fu_girl.json` would be distributing a description of someone
else's character. The same rule the repository already applies to the placeholder
backpack model applies here: **evidence is not content.**

Keeping them is still right. They are the only data in the repository that came
from a real shipped fighting game rather than from us, which makes them the only
regression test that can catch a loader or adapter change that works fine on
characters we designed to fit it. A schema that only its own author's files
validate against is not a schema.

## What depends on them

`tests/test_character_data.cpp`, `tests/test_prover_adapter.cpp`,
`tests/test_match_bridge.cpp` and `tests/test_cancels.cpp` resolve this directory
through `corpusDir()`. They assert the numbers ADR-001 measured — 25/134,
24/87, 10/26 moves and cancels, and the corner verdicts INFINITE / INFINITE /
TERMINATING. A mismatch is a defect in the code, not a reason to update the
expectation.

**These files are never staged next to an executable.** `stage_runtime_assets.cmake`
copies `Editor/src/Exported`, and this directory is deliberately outside it.

## `schema.v1.json`

Kept beside them because it is what they were authored against. The shipping
schema is v2, in `Games/UntitledFighter/Assets/Characters/`. ADR-001 records that a v1 file
is already a valid v2 file, so the corpus validates against either — which is
itself one of the things the fixtures are here to keep true.
