// The connect handshake; see the header. No GekkoNet, no MatchData.
#include "cse/net/Handshake.h"

#include <cstdio>
#include <cstring>

namespace cse::net {
namespace {

constexpr std::uint8_t kTag[4] = { 'C', 'S', 'E', 'H' };

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}
std::uint32_t get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08x", v);
    return b;
}
std::string dec(std::uint32_t v) { return std::to_string(v); }

} // namespace

void Handshake::Encode(const HandshakeOffer& o, std::uint8_t out[kWireBytes]) {
    std::memcpy(out, kTag, 4);
    put32(out + 4,  o.protocol);
    put32(out + 8,  o.contentHash);
    put32(out + 12, o.stateBytes);
    put32(out + 16, o.inputBytes);
    put32(out + 20, o.seed);
    out[24] = o.playerCount;
    out[25] = o.slot;
    out[26] = 0;
    out[27] = 0;
}

bool Handshake::Decode(const std::uint8_t* bytes, std::uint32_t length, HandshakeOffer* out) {
    if (length != kWireBytes || std::memcmp(bytes, kTag, 4) != 0) return false;
    out->protocol    = get32(bytes + 4);
    out->contentHash = get32(bytes + 8);
    out->stateBytes  = get32(bytes + 12);
    out->inputBytes  = get32(bytes + 16);
    out->seed        = get32(bytes + 20);
    out->playerCount = bytes[24];
    out->slot        = bytes[25];
    return true;
}

HandshakeResult Handshake::Compare(const HandshakeOffer& mine, const HandshakeOffer& theirs) {
    HandshakeResult r;
    r.theirs = theirs;
    // ONE ORDER, the cheapest disagreement first: a peer on another protocol
    // cannot be read further; sizes before the content, so a mismatched build
    // is named as a build and not as "different content"; the hash last,
    // because it is the one a player can act on (a different character file);
    // the slots after everything else, because two peers who both claim slot 0
    // agree about the match and disagree only about who sits where.
    struct Field { const char* name; std::uint32_t mine, theirs; bool asHex; };
    const Field fields[] = {
        { "protocol",     mine.protocol,    theirs.protocol,    false },
        { "player count", mine.playerCount, theirs.playerCount, false },
        { "state bytes",  mine.stateBytes,  theirs.stateBytes,  false },
        { "input bytes",  mine.inputBytes,  theirs.inputBytes,  false },
        { "seed",         mine.seed,        theirs.seed,        true  },
        { "content hash", mine.contentHash, theirs.contentHash, true  },
    };
    for (const Field& f : fields) {
        if (f.mine != f.theirs) {
            r.state  = HandshakeState::Refused;
            r.reason = std::string(f.name) + ": mine " + (f.asHex ? hex(f.mine) : dec(f.mine)) +
                       ", theirs " + (f.asHex ? hex(f.theirs) : dec(f.theirs));
            return r;
        }
    }
    if (mine.slot == theirs.slot) {
        r.state  = HandshakeState::Refused;
        r.reason = "slot: both peers claim slot " + dec(mine.slot);
        return r;
    }
    if (mine.slot >= mine.playerCount || theirs.slot >= theirs.playerCount) {
        r.state  = HandshakeState::Refused;
        r.reason = "slot: " + dec(mine.slot) + " and " + dec(theirs.slot) + " against " + dec(mine.playerCount) + " players";
        return r;
    }
    r.state = HandshakeState::Agreed;
    return r;
}

Handshake::Handshake(ITransport& transport, std::string peer, const HandshakeOffer& mine)
    : transport_(transport), peer_(std::move(peer)), mine_(mine) {}

void Handshake::poll_() {
    for (TransportPacket& p : transport_.Receive()) {
        HandshakeOffer theirs{};
        if (p.from == peer_ && Decode(p.bytes.data(), static_cast<std::uint32_t>(p.bytes.size()), &theirs)) {
            if (result_.state == HandshakeState::Waiting) result_ = Compare(mine_, theirs);
            continue;   // an offer is ours to consume, first or the peer's grace resends alike
        }
        pending_.push_back(std::move(p));   // the session's, or a stranger's the session will drop
        ++queued_;
    }
}

const HandshakeResult& Handshake::Pump() {
    // Send while waiting, and for the grace period after agreement; a refusal
    // sends nothing more -- the peer reaches the same verdict from our offer.
    const bool due = result_.state == HandshakeState::Waiting ||
                     (result_.state == HandshakeState::Agreed && graceResends_ > 0);
    if (due) {
        std::uint8_t wire[kWireBytes];
        Encode(mine_, wire);
        transport_.Send(peer_, wire, kWireBytes);
        ++sent_;
        if (result_.state == HandshakeState::Agreed) --graceResends_;
    }
    poll_();
    return result_;
}

void Handshake::Send(const std::string& to, const std::uint8_t* bytes, std::uint32_t length) {
    transport_.Send(to, bytes, length);
}

std::vector<TransportPacket> Handshake::Receive() {
    poll_();
    std::vector<TransportPacket> out(std::make_move_iterator(pending_.begin()),
                                     std::make_move_iterator(pending_.end()));
    pending_.clear();
    return out;
}

} // namespace cse::net
