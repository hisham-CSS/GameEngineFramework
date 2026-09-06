// An in-process network for sessions under test (ROADMAP M2.1).
//
// Two or more online sessions in one process, each holding an endpoint of the
// same LoopbackNetwork, exchange packets through it exactly as they would
// through UDP -- unreliable and unordered by contract -- except that here the
// test decides what the network does: how many frames a packet takes to
// arrive (latency, which forces the sessions to predict and roll back) and
// which packets vanish (loss, which the session's own input redundancy must
// survive). It is DETERMINISTIC: the same sends in the same order under the
// same settings deliver the same packets on the same Tick(), so a test that
// passes passes every time, and CI needs no socket. Addresses are the
// endpoint names.
#pragma once
#include "cse/net/ISession.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cse::net {

class LoopbackNetwork {
public:
    LoopbackNetwork() = default;
    LoopbackNetwork(const LoopbackNetwork&)            = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

    // The transport for a peer called `name`; created on first use and owned
    // by the network, which must outlive every session using it.
    ITransport& Endpoint(const std::string& name);

    // A packet sent now is receivable after this many Tick() calls (0: on the
    // very next Receive()). Applies to packets sent after the call.
    void SetLatencyFrames(int frames) { latency_ = frames < 0 ? 0 : frames; }
    // Every `n`-th packet sent, counted over the whole network, is dropped;
    // 0 drops nothing.
    void SetDropEvery(int n) { dropEvery_ = n < 0 ? 0 : n; }

    // Advance the network's clock by one frame; call once per loop iteration.
    void Tick() { ++now_; }

    std::uint64_t PacketsSent() const { return sent_; }
    std::uint64_t PacketsDropped() const { return dropped_; }

private:
    struct InFlight {
        std::int64_t     deliverAt;
        TransportPacket  packet;
    };
    struct Endpoint_ final : ITransport {
        Endpoint_(LoopbackNetwork& net, std::string name) : net_(net), name_(std::move(name)) {}
        void Send(const std::string& to, const std::uint8_t* bytes, std::uint32_t length) override;
        std::vector<TransportPacket> Receive() override;
        LoopbackNetwork&     net_;
        std::string          name_;
        std::deque<InFlight> inbox_;
    };

    std::map<std::string, std::unique_ptr<Endpoint_>> endpoints_;
    std::int64_t  now_       = 0;
    int           latency_   = 0;
    int           dropEvery_ = 0;
    std::uint64_t sent_      = 0;
    std::uint64_t dropped_   = 0;
};

} // namespace cse::net
