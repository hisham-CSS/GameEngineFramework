// The reflection table over GameState (ROADMAP M2.3, E6; DETERMINISM.md S8).
//
// {name, offset, element size, count, type} for every field of Event, Fighter
// and GameState, in declaration order, and a compile-time proof that the
// three tables together name EVERY BYTE: CoveredBytes() walks each table and
// requires the fields to be contiguous from offset 0 to sizeof(the struct).
// Add a field to GameState without adding it here and the static_assert below
// fails in the same translation unit that included GameState.h -- which is
// what S8 asks, "every field added is added to the table in the same commit",
// as a compile error rather than a review item.
//
// One table yields three things (ARCHITECTURE.md, the reflection table): the
// per-field desync log (M2.3, cse::game::FirstDivergence names the first
// field two peers disagree about), and later the Inspector view and a JSON
// serializer. It is integers and string literals only, and lives in the
// kernel because the kernel owns the layout; nothing here allocates or runs
// inside a tick.
#pragma once
#include "GameState.h"

#include <cstddef>   // det-ok: offsetof only -- a compile-time constant; <cstddef> holds no container and no allocator (K4, K5 stay unreachable)
#include <cstdint>

namespace cse::kernel {

enum class FieldType : std::uint8_t { U8, I16, U16, U32, I32, EventT, FighterT };

struct FieldInfo {
    const char*   name;
    std::uint32_t offset;        // from the start of the enclosing struct
    std::uint32_t elementSize;   // one element; the field spans elementSize * count
    std::uint32_t count;         // 1 for a scalar
    FieldType     type;
};

// offsetof is well defined for these standard-layout structs, and sizeof of an
// unevaluated member is C++11; the macro keeps a table row from misspelling
// its own name, size or offset.
#define CSE_STATE_FIELD(Struct, member, type, n) \
    ::cse::kernel::FieldInfo{ #member, static_cast<std::uint32_t>(offsetof(Struct, member)), \
                              static_cast<std::uint32_t>(sizeof(Struct::member) / (n)), \
                              static_cast<std::uint32_t>(n), type }

inline constexpr FieldInfo kEventFields[] = {
    CSE_STATE_FIELD(Event, slot, FieldType::U8,  1),
    CSE_STATE_FIELD(Event, kind, FieldType::U8,  1),
    CSE_STATE_FIELD(Event, a,    FieldType::I16, 1),
    CSE_STATE_FIELD(Event, b,    FieldType::I16, 1),
};

inline constexpr FieldInfo kFighterFields[] = {
    CSE_STATE_FIELD(Fighter, posX,            FieldType::I32, 1),
    CSE_STATE_FIELD(Fighter, posY,            FieldType::I32, 1),
    CSE_STATE_FIELD(Fighter, velX,            FieldType::I32, 1),
    CSE_STATE_FIELD(Fighter, velY,            FieldType::I32, 1),
    CSE_STATE_FIELD(Fighter, health,          FieldType::I32, 1),
    CSE_STATE_FIELD(Fighter, res,             FieldType::I32, kMaxResources),
    CSE_STATE_FIELD(Fighter, pushX,           FieldType::I32, 1),
    CSE_STATE_FIELD(Fighter, moveId,          FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, moveFrame,       FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, hitstun,         FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, blockstun,       FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, hitstop,         FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, knockdown,       FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, juggle,          FieldType::I16, 1),
    CSE_STATE_FIELD(Fighter, scaling,         FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, facing,          FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, airborne,        FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, comboHits,       FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, alreadyHitBits,  FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, team,            FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, active,          FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, crouching,       FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, guard,           FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, reaction,        FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, bounces,         FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, flags,           FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, prevButtons,     FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, bufferedButtons, FieldType::U16, 1),
    CSE_STATE_FIELD(Fighter, bufferAge,       FieldType::U8,  1),
    CSE_STATE_FIELD(Fighter, pad_,            FieldType::U8,  3),
};

inline constexpr FieldInfo kGameStateFields[] = {
    CSE_STATE_FIELD(GameState, tick,         FieldType::U32,      1),
    CSE_STATE_FIELD(GameState, rng,          FieldType::U32,      1),
    CSE_STATE_FIELD(GameState, roundTimer,   FieldType::U32,      1),
    CSE_STATE_FIELD(GameState, roundNumber,  FieldType::U16,      1),
    CSE_STATE_FIELD(GameState, roundState,   FieldType::U8,       1),
    CSE_STATE_FIELD(GameState, fighterCount, FieldType::U8,       1),
    CSE_STATE_FIELD(GameState, roundsWon,    FieldType::U8,       kMaxTeams),
    CSE_STATE_FIELD(GameState, roundsToWin,  FieldType::U8,       1),
    CSE_STATE_FIELD(GameState, pad_,         FieldType::U8,       1),
    CSE_STATE_FIELD(GameState, ev,           FieldType::EventT,   kMaxEventsPerTick),
    CSE_STATE_FIELD(GameState, evCount,      FieldType::U8,       1),
    CSE_STATE_FIELD(GameState, pad2_,        FieldType::U8,       3),
    CSE_STATE_FIELD(GameState, p,            FieldType::FighterT, kMaxFighters),
};

#undef CSE_STATE_FIELD

// The bytes a table covers when its fields are contiguous from 0; a table with
// a gap, an overlap or a field out of order covers 0xFFFFFFFF.
template <std::size_t N>
constexpr std::uint32_t CoveredBytes(const FieldInfo (&table)[N]) {
    std::uint32_t expect = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (table[i].offset != expect) return 0xFFFFFFFFu;
        expect += table[i].elementSize * table[i].count;
    }
    return expect;
}

