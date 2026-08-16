// Hitboxes, hurtboxes, and the one hit a tick can resolve.
//
// This is the first thing in the kernel that can make one fighter hit another.
// Everything before it moved two independent characters around a stage and
// counted down a stun nobody could inflict.
//
// ---------------------------------------------------------------------------
// WHERE THE BOX DATA LIVES, AND WHY IT IS A PARAMETER RATHER THAN A FIELD
// ---------------------------------------------------------------------------
// The alternative was a fixed-capacity move table inside GameState. It is
// rejected, and the rule that rejects it is already written down in
// Games/UntitledFighter/Data/include/cse/data/CharacterData.h: "if a tick
// WRITES it, it is an integer field in GameState; if a tick only READS it, it
// lives [in character data] and the state holds an index."
//
// A hitbox is read-only for the whole match. No tick writes one. So putting the
// table in GameState would:
//
//   * memcpy ~2.6 KB of unchanging data 128 times a second, in and out of a ring
//     whose entire budget argument (D4) is that the snapshot is small;
//   * put character data inside the desync checksum, where a content mismatch
//     would surface as "desync at tick 3" instead of as the lobby error
//     §4.8 specifies -- the connect handshake hashes the loaded data ONCE, and
//     that is the right place for it;
//   * create a second copy of the move table, seeded by ResetMatch, that can
//     drift from the CseData one it was copied from. Two sources of truth for
//     the frame data is exactly the failure D8 warns about, where the engine and
//     the analysis disagree by a frame.
//
// So `Simulate` takes it. The data is a POD of integers owned by this header --
// the kernel still links nothing, and CseData (which has std::string and
// std::vector and must never be reachable from a tick) fills one of these in at
// match start. The parameter is a reference to immutable data, so `Simulate`
// stays a pure function of its arguments: two peers with the same bytes compute
// the same tick, and re-simulation after a rollback reads the same bytes it read
// the first time, because nothing can have written them in between.
//
// The one thing this trades away: the snapshot no longer describes the match on
// its own. A saved GameState is meaningless without the MatchData it was
// simulated against, and it is the connect handshake's job to prove both peers
// hold the same one. That is a property of the design either way -- a move-table
// copy inside GameState would only have moved the proof, not removed it.
//
// ---------------------------------------------------------------------------
// WHAT IS DELIBERATELY NOT HERE
// ---------------------------------------------------------------------------
// Hitstop/freeze is OUT OF SCOPE and is not half-implemented here. It is not a
// box question at all -- it is a rule about which fighters advance on which
// ticks, it needs its own counter in GameState, and it changes the meaning of
// every frame number in the character data. Doing a piece of it would leave the
// kernel in a state where a designer's "startup 5" means five ticks sometimes.
//
// Also absent, and each for the same reason -- it is Phase 3 in
// ARCHITECTURE.md's build order and does not fit in "one hit": blocking and
// blockstun, pushback, juggle and proration, per-frame (rather than per-move)
// boxes, throw boxes, push boxes, priority and trade resolution beyond the
// symmetric rule below, and hitstun decay.
//
// ---------------------------------------------------------------------------
// CANCELS, AND THE ONE THING THEY ARE NOT ALLOWED TO COST
// ---------------------------------------------------------------------------
// A cancel edge is the sentence "move A, having connected, may be interrupted
// into move B after a delay". It arrived after the hit resolution above, and it
// arrived under a hard constraint: GameState's LAYOUT IS A WIRE CONTRACT. Its
// size, its per-field offsets and a golden checksum computed over it are
// asserted by tests/test_kernel.cpp and tests/test_determinism_crossplat.cpp,
// and the second of those is the evidence that gcc and MSVC agree byte for byte.
// Re-recording that golden is how the evidence gets destroyed, so the cancel
// rule below is built to need NO NEW FIELD IN GameState. It reads three things
// the state already carries -- moveId, moveFrame and alreadyHitBits -- and one
// table that lives here, next to the move table and for the same reason: no tick
// writes it.
//
// The consequence, stated rather than hidden. The state records THAT the current
// attack has connected (alreadyHitBits) and not WHEN. So "a delay of d ticks
// after contact" cannot be evaluated against the actual contact tick, and
// CancelEdge instead carries an absolute [earliestFrame, latestFrame] window in
// the SOURCE MOVE's own frame numbering, resolved once at load by the bridge
// that knows both the delay and the move's startup. The bridge measures from the
// first frame the move could possibly have connected on, which is the earliest
// contact and therefore the permissive end; MatchBuilder.cpp counts every edge
// that reading affects in its loss table, under `cancel.contact_frame`. The
// alternative -- a contactFrame byte in Fighter -- costs four bytes of state, a
// new golden hash and the crossplay proof that came with the old one, to buy
// back at most `active - 1` ticks of precision. That trade is refused here and
// the refusal is what this paragraph exists to record.
#pragma once

