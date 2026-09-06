// The rollback session seam.
//
// WHAT THIS IS FOR. A rollback session owns the hard, undifferentiating half of
// netcode: input delay, prediction, confirmation, frame-advantage adjustment,
// packet loss, disconnect, desync detection. docs/adr/ADR-002 CHOICE A decided to
// adopt an implementation rather than write one, and docs/adr/ADR-003 established
// GekkoNet clears every gate. This interface is what keeps that decision
// reversible.
//
// THREE RULES, each from a specific finding, each load-bearing:
//
// 1. IT IS AN EVENT PUMP, NOT A "CALL ME TO ROLL BACK" API. The session DRIVES
//    the loop -- it decides when to advance, when to save, when to load, and it
//    tells you. ADR-003 found this shape in GekkoNet and named it as a
//    constraint the plan had missed: if we ever replace the implementation, the
//    replacement must emit the same stream, so the shape belongs in the seam
//    rather than in one implementation of it.
//
// 2. NO GameState, ANYWHERE. Not in a parameter, not in a template argument, not
//    behind a typedef. The session moves BYTES and a length. ARCHITECTURE.md D4
//    makes the snapshot a memcpy of a POD, so bytes is all it needs, and
//    ADR-002 CHOICE A warns that letting the state's TYPE into the session layer
//    is how game #2 ends up forking the netcode. This header does not include
//    anything from a game's kernel -- Games/UntitledFighter/Kernel/ today --
//    and must not start. That is also why this library stayed at the top level
//    when the fighting game moved under Games/: it names no fighter, so game #2
//    inherits it rather than forking it.
//
// 3. NO FLOAT CROSSES THIS BOUNDARY. FramesAhead() returns an int. GekkoNet
//    computes a frame-advantage average in f32 -- ADR-003 traced it and proved
//    it never re-enters the library, so it is a pacing hint for the caller and
//    nothing more. Rounding it here means the property "no float reaches the
//    simulation" is enforced by OUR type rather than by their implementation
//    detail continuing to hold. It is one line and it makes the crossplay
//    guarantee ours to keep.
//
// 4. THE TRANSPORT IS A SEAM TOO (ROADMAP M2.1). A session's packets reach a
//    peer through ITransport: bytes and a length to a string address, and
//    whatever arrived since the last poll. GekkoNet's own adapter type never
//    appears here; GekkoSession.cpp bridges the two. UdpTransport.h -- UDP over
//    the platform's own sockets, no library -- is the transport of record
//    (GekkoNet's asio adapter is not compiled in this tree, on purpose);
//    LoopbackTransport.h is the in-process one every test runs on, with latency
//    and loss you can set. One online session ships per process, four fit for
//    tests, because the bridge behind the seam is process-global; ADR-021
//    records this default, why that is acceptable and what would reverse it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cse::net {

// One packet as the transport hands it back: who sent it (the address the
// session was told to reach that peer at) and the bytes. Owned by the caller
// of Receive() once returned.
struct TransportPacket {
    std::string               from;
    std::vector<std::uint8_t> bytes;
};

// How a session's packets travel. Addresses are opaque strings: "ip:port" for
// the built-in UDP adapter, a name for a loopback network. Unreliable and
// unordered by contract -- the session (GekkoNet) carries its own redundancy
// and never expects a transport to retransmit.
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual void Send(const std::string& to, const std::uint8_t* bytes, std::uint32_t length) = 0;
    // Every packet that arrived since the last call, in arrival order.
    virtual std::vector<TransportPacket> Receive() = 0;
};

// What the session is asking the host to do this pump.
enum class SessionEventType {
    // Run exactly one simulation tick with the supplied inputs.
    Advance,
    // Copy the current state OUT into the supplied buffer, and report its size
    // and a checksum.
    Save,
    // Copy the supplied bytes back IN, replacing the current state.
    Load,
};

struct SessionEvent {
    SessionEventType type;
    std::int32_t     frame;

    // --- Advance ---
    // Inputs for every player this tick, packed back to back, `inputBytes` long
    // in total. The host casts to its own input type; the session never knows
    // what one is.
    const std::uint8_t* inputs      = nullptr;
    std::uint32_t       inputBytes  = 0;
    // True when this tick is being RE-simulated after a correction. The host
    // must suppress anything non-deterministic or player-visible during these:
    // sound, particles, screen shake, haptics. Getting this wrong is the classic
    // "the same hit sound plays eight times" rollback bug.
    bool                rollingBack = false;
    // True when this tick is speculative runahead rather than a confirmed
    // advance. Same suppression rules apply.
    bool                runningAhead = false;

