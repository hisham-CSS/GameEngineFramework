#pragma once
// ECS-facing scripting component. PURE DATA, like PhysicsComponents.h, and
// for the same reason: the editor snapshots components wholesale for undo/redo
// and play-stop, resurrecting entities via reg.clear() + create(hint). A
// ScriptId stored here would survive that restore as a DANGLING handle to an
// instance the backend already destroyed. The entity -> instance map lives
// only in ScriptWorld, which is rebuilt after every bulk restore.

#include <string>

namespace MyCoreEngine {

    // Attaches one script file to an entity. Each entity gets its OWN
    // instance of the file, with isolated globals — two crates running
    // crate.lua do not share state.
    struct ScriptComponent {
        // Path relative to the project's script directory, e.g. "spinner.lua".
        std::string path;

        // Authoring toggle. A disabled script is still loaded (so errors show
        // up in the editor immediately) but no callback runs.
        bool enabled = true;
    };

} // namespace MyCoreEngine
