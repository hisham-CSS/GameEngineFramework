#pragma once
#include "../core/Core.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MyCoreEngine {

    // The asset FILESYSTEM domain: a cached, engine-owned view of the
    // runtime asset tree (the Exported/ root the engine actually loads
    // from). All disk walking lives here — UI (the editor's Assets panel)
    // and future systems render or query the cached tree instead of
    // touching std::filesystem themselves.
    //
    // Split from AssetManager on purpose: the manager caches what is
    // LOADED (models by path); the index knows what EXISTS on disk. This
    // is the seam where asset I/O gets optimized later without touching
    // the UI: async scanning on the job system (P4-3), file watchers
    // instead of polling, GUIDs + metadata sidecars, import pipelines,
    // and a pack-file/VFS backend for shipped builds. None of that is
    // implemented yet — today it is a synchronous, throttled rescan.
    //
    // Robustness rules baked in (recurring crash classes):
    // - path::string() THROWS on names outside the active code page
    //   (MSVC strict conversion). Such files can't be opened by the
    //   engine's narrow-path IO anyway: skipped per-entry, never fatal.
    // - Directory cycles (junctions/symlinks) are cut by a depth cap.
    // - A root that vanishes mid-session yields an empty tree, not an
    //   error state.
    class ENGINE_API AssetIndex {
    public:
        enum class Kind { Directory, Model, SceneJson, Texture, Shader, Other };

        struct Node {
            std::string name;     // display filename
            std::string relPath;  // engine-style path ("Exported/Model/foo.obj")
            Kind kind = Kind::Other;
            std::vector<Node> children; // non-empty for directories only;
                                        // sorted: directories first, then
                                        // files, each A→Z
            // planned (not yet stored): guid, size, mtime, import state
        };

        explicit AssetIndex(std::string root = "Exported");

        // Throttled polling: call once per frame with the frame delta; the
        // tree is rewalked at most every rescanInterval() seconds. Between
        // rescans this is a couple of float ops.
        void tick(float dt);

        // Rescan on the next tick regardless of the throttle (toolbar
        // Refresh, right after the app writes a file itself).
        void forceRescan() { pending_ = true; }

        void  setRescanInterval(float seconds);
        float rescanInterval() const { return interval_; }

        // The cached tree. Stable between rescans; a rescan replaces the
        // whole tree, so hold relPaths (not Node pointers) across frames.
        const Node& root() const { return root_; }

        // Bumps only when a rescan actually CHANGED the tree — cheap way
        // for dependents (thumbnails, caches) to notice disk changes.
        std::uint64_t version() const { return version_; }

        // Depth-first lookup by engine-style path; null when absent.
        const Node* find(const std::string& relPath) const;

    private:
        void rescanNow_();
        static void scanDir_(const std::filesystem::path& dir, Node& out, int depth);
        static bool sameTree_(const Node& a, const Node& b);

        std::string rootPath_;
        Node root_;
        std::uint64_t version_ = 0;
        float interval_ = 2.0f;
        float sinceScan_ = 1e9f; // scan on first tick
        bool pending_ = false;
    };

} // namespace MyCoreEngine