#include "GameState.h"

#include <cstdint>
#include <type_traits>

namespace cse::kernel {

// --- Boxes ------------------------------------------------------------------

// An axis-aligned integer rectangle in SUB-UNITS (1 pixel = 256), authored
// RELATIVE TO THE FIGHTER'S ORIGIN and as if the fighter faced +X. +X is stage
// right and +Y is up, matching Fighter::posX/posY.
//
// HALF-OPEN on both axes: the rectangle covers x in [x0, x1) and y in [y0, y1).
// Half-open removes the off-by-one that inclusive bounds smuggle into every
// width calculation (x1 - x0 is the width, full stop), and it makes two boxes
// that merely touch NOT overlap -- so a hit at exactly maximum range is a clean
// miss rather than a value that depends on which end of the box you measured.
struct Box {
    std::int32_t x0;
    std::int32_t y0;
    std::int32_t x1;
    std::int32_t y1;
};

// The largest magnitude a box coordinate may have: 4096 pixels, which is eight
// stage widths. This bound is not decoration -- it is what makes the arithmetic
// below provably overflow-free (see PlaceBox) and what makes negation total:
// INT32_MIN has no positive counterpart, and a mirror that cannot negate one
// value is a mirror with a hole in it.
inline constexpr std::int32_t kMaxBoxCoord = 1 << 20;

// The largest magnitude a fighter origin may have when a box is placed. Far
// beyond the stage clamp (480 px), because this is a safety bound rather than a
// gameplay one: it makes PlaceBox total for ANY GameState, including one a test
// or a corrupt packet built by hand.
inline constexpr std::int32_t kMaxWorldCoord = 1 << 24;

// True if a box is a well-formed, non-empty rectangle within the bounds above.
// Exposed for the data loader, which is where a bad box should be rejected --
// the simulation itself is total and never needs to ask.
bool BoxIsValid(const Box& b);

// Reflect a box across x = 0, for a fighter that faces -X.
//
// NEGATE AND SWAP, AND IT IS EXACT.
//
//     { x0, y0, x1, y1 }  ->  { -x1, y0, -x0, y1 }
//
// Each vertical edge is negated, and the two exchange roles, because negation
// reverses the order of the number line and x0 must remain the smaller edge.
//
// Why this construction and not a multiply. Integer negation is exact for every
// value in [-kMaxBoxCoord, kMaxBoxCoord], it is its own inverse, and it
// introduces no rounding decision at all -- mirroring a box twice returns the
// original bytes, and the mirrored box is the same width as the original by
// construction rather than by luck. Any construction that scales instead --
// reflecting about a pivot, multiplying by a signed facing, or halving a width
// to find a centre -- goes through a division, and D2 spells out what a division
// costs here: `>>` rounds toward minus infinity while `/` truncates toward zero,
// so a left-facing box can lose a least-significant sub-unit that the identical
// right-facing box keeps. One sub-unit of reach is 1/256th of a pixel, and it
// decides whether a combo connects, which is the difference between Terminating
// and Infinite in the proof this engine is the case study for.
//
// Note that multiplying each edge by -1 WITHOUT the swap is also exact, and also
// wrong: it produces x0 > x1, an inside-out rectangle that every overlap test
// below reports as empty, so a mirrored character would simply never hit
// anything. The swap is not an optimisation of the multiply. It is the half of
// the operation the multiply forgets.
//
// Y is not touched. Mirroring is horizontal; a fighter turning around does not
// turn upside down.
Box MirrorBox(const Box& local);

// Put a fighter-relative box into stage coordinates: mirror it if the fighter
// faces -X (facing != 0, per Fighter::facing), then translate by the origin.
//
// Both steps are exact, and their composition is exact, which is the property
// the mirror test rests on:
//
//     PlaceBox(b, -px, py, facing^1)  ==  MirrorBox(PlaceBox(b, px, py, facing))
//
// Inputs are clamped to the bounds above before use. The clamps are SYMMETRIC
// about zero (-kMaxBoxCoord..+kMaxBoxCoord), so clamping commutes with negation
// -- clamp(-v) == -clamp(v) -- and the guard that exists to prevent overflow
// cannot itself introduce the asymmetry this whole file is about.
Box PlaceBox(const Box& local, std::int32_t posX, std::int32_t posY,
             std::uint8_t facing);

// Half-open overlap. Boxes that share an edge do not overlap.
//
// This predicate is invariant under mirroring BOTH boxes, which is why the
// half-open convention is safe here even though the mirror of the point set
// [a, b) is (-b, -a]: the test only ever compares one box's edges against the
// other's, both endpoints move together under negation, and each of the four
// comparisons turns into the comparison it is paired with. Written out:
// a.x0 < b.x1 becomes -a.x1 < -b.x0, i.e. b.x0 < a.x1, which is the third
// comparison; and vice versa.
bool BoxesOverlap(const Box& a, const Box& b);

// --- The read-only character data a tick may look at ------------------------

// One attack. Frame numbers are ticks from the start of the move, where the tick
// the move starts is frame 0. The hitbox is live on frames
// [startup, startup + active) and the move ends after
// startup + active + recovery ticks.
struct MoveDef {
    std::int32_t startup;
    std::int32_t active;
    std::int32_t recovery;

