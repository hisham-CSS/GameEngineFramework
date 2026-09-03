// THE CLIP TABLE (ROADMAP M3.4b; ADR-019 D2 and D9).
//
// Which clip a fighter wears for each PoseKind, and for each move slot when
// the kind is Move -- resolved ONCE per adopted character, by NAME, from the
// character as CseData loaded it: the per-move `engine.anim3d.clip` override
// (else the move id) and the `<stem>.clips.json` sidecar A21/A22 already
// checked. Nothing here opens a file or knows what a matrix is; the mode hands
// the names to Model::GetClips() when it composes a frame (M3.4c).
//
// WHY BY NAME AND WHY REBUILT ON EVERY ADOPT. BuildMatchData numbers a
// character's moves into slots in file order, and a hot reload that reorders
// or inserts a move renumbers every slot after it. A table keyed by the slots
// of the PREVIOUS build would hand the new stand_lp the old crouch_mk's clip
// -- a pose from the wrong move on the right frame, which nothing downstream
// can detect. So the table is rebuilt from ids in adoptPrepared_, beside the
// binding table that already does the same for the same reason
// (FighterClips.AReloadThatReordersMovesRebindsEveryClipByName).
//
// WHAT THIS LIBRARY MAY DEPEND ON. CseGame, transitively CseData and CseKernel,
// and nothing else -- Presentation/CMakeLists.txt enforces it. That is why the
// input is the loaded CharacterData and a slot list rather than a Model.
#pragma once

#include "cse/data/CharacterData.h"
#include "cse/game/PoseSelect.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cse::presentation {

// One clip as the table knows it: the name Model::GetClips().Find() takes and
// the frame count the sidecar declared for it (A21 made it MoveDuration for a
// move; a cycle is whatever the artist keyed, >= 2).
struct ClipRef {
    std::string   name;
    std::uint32_t frames = 0;
};

// A move id and the slot BuildMatchData gave it in the current build. Slot 0
// is BuildMatchData's "absent" sentinel and is never a clip.
struct MoveSlot {
    std::uint16_t slot = 0;
    std::string   id;
};

class FighterClips {
public:
    // Rebuild from the character as loaded. A character with no
    // engine.anim3d.model yields an empty table: Find() answers nullptr for
    // everything and the mode draws the 2D placeholders as before. A move
    // whose clip is not in the sidecar gets no entry (A21 refuses that file,
    // so this is defence, not policy).
    void Rebuild(const cse::data::CharacterData& character,
                 const std::vector<MoveSlot>& slots);

    // The clip for a pose. `moveSlot` matters only when `kind` is Move; the
    // reserved cycles are keyed by kind alone. nullptr when there is none --
    // for None, for slot 0, for an empty table.
    const ClipRef* Find(cse::game::PoseKind kind, std::uint16_t moveSlot) const;

    bool        Empty() const { return moveClips_.empty() && !anyCycle_; }
    std::size_t MoveClipCount() const;

private:
    // Indexed by PoseKind; None and Move never hold a clip.
    std::array<ClipRef, cse::game::kPoseKindCount> cycles_{};
    bool anyCycle_ = false;
    // Indexed by slot; slot 0 is unused.
    std::vector<ClipRef> moveClips_;
};

} // namespace cse::presentation
