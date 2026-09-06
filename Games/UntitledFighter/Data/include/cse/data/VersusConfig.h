// The Versus lobby's three numbers (ROADMAP M2.5, ADR-022 D2): which slot
// this copy plays, the UDP port it listens on, and the peer it offers to. They
// come from the authored file UntitledFighter/versus.json and are overridden
// by the command line in online_peer's vocabulary (--slot N --port P --peer
// ip:port), so the two-process flow tests/two_peers.py drives and the shipped
// Player speak one language.
//
// HERE, IN CseData, for the reason AuthoringTelemetry is: it opens a path by
// name, and this library owns the sandbox every authored read goes through
// and, privately, the JSON dependency. The mode cannot parse JSON (nothing it
// links exposes nlohmann) and must not learn to; the presentation library is
// scoped to "one GameState -> the numbers a scene needs" and a lobby config is
// not that.
//
// WHAT IS NOT VALIDATED HERE, ON PURPOSE. `peer` is a non-empty string and
// nothing more: the transport parses "ip:port" (UdpTransport.h) and ADR-022 D5
// says its own error text reaches the lobby screen. A second parser here would
// be a second opinion about what an address is, and the day they disagreed the
// lobby would refuse a peer the socket would have reached.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cse::data {

// The defaults are the committed sample's slot-0 side, so a file that names
// only `slot` is a complete config and a missing file still describes a
// lobby -- the caller decides whether "missing" is worth a note.
struct VersusConfig {
    int           slot = 0;                    // 0 or 1: the pad slot this copy plays
    std::uint16_t port = 47011;                // the UDP port this copy binds
    std::string   peer = "127.0.0.1:47012";    // "ip:port"; IPv4 literals only (ADR-022 D5)
};

// Parses the file's text. A CLOSED key list -- slot, port, peer -- refused by
// name with the legal names listed, as the character loader and ParseFightLook
// do, because a typo in a three-key file that parsed as "defaults" would send
// a player into a lobby that listens on the wrong port and blames the peer.
// Absent keys keep the defaults. `out` is written ONLY on success: a config
// half-taken from a refused file is a lobby that binds the right port for the
// wrong slot. Errors read "versus: ...".
bool ParseVersusConfig(const std::string& jsonText, VersusConfig& out, std::string& error);

// Opens contentRoot/relPath through PathIsContained (absolute paths, drive/UNC
// roots and `..` refused lexically, before any filesystem access -- the rule
// docs/MAINTENANCE.md admits no exceptions to), caps the size as every
// authored read is capped, and parses. Path and file errors are prefixed with
// relPath in the loader's "<file>: <where>: <what>" shape; parse errors are
// ParseVersusConfig's.
bool LoadVersusConfig(const std::string& contentRoot, const std::string& relPath,
                      VersusConfig& out, std::string& error);

// Applies `--slot N`, `--port P` and `--peer ip:port` from a command line to
// `io`. The whole argv may be handed over (Application::commandLine() puts the
// program at [0] and the Player's scene wherever it was typed): a token that
// does not begin with `--` is skipped, and a `--key` ALWAYS takes the token
// after it as its value, known or not -- the Player's own grammar for finding
// its scene, so the two readers cannot disagree about which token is which.
// A value that is not a whole integer in range, or a flag with no value, is an
// error naming the flag, and `io` is left exactly as it was: std::atoi would
// have read `--slot one` as slot 0 and `--slot 1x` as slot 1 without a word,
// which is a lobby connected the wrong way round. False with `error` on a bad
// flag; true (and `error` empty) otherwise, including for no arguments at all.
bool ApplyVersusOverrides(const std::vector<std::string>& args, VersusConfig& io,
                          std::string& error);

} // namespace cse::data