    // Damage, in the SAME UNITS as Fighter::health. Not hundredths: CseData
    // stores hundredths (CharacterData.h) and the loader converts once, at load,
    // which is D8's rule -- exactly one documented quantization, applied
    // identically on both peers. A second conversion here would be a second
    // rounding rule in a file whose whole point is that there are none.
    std::int32_t damage;

    // Ticks of hitstun inflicted on hit. Clamped into Fighter::hitstun's uint16
    // range when applied; a negative value means no stun.
    std::int32_t hitstun;

    // The box, authored facing +X. Mirrored by MirrorBox when the fighter faces
    // the other way, never by scaling it.
    Box hitbox;

    // The input bits that start this move. ALL of them must be held, and zero
    // means "not startable from a button" (a move only reachable from a cancel,
    // once cancels exist). Held, not pressed: edge detection needs the previous
    // tick's buttons inside GameState, because a rollback restores state and
    // hands Simulate only the current tick's input -- that field is a deliberate
    // omission, not an oversight. See the note in Combat.cpp.
    std::uint16_t button;

    // Explicit, for the same reason Fighter has no implicit padding: §4.8 says
    // the connect handshake hashes the LOADED POD ARRAYS, and hashing a struct
    // with indeterminate padding compares two machines' uninitialised bytes.
    std::uint16_t pad_;
};

// 32 moves per fighter. A hard cap, deliberately: D4 forbids unbounded growth in
// anything the simulation reads, and a cap that is checked at load is a lobby
// error, while a cap that is not is a buffer overrun.
inline constexpr std::int32_t kMaxMovesPerFighter = 32;

// --- Cancels ----------------------------------------------------------------

// One directed cancel edge: "while performing `from`, start `to` instead".
//
// WHY THE WINDOW IS ABSOLUTE AND NOT A DELAY. The authored datum is a delay in
// ticks measured from the moment the source move CONNECTED (see
// Games/UntitledFighter/Data/include/cse/data/CharacterData.h, Cancel::delay).
// Evaluating that at tick time needs the contact tick, which GameState does not
// carry and -- per the long note at the top of this file -- is not going to
// start carrying. So the
// bridge resolves the delay against the source move's frame numbering ONCE, at
// load, and hands the kernel two frame numbers it can compare against
// Fighter::moveFrame with no arithmetic at all. Both bounds are INCLUSIVE:
// unlike a box edge, a frame number is a count of ticks rather than a position
// on a line, there is no half-open convention to preserve, and "the last frame
// the cancel works on" is the sentence a designer writes.
//
// An edge with earliestFrame > latestFrame is INERT rather than malformed. It is
// what an authored delay longer than the whole source move resolves to, and that
// is a real thing in the shipped data: every one of the AOF2 character's 26
// edges is a LINK (press the next button after the move has fully recovered)
// wearing the schema's cancel shape. The kernel simply never takes it, which is
// the correct behaviour, and MatchBuilder counts it rather than pretending the
// character has 26 cancels it can perform.
struct CancelEdge {
    // Kernel move ids, i.e. direct indices into FighterData::moves. An edge
    // naming slot 0 on either end can never fire: slot 0 is idle, a fighter with
    // moveId 0 has no move to cancel out of, and MoveAt refuses to describe it.
    std::uint16_t from;
    std::uint16_t to;

