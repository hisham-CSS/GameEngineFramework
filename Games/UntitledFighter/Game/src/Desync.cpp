// A desync, named; see the header.
#include "cse/game/Desync.h"

#include "cse/kernel/StateReflection.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace cse::game {
namespace {

using cse::kernel::FieldInfo;
using cse::kernel::FieldPath;
using cse::kernel::FieldType;
using cse::kernel::GameState;

std::int64_t readValue(const GameState& s, std::uint32_t offset, std::uint32_t width, FieldType type) {
    const auto* b = reinterpret_cast<const unsigned char*>(&s) + offset;
    std::uint64_t u = 0;
    for (std::uint32_t i = 0; i < width && i < 8; ++i) u |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    switch (type) {
    case FieldType::I16: return static_cast<std::int16_t>(static_cast<std::uint16_t>(u));
    case FieldType::I32: return static_cast<std::int32_t>(static_cast<std::uint32_t>(u));
    default:             return static_cast<std::int64_t>(u);
    }
}

std::string spell(const FieldPath& p) {
    std::string s = p.top->name;
    if (p.top->count > 1) s += "[" + std::to_string(p.topIndex) + "]";
    if (p.member != nullptr) {
        s += ".";
        s += p.member->name;
        if (p.member->count > 1) s += "[" + std::to_string(p.memberIndex) + "]";
    }
    return s;
}

std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08x", v);
    return b;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

} // namespace

bool FirstDivergence(const GameState& local, const GameState& remote, Divergence* out) {
    *out = Divergence{};
    const auto* a = reinterpret_cast<const unsigned char*>(&local);
    const auto* b = reinterpret_cast<const unsigned char*>(&remote);
    // The first differing BYTE finds the first differing FIELD in table order,
    // because the table is contiguous and ordered (StateReflection.h's
    // static_asserts); the field is then read whole, so a difference in the
    // high byte of an int32 is still reported as that int32.
    for (std::uint32_t i = 0; i < sizeof(GameState); ++i) {
        if (a[i] == b[i]) continue;
        FieldPath path;
        if (!cse::kernel::LocateGameStateByte(i, &path)) return false;
        out->found  = true;
        out->tick   = local.tick;
        out->field  = spell(path);
        out->offset = path.offset;
        out->width  = path.width;
        out->local  = readValue(local, path.offset, path.width, path.type);
        out->remote = readValue(remote, path.offset, path.width, path.type);
        return true;
    }
    return false;
}

std::string DesyncArtifactJson(std::uint32_t reportedFrame, std::uint32_t localChecksum, std::uint32_t remoteChecksum,
                               int remotePlayer, const Divergence* d) {
    std::string j = "{\n";
    j += "  \"reportedFrame\": " + std::to_string(reportedFrame) + ",\n";
    j += "  \"localChecksum\": " + quoted(hex(localChecksum)) + ",\n";
    j += "  \"remoteChecksum\": " + quoted(hex(remoteChecksum)) + ",\n";
    j += "  \"remotePlayer\": " + std::to_string(remotePlayer) + ",\n";
    if (d == nullptr) {
        j += "  \"divergence\": \"state not held: the reported frame had left the history before the peer's state arrived\"\n";
    } else if (!d->found) {
        j += "  \"divergence\": \"none: the two states at the reported frame are byte-identical\"\n";
    } else {
        j += "  \"divergence\": {\n";
        j += "    \"tick\": " + std::to_string(d->tick) + ",\n";
        j += "    \"field\": " + quoted(d->field) + ",\n";
        j += "    \"offset\": " + std::to_string(d->offset) + ",\n";
        j += "    \"width\": " + std::to_string(d->width) + ",\n";
        j += "    \"local\": " + std::to_string(d->local) + ",\n";
        j += "    \"remote\": " + std::to_string(d->remote) + "\n";
        j += "  }\n";
    }
    j += "}\n";
    return j;
}

bool WriteDesyncArtifact(const std::string& path, const std::string& json, std::string* error) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.good()) { if (error) *error = "cannot open " + path; return false; }
    out << json;
    if (!out.good()) { if (error) *error = "cannot write " + path; return false; }
    return true;
}

void StateHistory::Push(const GameState& state) {
    const int slot = static_cast<int>(state.tick % static_cast<std::uint32_t>(kCapacity));
    std::memcpy(&ring_[slot], &state, sizeof(GameState));
    held_[slot] = true;
}

const GameState* StateHistory::Find(std::uint32_t tick) const {
    const int slot = static_cast<int>(tick % static_cast<std::uint32_t>(kCapacity));
    if (!held_[slot] || ring_[slot].tick != tick) return nullptr;
    return &ring_[slot];
}

} // namespace cse::game
