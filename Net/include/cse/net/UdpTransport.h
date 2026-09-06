// The transport of record: UDP over the operating system's sockets (ROADMAP M2.1).
//
// One non-blocking IPv4 socket bound to a port; Send() is one datagram to
// "ip:port", Receive() drains whatever arrived. Unreliable and unordered, which
// is the ITransport contract and what a rollback session wants -- GekkoNet
// carries its own redundancy, and a transport that retransmitted would only
// add latency to inputs the session has already given up on. Winsock on
// Windows, BSD sockets elsewhere; no library beyond the platform's, which is
// why this is the default rather than GekkoNet's own asio adapter -- our
// third-party build compiles GekkoNet without asio (ThirdParty/CMakeLists.txt),
// and ADR-021 says why that stays so.
//
// The address a received packet is reported from is the sender's "ip:port" as
// text, which is what GekkoNet compares against the address a remote actor was
// added with; spell the peers the way they will bind ("127.0.0.1:47012", not
// "localhost:47012").
#pragma once
#include "cse/net/ISession.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cse::net {

class UdpTransport final : public ITransport {
public:
    // Binds the port on every IPv4 interface. Null with `error` filled when the
    // socket cannot be opened or bound (port in use, no permission).
    static std::unique_ptr<UdpTransport> Bind(std::uint16_t port, std::string* error);
    ~UdpTransport() override;

    UdpTransport(const UdpTransport&)            = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    void Send(const std::string& to, const std::uint8_t* bytes, std::uint32_t length) override;
    std::vector<TransportPacket> Receive() override;

    std::uint16_t Port() const { return port_; }
    std::uint64_t PacketsSent() const { return sent_; }
    std::uint64_t PacketsReceived() const { return received_; }

private:
    UdpTransport() = default;
    std::uintptr_t socket_   = 0;    // SOCKET on Windows, int elsewhere; opaque here
    std::uint16_t  port_     = 0;
    std::uint64_t  sent_     = 0;
    std::uint64_t  received_ = 0;
};

} // namespace cse::net
