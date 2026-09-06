// The connect handshake (ROADMAP M2.2; DETERMINISM.md A4, A5; ADR-002 CHOICE C).
//
// Before a session starts, each peer offers what it loaded -- the hash of the
// LOADED POD ARRAYS (HashMatchData over the MatchData, never the source text:
// canonicalising text is where float-repr and key-order bugs live), the state
// and input sizes the session will be configured with, the seed, the player
// count and the slot it means to play -- and the two offers are compared
// field by field in one fixed order. Every field must agree, the slots must
// differ, and the FIRST disagreement is the reason: "content hash: mine
// 0x.... theirs 0x....", a lobby error, never "desync at tick 3".
//
// This library carries the hash and never computes it: it knows no MatchData,
// exactly as ISession knows no GameState. CseGame's HashMatchData is the one
// function that answers "the same data?" for a replay and for a peer, and the
// caller puts its result in the offer.
//
// THE HANDSHAKE IS ITSELF A TRANSPORT, wrapping the real one, and the session
// that follows is created ON IT. Two facts force that shape. The transport is
// lossy by contract, so the offer is sent on EVERY Pump() until the peer's
// arrives and for a grace period after -- the last packet of any two-way
// exchange can be lost, and a peer that agreed and fell silent would leave
// the other waiting forever. And a peer that agreed first starts its session
// at once, so its session packets reach us while we may still be waiting: a
// handshake that drained the transport itself would swallow them (the first
// cut did, and no session ever connected). So Receive() here peels offers off
// and hands everything else through, Pump() queues what it reads for the
// session, and the two can interleave in any order without a packet lost.
#pragma once
#include "cse/net/ISession.h"

#include <cstdint>
#include <deque>
#include <string>

namespace cse::net {

// The layout on the wire; bump when it changes, and a peer on another version
// is refused by name rather than misread.
inline constexpr std::uint32_t kHandshakeProtocol = 1;

struct HandshakeOffer {
    std::uint32_t protocol    = kHandshakeProtocol;
    std::uint32_t contentHash = 0;   // HashMatchData over the loaded MatchData (A4)
    std::uint32_t stateBytes  = 0;   // SessionConfig::stateBytes
    std::uint32_t inputBytes  = 0;   // SessionConfig::inputBytesPerPlayer
    std::uint32_t seed        = 0;   // the seed both peers ResetMatch with
    std::uint8_t  playerCount = 0;
    std::uint8_t  slot        = 0;   // the slot the sender plays; the two must differ
};

enum class HandshakeState { Waiting, Agreed, Refused };

struct HandshakeResult {
    HandshakeState state = HandshakeState::Waiting;
    // Refused: the first field that disagrees, with both values (A5). Empty otherwise.
    std::string    reason;
    // The peer's offer once it has arrived (Agreed or Refused).
    HandshakeOffer theirs{};
};

class Handshake final : public ITransport {
public:
    // `transport` is the real one, which must outlive this; `peer` is the
    // address the session's remote slot will name. Create the session on THIS
    // object once Agreed.
    Handshake(ITransport& transport, std::string peer, const HandshakeOffer& mine);

    // Send my offer if still due, read what arrived (offers compared, the rest
    // queued for the session), report. Call once per frame until Agreed or
    // Refused, then keep calling it beside the session for the grace period.
    const HandshakeResult& Pump();
    const HandshakeResult& Result() const { return result_; }

    // ITransport, for the session: sends pass through; receives are every
    // packet that was not an offer, queued or fresh, in arrival order.
    void Send(const std::string& to, const std::uint8_t* bytes, std::uint32_t length) override;
    std::vector<TransportPacket> Receive() override;

    // The comparison alone, for a caller that exchanged offers another way.
    static HandshakeResult Compare(const HandshakeOffer& mine, const HandshakeOffer& theirs);

    // 28 bytes: a 4-byte tag then the fields, little-endian, fixed order.
    static constexpr std::uint32_t kWireBytes = 28;
    static void Encode(const HandshakeOffer& offer, std::uint8_t out[kWireBytes]);
    static bool Decode(const std::uint8_t* bytes, std::uint32_t length, HandshakeOffer* out);

    std::uint32_t OffersSent() const { return sent_; }
    // Packets that were not offers and went (or wait) to the session.
    std::uint32_t PacketsQueued() const { return queued_; }

private:
    void poll_();

    ITransport&                 transport_;
    std::string                 peer_;
    HandshakeOffer              mine_;
    HandshakeResult             result_;
    std::deque<TransportPacket> pending_;
    std::uint32_t               sent_         = 0;
    std::uint32_t               queued_       = 0;
    int                         graceResends_ = 16;   // offers still sent after agreement
};

} // namespace cse::net
