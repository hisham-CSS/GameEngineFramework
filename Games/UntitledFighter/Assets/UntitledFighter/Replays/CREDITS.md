# The shipped replay

Project-generated content (ROADMAP M2.5, ADR-022 D3) under the repository's
`LICENSE.txt` (**MIT**), per ADR-019 D10 (answered 2026-09-06).

- **Author:** Hisham Ata, via the generator named below.
- **Licence:** MIT (the repository's). Redistribution allowed: **yes**.
- **Generator:** `UntitledFighterCatalogue` (`Games/UntitledFighter/Catalogue/`),
  the project's own cook of the `base` row of
  `Characters/fighter_a/variants/catalogue.json`: fighter_a against itself, the
  verdict's own combo performed from the corner bench, recorded as inputs and
  checkpoints (CSRP, `Games/UntitledFighter/Game/include/cse/game/Replay.h`).
  Every byte derives from MIT-licensed project content; no third-party asset is
  used or derived from.
- **Regenerate:** from `out/build/<preset>/build/bin/<Config>/` run
  `UntitledFighterCatalogue Exported/Characters <scratch dir>` and copy
  `<scratch dir>/base.csrp` here.
- **When:** a replay names exactly one MatchData by hash (`docs/DETERMINISM.md`
  S9), and the Replay intent reads this file against the hash of the match it
  builds. Any frame-data edit to `Characters/fighter_a.json`, any change to the
  binding table (`UntitledFighterMode::MatchBuildOptions` and its twin
  `normalBindings` in `Games/UntitledFighter/Game/src/Catalogue.cpp`), or a
  kernel / `GameState` change makes this file stale:
  `FightMode.AReplayDrivesBothSlotsAndTheTrainingClockStillWorks` goes red with
  the command above in its message, and the mode's honest-error screen says the
  same. Re-run the cook and re-commit the file in the same commit as the change.
