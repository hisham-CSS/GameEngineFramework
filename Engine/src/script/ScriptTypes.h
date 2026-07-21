#pragma once
// Shared vocabulary for the scripting seam. Nothing here knows about Lua,
// entt, or any particular language runtime.

#include <cstdint>
#include <string>

namespace MyCoreEngine {

    // Opaque handle to one loaded script INSTANCE (not one script file).
    // Two entities running the same .lua file get two ids and two isolated
    // global environments, so per-entity state cannot bleed across objects.
    enum class ScriptId : uint64_t { Invalid = 0 };

    inline bool IsValid(ScriptId id) { return id != ScriptId::Invalid; }

    // An entity as seen from inside a script. Deliberately a plain integer:
    // backends must never include entt, exactly as physics backends never see
    // entt::entity. ScriptWorld converts at the boundary.
    using ScriptEntity = uint64_t;
    inline constexpr ScriptEntity kInvalidScriptEntity = ~uint64_t(0);

    // A script failure. Reported across the seam as a value, never thrown:
    // an exception escaping a script callback would have to unwind through
    // the language runtime's own C frames, which is not portable.
    struct ScriptError {
        std::string message;  // includes chunk name + line when the backend can supply it
        int  line = -1;       // -1 when unknown
        bool ok() const { return message.empty(); }
    };

    struct ScriptSettings {
        // Directory searched for script files and `require`d modules.
        std::string scriptDirectory;

        // Hard cap on VM instructions per callback; 0 disables the check.
        // This exists because a script is authored content that runs inside
        // the editor's frame loop: `while true do end` in a user's file would
        // otherwise hang the editor with no way out but the task manager.
        // With a limit, the offending script is aborted and disabled and the
        // editor keeps running.
        uint32_t instructionLimit = 2'000'000;

        // Expose the language's unrestricted libraries (io, os, package,
        // debug). Off by default: a shipped game running downloaded scripts
        // should not hand them the filesystem. The editor may turn it on for
        // authoring convenience.
        bool allowUnsafeLibraries = false;
    };

} // namespace MyCoreEngine