static_assert(CoveredBytes(kEventFields) == sizeof(Event),
              "S8: an Event field is missing from the reflection table, or the table is out of order");
static_assert(CoveredBytes(kFighterFields) == sizeof(Fighter),
              "S8: a Fighter field is missing from the reflection table, or the table is out of order");
static_assert(CoveredBytes(kGameStateFields) == sizeof(GameState),
              "S8: a GameState field is missing from the reflection table, or the table is out of order");

// Where one byte of a GameState lives: the top-level field and, for an element
// of ev[] or p[], the member inside it. Integers only; the caller spells the
// path ("p[1].posX", "ev[3].a", "roundsWon[1]").
struct FieldPath {
    const FieldInfo* top         = nullptr;   // the GameState field
    std::uint32_t    topIndex    = 0;         // into top->count
    const FieldInfo* member      = nullptr;   // inside an Event or a Fighter, else null
    std::uint32_t    memberIndex = 0;         // into member->count
    std::uint32_t    offset      = 0;         // of the located scalar, from the GameState's start
    std::uint32_t    width       = 0;         // of the located scalar
    FieldType        type        = FieldType::U8;
};

inline bool LocateGameStateByte(std::uint32_t byte, FieldPath* out) {
    for (const FieldInfo& f : kGameStateFields) {
        const std::uint32_t span = f.elementSize * f.count;
        if (byte < f.offset || byte >= f.offset + span) continue;
        out->top      = &f;
        out->topIndex = (byte - f.offset) / f.elementSize;
        const std::uint32_t inner = (byte - f.offset) % f.elementSize;
        const FieldInfo* table = nullptr;
        std::size_t n = 0;
        if (f.type == FieldType::EventT)   { table = kEventFields;   n = sizeof(kEventFields) / sizeof(kEventFields[0]); }
        if (f.type == FieldType::FighterT) { table = kFighterFields; n = sizeof(kFighterFields) / sizeof(kFighterFields[0]); }
        if (table == nullptr) {
            out->member      = nullptr;
            out->memberIndex = 0;
            out->offset      = f.offset + out->topIndex * f.elementSize;
            out->width       = f.elementSize;
            out->type        = f.type;
            return true;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const FieldInfo& m = table[i];
            const std::uint32_t mspan = m.elementSize * m.count;
            if (inner < m.offset || inner >= m.offset + mspan) continue;
            out->member      = &m;
            out->memberIndex = (inner - m.offset) / m.elementSize;
            out->offset      = f.offset + out->topIndex * f.elementSize + m.offset + out->memberIndex * m.elementSize;
            out->width       = m.elementSize;
            out->type        = m.type;
            return true;
        }
        return false;   // unreachable while the member tables cover their structs
    }
    return false;
}

} // namespace cse::kernel