    // Fighter::moveFrame bounds on the SOURCE move, inclusive both ends.
    std::int32_t earliestFrame;
    std::int32_t latestFrame;

    // 1 when the source must have CONNECTED for this edge to be available; 0
    // when it may be taken on a whiff.
    //
    // This is one bit where the schema has four values (Contact: Hit, Block,
    // Whiff, Always), and the collapse is forced rather than chosen: the kernel
    // has no blocking at all -- Fighter::blockstun exists and nothing ever writes
    // it -- so "connected as a hit" and "connected as a block" are the same
    // observation here. Hit and Block both become 1, Whiff and Always both become
    // 0, and MatchBuilder counts the edges that reading moves.
    std::uint8_t onHit;

    // Explicit, for MoveDef::pad_'s reason: the connect handshake hashes these
    // bytes, and an indeterminate byte is a byte two peers can disagree about.
    std::uint8_t pad_[3];
};

// 256 cancel edges per fighter. Chosen against the shipped data rather than
// picked round: Kung Fu Girl authors 134, Kung Fu Man 87, the AOF2 character 26,
// so this is comfortable headroom over the largest real character and still a
// fixed bound, which is what D4 asks of anything the simulation reads. Over the
// cap is a REFUSAL in the bridge, never a truncation, for the reason
// MatchBuilder.h gives about moves: a character missing its last cancels is a
// different character from the one the prover analysed.
inline constexpr std::int32_t kMaxCancelsPerFighter = 256;

// Everything one fighter's simulation reads and never writes.
struct FighterData {
    // The body, authored facing +X. One box for the whole character today;
    // per-frame hurtboxes (a crouch is a different shape) are Phase 3, and they
    // land as a per-move override here, not as a field of GameState.
    Box hurtbox;

    // Number of USED slots in moves[], INCLUDING the reserved idle slot 0.
    // A moveId names a real move if and only if 0 < moveId && moveId < moveCount.
    //
    // Slot 0 is reserved and must stay zeroed. It exists so that Fighter::moveId
    // is a DIRECT index with no arithmetic anywhere -- GameState.h already
    // documents moveId 0 as idle, and a 1-based table would put a "-1" at every
    // lookup site, which is the kind of correction that is right in four places
    // and forgotten in the fifth.
    std::int32_t moveCount;

    MoveDef moves[kMaxMovesPerFighter];

    // Number of USED entries in cancels[]. There is no reserved slot here and no
    // off-by-one to remember: entry 0 is a real edge, because nothing indexes
    // this array out of GameState. Fighter::moveId indexes moves[]; nothing
    // indexes cancels[] except the scan below.
    std::int32_t cancelCount;

