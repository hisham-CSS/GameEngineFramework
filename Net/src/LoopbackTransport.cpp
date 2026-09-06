// The in-process network; see the header. No GekkoNet here either.
#include "cse/net/LoopbackTransport.h"

namespace cse::net {

ITransport& LoopbackNetwork::Endpoint(const std::string& name) {
    auto it = endpoints_.find(name);
    if (it == endpoints_.end())
        it = endpoints_.emplace(name, std::make_unique<Endpoint_>(*this, name)).first;
    return *it->second;
}

void LoopbackNetwork::Endpoint_::Send(const std::string& to, const std::uint8_t* bytes, std::uint32_t length) {
    ++net_.sent_;
    if (net_.dropEvery_ > 0 && net_.sent_ % static_cast<std::uint64_t>(net_.dropEvery_) == 0) {
        ++net_.dropped_;
        return;
    }
    // A packet to a peer nobody has claimed yet is delivered when it does:
    // the endpoint is created on first mention, from either side.
    Endpoint_& dest = static_cast<Endpoint_&>(net_.Endpoint(to));
    InFlight f;
    f.deliverAt    = net_.now_ + net_.latency_;
    f.packet.from  = name_;
    f.packet.bytes.assign(bytes, bytes + length);
    dest.inbox_.push_back(std::move(f));
}

std::vector<TransportPacket> LoopbackNetwork::Endpoint_::Receive() {
    std::vector<TransportPacket> out;
    // In-order delivery from a queue whose deliverAt only grows with the
    // send order under a fixed latency; a latency change mid-run may let a
    // later packet overtake an earlier one, which is what a network does.
    std::deque<InFlight> keep;
    for (auto& f : inbox_) {
        if (f.deliverAt <= net_.now_) out.push_back(std::move(f.packet));
        else keep.push_back(std::move(f));
    }
    inbox_.swap(keep);
    return out;
}

} // namespace cse::net
