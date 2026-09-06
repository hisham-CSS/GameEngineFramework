// Two peers swap one blob each over a lossy transport (ROADMAP M2.3).
//
// After a desync the match is over and the session is gone; what is left to
// do is exchange the two states at the reported frame so each side can name
// the first field they disagree about (cse::game::FirstDivergence). This is
// that exchange, in bytes: my blob under a tag (the frame), theirs back. It is
// chunked to fit a datagram, sent whole on every Pump() -- the transport is
// lossy by contract and nothing acknowledges -- and reassembled from whatever
// arrives, in any order. Complete when every chunk of theirs is here; keep
// pumping a few frames after, for the peer's sake. Anything that is not a
// chunk (the dead session's last packets) is ignored.
#pragma once
#include "cse/net/ISession.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cse::net {

class BlobExchange {
public:
    static constexpr std::uint32_t kChunkBytes = 1000;

    BlobExchange(ITransport& transport, std::string peer, std::uint32_t tag,
                 const std::uint8_t* bytes, std::uint32_t length);

    // Send my chunks, read theirs; true once their blob is complete.
    bool Pump();
    bool Complete() const { return complete_; }
    std::uint32_t                    TheirTag() const { return theirTag_; }
    const std::vector<std::uint8_t>& Theirs() const { return theirs_; }

private:
    ITransport&               transport_;
    std::string               peer_;
    std::uint32_t             tag_;
    std::vector<std::uint8_t> mine_;
    std::vector<std::uint8_t> theirs_;
    std::vector<bool>         have_;
    std::uint32_t             theirTag_   = 0;
    std::uint32_t             theirTotal_ = 0;
    bool                      complete_   = false;
    int                       sendsLeft_  = 64;   // pumps that still send after completion
};

} // namespace cse::net
