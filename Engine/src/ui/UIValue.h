#pragma once
// One value moving between a game's data and the UI.
//
// A tagged struct with every field present, rather than std::variant: it
// matches UIDeclaration (the same shape, for the same reason), it has no
// exception paths, and it crosses the DLL boundary without depending on the
// caller's standard-library build. Copies happen only when a binding actually
// fires — never on a frame where nothing changed.
//
// COERCION NEVER GUESSES. Every As* returns false rather than picking a
// plausible interpretation, so the caller can name BOTH kinds in the error
// ("cannot use string 'rifle' as a length"). Without reflection, a value that
// silently converted wrong is indistinguishable from a UI that simply does not
// work, and that is the single hardest bug class in a binding system.
//
// Strings route through the stylesheet's own parsers, so "auto", "50%",
// "#d93a3d" and "rgba(0,0,0,.45)" mean exactly the same thing in a bound value
// as they do in a .cstyle file.
#include "../core/Core.h"
#include "UIStyle.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace MyCoreEngine::ui {

    struct ENGINE_API UIValue {
        enum class Kind : std::uint8_t { None, Bool, Int, Number, Length, Color, String };

        Kind      kind = Kind::None;
        bool      boolean = false;
        // 64-bit on purpose: score, currency and ammo counters must not wrap in
        // a UI value type, and the cost is 4 bytes in a struct that is already
        // carrying a std::string.
        long long integer = 0;
        float     number = 0.0f;
        // A length, not just a number, so a converter can say "37%" rather than
        // forcing every consumer to re-decide what unit a bare float meant.
        StyleLength length{};
        glm::vec4   color{ 0.0f };
        std::string text;

        static UIValue Bool(bool v);
        static UIValue Int(long long v);
        static UIValue Number(float v);
        static UIValue Len(const StyleLength& v);
        static UIValue Color4(const glm::vec4& v);
        static UIValue Str(std::string v);

        bool AsBool(bool& out) const;
        bool AsNumber(float& out) const;
        bool AsInt(long long& out) const;
        bool AsLength(StyleLength& out) const;
        bool AsColor(glm::vec4& out) const;

        // `decimals` < 0 formats with %g, so an integral number prints "100"
        // rather than "100.000000" — most of the difference between a template
        // that reads like a template and one that reads like C++.
        std::string ToDisplayString(int decimals = -1) const;

        // False, with `why` filled in, for NaN or +/-inf in ANY numeric field.
        //
        // This must be checked BEFORE a value is stringified or coerced, and
        // that ordering is the whole point: %g turns NaN into "nan", and strtod
        // accepts "nan", so a non-finite value that is not gated here parses
        // back as a perfectly valid length, reaches yoga, and makes the layout
        // undefined with no diagnostic anywhere.
        bool IsFinite(std::string& why) const;

        // Compares only the field the kind selects. Different kinds are never
        // equal, even when they would coerce to the same thing — this drives
        // change detection, and "1 == 1.0 == true" there would mean a genuine
        // type change went unnoticed.
        bool operator==(const UIValue& o) const;
        bool operator!=(const UIValue& o) const { return !(*this == o); }

        // "number", "string", ... for error messages.
        const char* KindName() const;
    };

} // namespace MyCoreEngine::ui
