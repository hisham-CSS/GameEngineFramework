// Two peers swap one blob each; see the header. No GekkoNet.
#include "cse/net/BlobExchange.h"

#include <algorithm>
#include <cstring>

namespace cse::net {
namespace {

constexpr std::uint8_t kTag[4] = { 'C', 'S', 'E', 'B' };
constexpr std::uint32_t kHeader = 16;   // tag(4) blobTag(4) total(4) index(2) count(2)

void put32(std::uint8_t* p, std::uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i)); }
void put16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); }
std::uint32_t get32(const std::uint8_t* p) { std::uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return v; }
std::uint16_t get16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

} // namespace

BlobExchange::BlobExchange(ITransport& transport, std::string peer, std::uint32_t tag,
                           const std::uint8_t* bytes, std::uint32_t length)
    : transport_(transport), peer_(std::move(peer)), tag_(tag), mine_(bytes, bytes + length) {}

bool BlobExchange::Pump() {
    if (sendsLeft_ > 0) {
        const std::uint32_t count = static_cast<std::uint32_t>((mine_.size() + kChunkBytes - 1) / kChunkBytes);
        std::uint8_t wire[kHeader + kChunkBytes];
        for (std::uint32_t i = 0; i < std::max<std::uint32_t>(count, 1u); ++i) {
            const std::uint32_t from = i * kChunkBytes;
            const std::uint32_t n    = static_cast<std::uint32_t>(std::min<std::size_t>(kChunkBytes, mine_.size() - from));
            std::memcpy(wire, kTag, 4);
            put32(wire + 4, tag_);
            put32(wire + 8, static_cast<std::uint32_t>(mine_.size()));
            put16(wire + 12, static_cast<std::uint16_t>(i));
            put16(wire + 14, static_cast<std::uint16_t>(std::max<std::uint32_t>(count, 1u)));
            if (n > 0) std::memcpy(wire + kHeader, mine_.data() + from, n);
            transport_.Send(peer_, wire, kHeader + n);
        }
        if (complete_) --sendsLeft_;
    }
    for (const TransportPacket& p : transport_.Receive()) {
        if (p.from != peer_ || p.bytes.size() < kHeader || std::memcmp(p.bytes.data(), kTag, 4) != 0) continue;
        const std::uint32_t blobTag = get32(p.bytes.data() + 4);
        const std::uint32_t total   = get32(p.bytes.data() + 8);
        const std::uint16_t index   = get16(p.bytes.data() + 12);
        const std::uint16_t count   = get16(p.bytes.data() + 14);
        if (count == 0 || index >= count || total > 16u * 1024u * 1024u) continue;
        if (have_.empty()) {
            theirTag_   = blobTag;
            theirTotal_ = total;
            theirs_.assign(total, 0);
            have_.assign(count, false);
        }
        if (blobTag != theirTag_ || total != theirTotal_ || count != have_.size()) continue;   // a different blob: not ours
        const std::uint32_t from = static_cast<std::uint32_t>(index) * kChunkBytes;
        const std::uint32_t n    = static_cast<std::uint32_t>(p.bytes.size() - kHeader);
        if (from + n > total) continue;
        std::memcpy(theirs_.data() + from, p.bytes.data() + kHeader, n);
        have_[index] = true;
        bool all = true;
        for (bool h : have_) all = all && h;
        complete_ = all;
    }
    return complete_;
}

} // namespace cse::net