    // File order, preserved. The scan takes the FIRST edge it can, so the order
    // is a tie-break rule that two peers must agree on -- and preserving the
    // authored order makes that rule one a designer can see in their own file,
    // rather than one that emerged from a sort nobody wrote down.
    CancelEdge cancels[kMaxCancelsPerFighter];
};

// The read-only data for both sides of a match, indexed by the same slot as
// GameState::p. Passing both halves as one object keeps the tick from having to
// be told which fighter it is looking at data for.
struct MatchData {
    FighterData p[2];
};

// A match with no moves and no bodies: nothing can start, nothing can be live,
// and a degenerate hurtbox overlaps nothing. This is what the two-argument
// Simulate uses, so that a caller who has not loaded a character yet gets
// exactly the pre-hitbox kernel rather than a subtly different one.
inline constexpr MatchData kNoMoves{};

// The data is on the wire's side of the handshake, so its layout has to be as
// disciplined as GameState's.
static_assert(std::is_trivially_copyable_v<MoveDef>, "MatchData is hashed and compared as bytes");
static_assert(std::is_standard_layout_v<MatchData>, "MatchData is hashed and compared as bytes");
static_assert(sizeof(MoveDef) == 40,
              "MoveDef grew, shrank, or acquired implicit padding. The connect "
              "handshake hashes these bytes (ARCHITECTURE.md 4.8), so a padding "
              "hole would make two peers with identical characters disagree.");
static_assert(std::is_trivially_copyable_v<CancelEdge>, "MatchData is hashed and compared as bytes");
static_assert(sizeof(CancelEdge) == 16,
              "CancelEdge grew, shrank, or acquired implicit padding. Same "
              "handshake, same hazard as MoveDef above.");

// --- Reading the state through the data -------------------------------------

// The MoveDef a fighter's moveId names, or null if it names none -- either
// because the fighter is idle (moveId 0) or because the id is outside this
// character's table.
//
// Callers branch on null, which is not the pointer branch §4.3 forbids: that
// rule is about behaviour that depends on an ADDRESS, and null-ness is a value
// every peer computes identically from the same integers.
//
// An id the table does not describe is INERT rather than an error: its frame
// counter advances and it has no boxes. That is deliberately identical to what
// the kernel did before hitboxes existed, so a harness that sets moveId by hand
// (tests/test_determinism_crossplat.cpp does exactly that) keeps behaving as it
// did, and a character file that was loaded with fewer moves than the state
// refers to cannot index off the end of the array.
const MoveDef* MoveAt(const FighterData& data, std::uint16_t moveId);

// Total ticks a move occupies.
std::int32_t MoveDuration(const MoveDef& m);

// The fighter's hitbox in stage coordinates, if one is live this tick.
// Returns false when the fighter is idle, when the id is undescribed, or when
// the current frame is outside [startup, startup + active).
bool ActiveHitbox(const FighterData& data, const Fighter& f, Box& out);

// The fighter's body in stage coordinates. Always exists; a character whose
// hurtbox is degenerate simply cannot be hit, which is what kNoMoves relies on.
Box Hurtbox(const FighterData& data, const Fighter& f);

// --- Cancels, read out of the state -----------------------------------------

// Whether the fighter's CURRENT move satisfies this edge's window and contact
// requirement, ignoring buttons entirely.
//
// Separated from the button test so that the two halves of "can I cancel" can be
// asserted apart. A test that only ever asks the combined question cannot tell a
// window that is off by one from a binding that is missing, and those are the
// two ways this feature fails silently.
//
// "Has connected" is Fighter::alreadyHitBits being nonzero. That field is
// cleared whenever a move starts or ends (StepAttack) and set by ResolveHits on
// the ATTACKER when its box overlapped a body, so a nonzero value means exactly
// "the attack currently in progress has landed on somebody". It is the multi-hit
// guard doing a second job, and the two uses do not conflict: both want the same
// sentence to be true.
bool CancelIsOpen(const Fighter& f, const CancelEdge& edge);

// The edge this fighter may take THIS TICK given these inputs, or null.
//
// Scans FighterData::cancels in index order and returns the first edge that
// matches. First-wins over a fixed dense array is the same tie-break rule
// StepAttack already uses for buttons, and it is chosen for the same reason: two
// peers holding the same bytes must pick the same edge, and "the first one in
// the file" is a rule that survives being written down.
//
// Returns null when the fighter is idle, when its moveId names no move, when no
// edge's window is open, or when the target of every open edge is a move whose
// buttons are not held. A target with no button at all (MoveDef::button == 0) is
// unreachable by definition and is skipped rather than taken for free.
const CancelEdge* FindCancel(const FighterData& data, const Fighter& f, Input in);

// --- The tick's two combat steps --------------------------------------------

// Advance one fighter's attack: end a move that has run out, CANCEL a move that
// is still running into a follow-up the fighter is asking for, or start one if
// the fighter is idle and holding a move's buttons. Called from stepFighter,
// once per fighter, in slot order.
//
// THE ORDERING FACT A CANCEL RULE HAS TO LIVE WITH. This runs inside the
// per-fighter step, which is BEFORE ResolveHits -- so a hit landing on tick N is
// not visible to a cancel test until tick N+1. The fastest cancel the kernel can
// express is therefore one tick after contact, never zero, and an edge whose
// authored delay is 0 fires on the tick after the hit. That is a property of the
// tick's shape, not a fudge: on the tick the hit resolves, the hit has not
// happened yet as far as the first half of the tick is concerned, and moving the
// cancel test after ResolveHits would instead let a fighter cancel a move on the
// same tick it started, which is worse.
void StepAttack(Fighter& f, const FighterData& data, Input in, bool actionable);

// Test both fighters' live hitboxes against the other's body and apply at most
// one hit each way. Called once per tick, after both fighters have moved and
// after facing has been resolved -- boxes are mirrored by facing, so facing must
// be settled before any box is built.
void ResolveHits(GameState& state, const MatchData& data);

} // namespace cse::kernel