    // --- Save ---
    // Write up to `saveCapacity` bytes into `saveBuffer`, then set *saveLength
    // and *saveChecksum. The buffer belongs to the session; do not free it.
    std::uint8_t*  saveBuffer   = nullptr;
    std::uint32_t  saveCapacity = 0;
    std::uint32_t* saveLength   = nullptr;
    std::uint32_t* saveChecksum = nullptr;

    // --- Load ---
    const std::uint8_t* loadBuffer = nullptr;
    std::uint32_t       loadBytes  = 0;
};

// A desync, reported rather than corrected.
//
// ADR-002 CHOICE C: on mismatch the match STOPS and names the frame. It does not
// silently resync -- in 2-player P2P there is no authority to resync from, and a
// silently corrected position is worse than a stop because the player cannot
// tell it from a lost interaction.
struct DesyncReport {
    std::int32_t  frame          = 0;
    std::uint32_t localChecksum  = 0;
    std::uint32_t remoteChecksum = 0;
    std::int32_t  remotePlayer   = 0;
};

struct SessionConfig {
    std::uint8_t  playerCount           = 2;
    std::uint32_t inputBytesPerPlayer   = 0;
    std::uint32_t stateBytes            = 0;
    // How many ticks of prediction before the session stalls rather than
    // guessing further. ARCHITECTURE.md D4 budgets 8.
    std::uint8_t  predictionWindow      = 8;
    bool          desyncDetection       = true;
    // Ticks between checksum exchanges. ADR-002 CHOICE C says every 8.
    std::uint32_t desyncCheckInterval   = 8;
    // --- Online (ROADMAP M2.1) -------------------------------------------
    // One entry per player slot, in slot order; EMPTY means every player is
    // local, which is what the local and stress sessions are. A local slot's
    // entry is the empty string; a remote slot's is the address the transport
    // routes by. Handles equal slots because actors are added in slot order.
    std::vector<std::string> peerAddresses;
    // Frames a local input is held before the session uses it: lag traded
    // for fewer mispredictions (GekkoNet's local delay). The kernel never sees
    // it -- inputs arrive per frame as always, later.
    std::uint8_t  localDelay             = 2;
};

class ISession {
public:
    virtual ~ISession() = default;

    ISession(const ISession&)            = delete;
    ISession& operator=(const ISession&) = delete;

    // Hand this tick's local input for one player. `input` points at
    // `inputBytesPerPlayer` bytes.
    virtual void AddLocalInput(int player, const void* input) = 0;

    // Pump. Returns a pointer to `count` events, owned by the session and valid
    // until the next call. Handle them IN ORDER -- a Load followed by Advances
    // is a rollback, and reordering them silently corrupts the state.
    virtual const SessionEvent* Update(int* count) = 0;

    // How many frames ahead of the remote peer this client is running, ROUNDED
    // TO AN INTEGER at this boundary on purpose (rule 3 above). Positive means
    // ahead and the host should slow down slightly. A host that ignores this
    // still works; it just drifts into deeper rollbacks.
    virtual int FramesAhead() const = 0;

    // True when a desync has been detected, with the report filled in. Once this
    // returns true the match is over -- see DesyncReport.
    virtual bool PollDesync(DesyncReport* out) = 0;

    // Remote players currently connected (ROADMAP M2.1). Zero for a local or
    // stress session, and for an online one until the peers have found each
    // other -- Advance events do not start before they have.
    virtual int ConnectedPeers() const = 0;

protected:
    ISession() = default;
};

// The only implementation today. Returns null if the session cannot be created.
// Declared as a factory returning the INTERFACE so that no consumer needs
// gekkonet.h -- it is a PRIVATE dependency of this library, and the seam is
// worthless if it leaks.
ISession* CreateGekkoLocalSession(const SessionConfig& cfg);

// Same, but the session rolls back continuously to hunt state divergence. This
// is how you test a simulation's determinism without a network: ADR-003 used it
// to re-simulate 1617 ticks and compare byte-for-byte against a straight run.
ISession* CreateGekkoStressSession(const SessionConfig& cfg);

// An online session (ROADMAP M2.1): cfg.peerAddresses names every slot, local
// (empty) or remote (an address the transport routes by). Packets travel
// through `transport`, which is required and must outlive the session --
// UdpTransport.h is the one of record, LoopbackTransport.h the tests'. Null if
// the config is incomplete, if no slot is local, if no transport is given, or
// if the process already holds as many online sessions as the bridge has slots
// (ADR-021: one is the shipped case; four is the tests' ceiling).
ISession* CreateGekkoOnlineSession(const SessionConfig& cfg, ITransport* transport);

void DestroySession(ISession* session);

} // namespace cse::net
