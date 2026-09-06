#include "cse/presentation/FighterClips.h"

#include <algorithm>

namespace cse::presentation {

namespace {

const cse::data::ClipLength* findClip(const cse::data::CharacterData& c, const std::string& name) {
    for (const auto& cl : c.anim3dClips)
        if (cl.name == name) return &cl;
    return nullptr;
}

} // namespace

void FighterClips::Rebuild(const cse::data::CharacterData& character,
                           const std::vector<MoveSlot>& slots) {
    cycles_    = {};
    anyCycle_  = false;
    moveClips_.clear();
    if (character.anim3dModel.empty()) return;   // off by default: no model, no table

    // The reserved cycles, in PoseKind order. kReservedCycleNames is spelled
    // in that order on purpose and test_pose_select pins it against
    // PoseKindName, so one list serves the loader (A22), this table and the
    // selector.
    using cse::game::PoseKind;
    for (std::size_t i = 0; i < cse::data::kReservedCycleNames.size(); ++i) {
        const std::size_t kind = static_cast<std::size_t>(PoseKind::Idle) + i;
        if (kind >= cycles_.size()) break;
        if (const auto* cl = findClip(character, cse::data::kReservedCycleNames[i])) {
            cycles_[kind] = ClipRef{ cl->name, static_cast<std::uint32_t>(std::max(cl->frames, 0)) };
            anyCycle_ = true;
        }
    }

    // The moves, by id -> slot. The table is sized to the largest slot handed
    // in, so a reload that renumbers is a rebuild, never an index past the end.
    std::uint16_t maxSlot = 0;
    for (const MoveSlot& s : slots) maxSlot = std::max(maxSlot, s.slot);
    moveClips_.assign(static_cast<std::size_t>(maxSlot) + 1u, ClipRef{});
    for (const MoveSlot& s : slots) {
        if (s.slot == 0) continue;
        const cse::data::MoveIndex mi = character.FindMove(s.id);
        if (mi == cse::data::kInvalidMove) continue;
        const cse::data::Move& mv = character.moves[mi];
        const std::string& clipName = mv.anim3dClip.empty() ? mv.id : mv.anim3dClip;
        if (const auto* cl = findClip(character, clipName))
            moveClips_[s.slot] = ClipRef{ cl->name, static_cast<std::uint32_t>(std::max(cl->frames, 0)) };
    }
}

const ClipRef* FighterClips::Find(cse::game::PoseKind kind, std::uint16_t moveSlot) const {
    using cse::game::PoseKind;
    if (kind == PoseKind::None) return nullptr;
    if (kind == PoseKind::Move) {
        if (moveSlot == 0 || moveSlot >= moveClips_.size()) return nullptr;
        const ClipRef& r = moveClips_[moveSlot];
        return r.name.empty() ? nullptr : &r;
    }
    const std::size_t k = static_cast<std::size_t>(kind);
    if (k >= cycles_.size()) return nullptr;
    return cycles_[k].name.empty() ? nullptr : &cycles_[k];
}

std::size_t FighterClips::MoveClipCount() const {
    std::size_t n = 0;
    for (const ClipRef& r : moveClips_) if (!r.name.empty()) ++n;
    return n;
}

} // namespace cse::presentation
