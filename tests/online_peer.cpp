// One peer of a two-process match over the built-in UDP adapter (ROADMAP M2.1).
//
// tests/two_peers.py launches two of these on 127.0.0.1, one per slot, and
// compares what they print. Each runs the kernel through an ONLINE session
// with the other as a remote player, on the same scripted inputs test_session
// uses, until `--ticks` confirmed frames have been simulated, then prints the
// checksum at that frame. Two processes that agree, and neither reporting a
// desync, is the property the spike exists to measure: the transport carries
// a real match. Pacing follows FramesAhead(): a peer that is ahead sleeps a
// frame, which is the whole of the host's obligation to the session.
//
//     online_peer --slot 0 --port 47011 --peer 127.0.0.1:47012 --ticks 300
//
// Exit 0 with `checksum <hex> frame <n>` on stdout; nonzero and a reason on
// stderr otherwise (a desync, a peer that never connected, a timeout).
#include "cse/game/Replay.h"
#include "cse/kernel/Simulate.h"
#include "cse/net/Handshake.h"
#include "cse/net/ISession.h"
#include "cse/net/UdpTransport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace cse::kernel;
using namespace cse::net;

namespace {

struct WireInput { std::uint16_t bits; };

InputPair scripted(int t) {
    InputPair in{};
    if (t % 7 == 0)  in.p[0].bits |= kInputRight;
    if (t % 11 == 0) in.p[0].bits |= kInputUp;
    if (t % 5 == 0)  in.p[1].bits |= kInputLeft;
    if (t % 13 == 0) in.p[1].bits |= kInputLP;
    return in;
}

int fail(const char* why) {
    std::fprintf(stderr, "online_peer: %s\n", why);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    int slot = 0, ticks = 300;
    unsigned short port = 0;
    std::string peer;
    bool contentMismatch = false;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (k == "--content-mismatch") { contentMismatch = true; continue; }
        if (i + 1 >= argc) return fail("an argument without its value");
        if (k == "--slot")       slot  = std::atoi(argv[++i]);
        else if (k == "--port")  port  = static_cast<unsigned short>(std::atoi(argv[++i]));
        else if (k == "--peer")  peer  = argv[++i];
        else if (k == "--ticks") ticks = std::atoi(argv[++i]);
        else return fail("unknown argument");
    }
    if (port == 0 || peer.empty() || (slot != 0 && slot != 1) || ticks < 16) return fail("usage: --slot 0|1 --port P --peer ip:port --ticks N");

    SessionConfig cfg{};
    cfg.playerCount         = 2;
    cfg.inputBytesPerPlayer = sizeof(WireInput);
    cfg.stateBytes          = sizeof(GameState);
    cfg.predictionWindow    = 8;
    cfg.desyncDetection     = true;
    cfg.desyncCheckInterval = 8;
    cfg.localDelay          = 2;
    cfg.peerAddresses       = { slot == 0 ? std::string() : peer, slot == 1 ? std::string() : peer };

    std::string error;
    std::unique_ptr<UdpTransport> transport = UdpTransport::Bind(port, &error);
    if (!transport) { std::fprintf(stderr, "online_peer: %s\n", error.c_str()); return 2; }

    // THE HANDSHAKE FIRST (ROADMAP M2.2). This harness runs the data-less
    // kernel, so the loaded arrays are kNoMoves; HashMatchData over them is
    // still the hash the handshake carries. --content-mismatch offers a wrong
    // one, so the driver can see A5 fire over a real wire.
    HandshakeOffer offer;
    offer.contentHash = cse::game::HashMatchData(cse::kernel::kNoMoves) + (contentMismatch ? 1u : 0u);
    offer.stateBytes  = cfg.stateBytes;
    offer.inputBytes  = cfg.inputBytesPerPlayer;
    offer.seed        = 0xC0FFEEu;
    offer.playerCount = cfg.playerCount;
    offer.slot        = static_cast<std::uint8_t>(slot);
    Handshake handshake(*transport, peer, offer);
    const auto handshakeStart = std::chrono::steady_clock::now();
    while (handshake.Pump().state == HandshakeState::Waiting) {
        if (std::chrono::steady_clock::now() - handshakeStart > std::chrono::seconds(30)) return fail("the peer never offered");
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (handshake.Result().state == HandshakeState::Refused) {
        std::fprintf(stderr, "online_peer: refused: %s\n", handshake.Result().reason.c_str());
        return 4;
    }

    // The session on the handshake: it peels the peer's grace-period offers off
    // and hands the session everything else.
    ISession* session = CreateGekkoOnlineSession(cfg, &handshake);
    if (session == nullptr) return fail("the online session could not be created");

    GameState live{};
    ResetMatch(live, 0xC0FFEEu);
    std::map<int, std::uint32_t> checksumAfterFrame;   // frame f simulated -> checksum of the state after it
    int confirmedFrames = 0;
    int iteration = 0;
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60)) {
            DestroySession(session);
            return fail("timed out before the peers finished");
        }
        handshake.Pump();   // the grace resends, so a peer whose last offer was lost still hears us
        WireInput in{ scripted(iteration).p[slot].bits };
        session->AddLocalInput(slot, &in);
        int n = 0;
        const SessionEvent* ev = session->Update(&n);
        for (int i = 0; i < n; ++i) {
            switch (ev[i].type) {
            case SessionEventType::Save:
                *ev[i].saveLength   = sizeof(GameState);
                *ev[i].saveChecksum = Checksum(live);
                std::memcpy(ev[i].saveBuffer, &live, sizeof(GameState));
                break;
            case SessionEventType::Load:
                std::memcpy(&live, ev[i].loadBuffer, sizeof(GameState));
                break;
            case SessionEventType::Advance: {
                const auto* wire = reinterpret_cast<const WireInput*>(ev[i].inputs);
                InputPair pair{};
                pair.p[0].bits = wire[0].bits;
                pair.p[1].bits = wire[1].bits;
                Simulate(live, pair);
                checksumAfterFrame[ev[i].frame] = Checksum(live);
                if (!ev[i].rollingBack && !ev[i].runningAhead) confirmedFrames = ev[i].frame + 1;
                break;
            }
            }
        }
        DesyncReport report{};
        if (session->PollDesync(&report)) {
            std::fprintf(stderr, "online_peer: desync at frame %d (local %08x, remote %08x)\n",
                         report.frame, report.localChecksum, report.remoteChecksum);
            DestroySession(session);
            return 3;
        }
        if (confirmedFrames >= ticks) break;
        ++iteration;
        // Pace: a frame is 1/60 s; a peer running ahead of the other yields one more.
        std::this_thread::sleep_for(std::chrono::milliseconds(session->FramesAhead() > 0 ? 32 : 16));
    }
    // The checksum after the last frame both peers are asked for, minus the
    // prediction window so it is a frame neither can still be revising.
    const int frame = ticks - 1 - cfg.predictionWindow;
    const auto it = checksumAfterFrame.find(frame);
    const int connected = session->ConnectedPeers();
    DestroySession(session);
    if (it == checksumAfterFrame.end()) return fail("the reported frame was never simulated");
    std::printf("checksum %08x frame %d connected %d\n", it->second, frame, connected);
    return 0;
}
