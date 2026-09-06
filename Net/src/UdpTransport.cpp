// UDP over the platform's sockets; see the header. No GekkoNet here.
#include "cse/net/UdpTransport.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace cse::net {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalid = INVALID_SOCKET;
int lastError() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK; }
void closeSocket(SocketHandle s) { closesocket(s); }
// Winsock wants WSAStartup once per process; a static counts the transports.
int& winsockUsers() { static int users = 0; return users; }
bool acquireWinsock(std::string* error) {
    if (winsockUsers()++ == 0) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            --winsockUsers();
            if (error) *error = "WSAStartup failed";
            return false;
        }
    }
    return true;
}
void releaseWinsock() { if (--winsockUsers() == 0) WSACleanup(); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalid = -1;
int lastError() { return errno; }
bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
void closeSocket(SocketHandle s) { close(s); }
bool acquireWinsock(std::string*) { return true; }
void releaseWinsock() {}
#endif

// "ip:port" -> sockaddr_in; false when the text is not IPv4 dotted quad + port.
bool parseAddress(const std::string& text, sockaddr_in* out) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) return false;
    const std::string ip = text.substr(0, colon);
    const int port = std::atoi(text.c_str() + colon + 1);
    if (port <= 0 || port > 65535) return false;
    std::memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port   = htons(static_cast<std::uint16_t>(port));
    return inet_pton(AF_INET, ip.c_str(), &out->sin_addr) == 1;
}

std::string formatAddress(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, const_cast<in_addr*>(&a.sin_addr), ip, sizeof(ip));
    char out[64];
    std::snprintf(out, sizeof(out), "%s:%u", ip, static_cast<unsigned>(ntohs(a.sin_port)));
    return out;
}

} // namespace

std::unique_ptr<UdpTransport> UdpTransport::Bind(std::uint16_t port, std::string* error) {
    if (!acquireWinsock(error)) return nullptr;
    const SocketHandle s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalid) {
        if (error) *error = "socket() failed: " + std::to_string(lastError());
        releaseWinsock();
        return nullptr;
    }
    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        if (error) *error = "bind(" + std::to_string(port) + ") failed: " + std::to_string(lastError());
        closeSocket(s);
        releaseWinsock();
        return nullptr;
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    std::unique_ptr<UdpTransport> t(new UdpTransport());
    t->socket_ = static_cast<std::uintptr_t>(s);
    t->port_   = port;
    return t;
}

UdpTransport::~UdpTransport() {
    closeSocket(static_cast<SocketHandle>(socket_));
    releaseWinsock();
}

void UdpTransport::Send(const std::string& to, const std::uint8_t* bytes, std::uint32_t length) {
    sockaddr_in dest{};
    if (!parseAddress(to, &dest)) return;   // an address the session was given and we cannot read: dropped, like any lost datagram
    sendto(static_cast<SocketHandle>(socket_), reinterpret_cast<const char*>(bytes), static_cast<int>(length), 0,
           reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    ++sent_;
}

std::vector<TransportPacket> UdpTransport::Receive() {
    std::vector<TransportPacket> out;
    std::uint8_t buffer[2048];
    for (;;) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = sizeof(from);
#else
        socklen_t fromLen = sizeof(from);
#endif
        const auto n = recvfrom(static_cast<SocketHandle>(socket_), reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n < 0) {
            if (wouldBlock(lastError())) break;   // drained
            break;                                // any other error: nothing more this poll
        }
        TransportPacket p;
        p.from = formatAddress(from);
        p.bytes.assign(buffer, buffer + n);
        out.push_back(std::move(p));
        ++received_;
    }
    return out;
}

} // namespace cse::net
