# ADR-022 — VERSUS: one mode, three sources, and a lobby that reads a file before it reads a field

**Status.** Proposed 2026-09-06 · recommended default **not yet enacted** — it
is the shape ROADMAP M2.5 will be built to, and D2 (where a player types a peer
address) is the human's to confirm or change before that build starts; the
rest is safe and reversible and proceeds under CLAUDE.md's clause the moment
M2.5 is taken.

## Context

Every piece of "two people, one match" exists and none of them is wired into a
host. M2.1 built the transport seam (`ITransport`, `UdpTransport`,
`LoopbackNetwork`), M2.2 the connect handshake over the loaded arrays, M2.3 the
desync artifact naming the first divergent field, and M2.4 the driver that
turns a session's events into the fight's ticks plus the mode's
`AttachSession`/`DetachSession` and a headless input seam. `tests/online_peer.cpp`
already plays a whole online match — handshake, session, desync, artifact — as
a process with no window. What is missing is a player's way in: a menu entry,
a lobby, and the mode drawing the match it is playing.

Three constraints decide the shape:

- **ADR-010 E3: one presentation for Training, Replay and Versus.** The
  fighter, the boxes, the HUD and the camera are read off one `FightSession`
  and one `MatchData`; a second mode class with its own draw path is the
  "two copies of a host loop drift" ADR-010 exists to forbid.
- **The engine names no game** (CLAUDE.md; `GameMode.h`). The menu's markup
  (`Editor/src/Exported/UI/menu.cxml`) holds anonymous slots the title's
  registry fills; `InstallMenuUIContent` registers engine verbs. A "peer
  address" field belongs to a fighting game, so it cannot be an engine verb,
  and today no title-authored markup rides in the shared menu.
- **Wire what exists before building what does not** (ADR-010's vertical-slice
  row). `ReplayInputSource` exists; `Handshake` exists; the artifact exists.
  M2.5 is wiring, and its risk is UX, not simulation.

## Decision (recommended default), in five parts

**D1. One mode class, three registry entries.** `UntitledFighterMode` takes an
intent at construction — `Training`, `Replay`, `Versus` — and
`RegisterTitleGameModes` adds three instances with three display names. The
menu shows three verbs and knows nothing else; the Editor's Game view shows the
same three, because both hosts read one registry (Play == Player, M2.6). No
engine change, no second draw path: the intent chooses WHERE THE BITS COME
FROM — the pad and the demonstration (today), a `ReplayInputSource` on both
slots, or `AttachSession` — and nothing else.

**D2. The lobby reads a file, then the command line, and only then a field.**
`Versus` enters a LOBBY state drawn by the mode with the same `Renderer2D` and
font as its HUD: my slot, my port, the peer's address, and the handshake's
verdict as it happens ("waiting for 192.0.2.7:47012", "content hash
0x3b4b21c5 ≠ 0x3b4b21c6 — the peer loaded a different fighter_a.json", then
LIVE). The three values come from `UntitledFighter/versus.json` beside the
title's other authored files (slot, port, peer; the sample ships pointing at
127.0.0.1 so two copies on one machine connect out of the box), overridden by
the same arguments `tests/online_peer.cpp` already takes (`--slot`, `--port`,
`--peer`). **What this defers, and why it is the human's:** typing an address
in-game. The UI toolkit has a `TextField` (`Engine/src/ui/UITextField.h`) and
`push-*` bindings, so the field is cheap; where it lives is not — a
title-authored panel inside the shared menu crosses the engine/title boundary
in a new direction, and a field drawn by the mode duplicates the toolkit's
text editing. Both are product decisions about how the game is entered, not
engineering ones, so the default ships without the field and the ADR asks.

**D3. Desync is the mode's last act.** The mode keeps a `cse::game::StateHistory`
as a `FightSession` observer (every tick, including re-simulated ones, is
pushed by tick), polls `ISession::PollDesync` after each pump, and on a report
does exactly what `online_peer.cpp` does: keep pumping for the grace frames so
the peer detects too, end the session, swap states at tick frame + 1 through
`BlobExchange`, write `DesyncArtifactJson` to `desync_slot<N>.json` beside the
executable, and put the first divergent field on the HUD where the verdict
goes. The match is over from the report; the mode returns to the lobby with
the artifact's path on screen. Never corrected (DETERMINISM.md T6).

**D4. One presentation, one new chip.** `FightView`, `FightHud`, `FightScene`
and `FightPresentation` are untouched. The HUD gains one chip that reads the
mode's intent and, for `Versus`, `ISession::ConnectedPeers`, `FramesAhead` and
`SessionDriver::Counts` — read, never recomputed. `Replay` shows the replay's
tick and its `resimulated` flag from `TickView`. The mode's training controls
stay inert while a session is live (M2.4) and are absent in `Replay` except
pause, step and slow motion, which a replay CAN honour because nothing else is
simulating it.

**D5. Ports and addresses are strings the transport parses.** `UdpTransport`
already takes `ip:port`; the lobby passes them through and reports the
transport's own error text. No hostname resolution in the first build (a
resolver is a platform seam and a blocking call inside a fixed step); IPv4
literals only, said so on the lobby screen.

## What this does not decide

Matchmaking, relays, NAT traversal, spectators, more than two players, a
rematch protocol, an agreed pause (T3 says the host's pause is inert; an agreed
one would be a session message no mode sends), and whether `versus.json` is
per-user data or authored content — the first build treats it as authored and
the Player's install copies it.

## Reversed if

The human wants address entry in-game from the first build (D2 flips to a
title panel or a mode-drawn field, and this ADR records which); or the three
registry entries prove to be three different draw paths after all (D1 was
wrong and the presentation needs a real `FightPresenter` seam, ADR-010 E3's
literal reading); or a second title wants the lobby, in which case it moves
down into the engine as a game-agnostic "connect" screen and the title keeps
only the file.
