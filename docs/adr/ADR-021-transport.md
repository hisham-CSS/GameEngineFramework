# ADR-021 — Transport: UDP over the platform's sockets, behind a seam the tests can stand in for

**Status.** Proposed 2026-09-06 · recommended default enacted under CLAUDE.md's
safe-and-reversible clause (no dependency added; the seam reverses it) — the
human confirms or reverses by editing this line.

## Context

ROADMAP M2.1 is "Transport — spike, then an ADR". ADR-002 CHOICE A adopted
GekkoNet for the rollback session and ADR-003 vendored it behind `ISession`, a
seam that moves bytes and a length and names no `GameState`. Until this WP the
seam carried local and stress sessions only: `GekkoSession.cpp` added local
actors and nothing ever left the process. Two people, one match, needs packets
to travel, and GekkoNet leaves that to a `GekkoNetAdapter` — three C function
pointers (send, receive, free) with no user-data argument.

Three facts shaped the spike:

- **GekkoNet's own UDP adapter is not in this tree.** `ThirdParty/CMakeLists.txt`
  builds GekkoNet `NO_ASIO`, on purpose: the adapter is the only user of asio,
  and excluding it drops a 6.8 MB header tree and a Boost licence from the build.
  That decision stands; the note beside it already said "we supply transport
  through ISession".
- **The adapter has no user-data pointer**, so an object behind it can only be
  reached through a static. Whatever the transport is, one session per process
  is the natural shape; more than one needs one set of static functions each.
- **The tests must not need a socket.** CI runs the suite on a shared Windows
  runner; a test that binds ports is a test that flakes on someone else's
  schedule. The property under test — two kernels agreeing frame for frame
  through prediction, rollback, latency and loss — is a property of the session
  and the kernel, not of UDP.

## Decision (recommended default), in four parts

1. **`ITransport` is the seam**, in `Net/include/cse/net/ISession.h` beside
   `ISession`: `Send(to, bytes, length)` to an opaque string address, and
   `Receive()` returning every packet that arrived since the last call with the
   address it came from. Unreliable and unordered by contract; GekkoNet carries
   its own input redundancy and never expects a transport to retransmit.
   `SessionConfig::peerAddresses` names every slot, local (empty) or remote (an
   address), in slot order so handles equal slots; `SessionConfig::localDelay`
   is GekkoNet's local input delay. `CreateGekkoOnlineSession(cfg, transport)`
   requires a transport. GekkoNet's adapter type appears in `GekkoSession.cpp`
   and nowhere else, as before.
2. **`UdpTransport` is the transport of record**: one non-blocking IPv4 socket
   over Winsock or BSD sockets, addresses as `"ip:port"`, no library beyond the
   platform's. `Net/CMakeLists.txt` links `ws2_32` PRIVATE, which the existing
   leak check admits as `$<LINK_ONLY:>`.
3. **`LoopbackNetwork` is the tests' transport**: named endpoints in one
   process, a frame clock the test advances, latency in frames and a
   drop-every-n, all deterministic. Four `Bridge<N>` slots in `GekkoSession.cpp`
   let up to four online sessions share a process; the shipped game uses one.
4. **The bridge mirrors GekkoNet's own memory convention exactly**: `Receive`
   hands back `malloc`'d results GekkoNet frees through the bridge's `free`,
   three calls per result (the result, its address, its data), because that is
   what `backend.cpp` does with the adapter it ships.

*Recommended: accept.* It adds no dependency, touches no kernel, and every
part is reversible through the seam: a different transport implements
`ITransport`; a different session implements `ISession`.

## What was measured

- `Session.TwoPeersOverALoopbackTransportAgreeOnEveryChecksum`: two online
  sessions, one player each, 400 frames; every frame both confirmed carries the
  same checksum on both sides; both saw the other connect.
- `Session.LatencyForcesRollbacksAndBothPeersConverge`: four frames of latency
  against a local delay of two; rollbacks happen (loads counted) and the peers
  still agree.
- `Session.LossIsSurvivedByTheSessionsOwnRedundancy`: after the peers connect,
  one packet in four vanishes in every direction; they still agree. Started
  before the connection, the same loss ate the handshake and the match never
  began — a finding about GekkoNet's handshake, recorded in the test.
- `Session.ADivergentPeerIsReportedAndNamed`: one peer's kernel starts
  diverging at frame 120; a desync is reported within 200 frames, dated after
  the divergence, with two checksums that differ (ADR-002 CHOICE C between two
  kernels).
- `test_online_two_peers`: two processes, `UdpTransport` on 127.0.0.1, 240
  frames; both exit 0 with the same checksum at the same frame and each counted
  the other as connected. With the bridge made to receive nothing, the four
  loopback tests fail naming "never saw connect" and "barely advanced".

## What this does not decide

NAT traversal, a relay, matchmaking, encryption, IPv6, spectators, more than
two players over the wire, and the host's obligations — the tick count, pacing
by `FramesAhead()`, stopping the match on a desync — which are M2.2 to M2.6.
`online_peer` paces by sleeping a frame, or two when ahead; that is a test
harness, not the mode.

## Reversed if

- A transport this design cannot express is needed (a relay that must know the
  session's frame, encryption keyed per player): the seam gains what it needs
  or a new ADR replaces it.
- The one-session-per-process shape bites a real use (a dedicated spectator
  relay in the same process as a player): raise `kBridgeSlots`, or give
  GekkoNet a user-data argument upstream.
- GekkoNet is replaced (ADR-002 CHOICE A's reversal): `ITransport` survives,
  the bridge goes with `GekkoSession.cpp`.
