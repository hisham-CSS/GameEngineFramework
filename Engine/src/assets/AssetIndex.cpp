#include "AssetIndex.h"
#include "../core/JobSystem.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace {

    std::string lowerExt(const std::filesystem::path& p) {
        std::string e = p.extension().string();
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return e;
    }

    MyCoreEngine::AssetIndex::Kind classify(const std::filesystem::path& p,
                                            const std::string& name) {
        using Kind = MyCoreEngine::AssetIndex::Kind;
        const std::string ext = lowerExt(p);
        if (ext == ".obj") return Kind::Model;
        if (ext == ".json" && name != "project.json") return Kind::SceneJson;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" ||
            ext == ".tga" || ext == ".bmp") return Kind::Texture;
        if (ext == ".glsl") return Kind::Shader;
        return Kind::Other;
    }

    constexpr int kMaxDepth = 16; // junction/symlink cycle cut-off

} // namespace

namespace MyCoreEngine {

    AssetIndex::AssetIndex(std::string root)
        : rootPath_(std::move(root))
    {
        root_.name = rootPath_;
        root_.relPath = rootPath_;
        root_.kind = Kind::Directory;
    }

    void AssetIndex::setRescanInterval(float seconds)
    {
        interval_ = seconds > 0.1f ? seconds : 0.1f;
    }

    void AssetIndex::tick(float dt, JobSystem* jobs)
    {
        sinceScan_ += dt > 0.f ? dt : 0.f;
        if (!(pending_ || sinceScan_ >= interval_)) return;
        if (jobs) rescanAsync_(*jobs);
        else rescanNow_();
    }

    AssetIndex::Node AssetIndex::buildTree_(const std::string& rootPath)
    {
        Node fresh;
        fresh.name = rootPath;
        fresh.relPath = rootPath;
        fresh.kind = Kind::Directory;

        std::error_code ec;
        if (std::filesystem::is_directory(rootPath, ec)) {
            scanDir_(rootPath, fresh, 0);
        }
        // else: vanished root -> empty tree (never an error state)
        return fresh;
    }

    void AssetIndex::adoptTree_(Node&& fresh)
    {
        if (!sameTree_(root_, fresh)) {
            root_ = std::move(fresh);
            ++version_;
        }
    }

    void AssetIndex::rescanNow_()
    {
        pending_ = false;
        sinceScan_ = 0.f;
        adoptTree_(buildTree_(rootPath_));
    }

    void AssetIndex::rescanAsync_(JobSystem& jobs)
    {
        if (scanInFlight_) return; // one walk at a time; retry next interval
        scanInFlight_ = true;
        pending_ = false;
        sinceScan_ = 0.f;

        // worker gets a COPY of the root path (never mutated after ctor,
        // but a copy keeps the job free of any reference to this object);
        // the tree swap happens back on the main thread
        auto result = std::make_shared<Node>();
        const std::string rootPath = rootPath_;
        jobs.submit(
            [result, rootPath] { *result = buildTree_(rootPath); },
            [this, result] {
                adoptTree_(std::move(*result));
                scanInFlight_ = false;
            });
    }

    void AssetIndex::scanDir_(const std::filesystem::path& dir, Node& out, int depth)
    {
        if (depth >= kMaxDepth) return;

        std::vector<Node> dirs, files;
        std::error_code ec;
        try {
            for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
                try {
                    Node n;
                    // path::string() THROWS for names not representable in
                    // the active code page (MSVC strict conversion — no '?'
                    // fallback). The engine's narrow-path IO can't open such
                    // files anyway: skip the entry, never crash the scan.
                    n.name = de.path().filename().string();
                    n.relPath = de.path().generic_string(); // engine style
                    if (de.is_directory(ec)) {
                        n.kind = Kind::Directory;
                        scanDir_(de.path(), n, depth + 1);
                        dirs.push_back(std::move(n));
                    }
                    else {
                        // .import sidecars are per-asset metadata (edited
                        // via the Inspector), not browsable content
                        constexpr const char* kSidecar = ".import";
                        constexpr size_t kSidecarLen = 7;
                        if (n.name.size() > kSidecarLen &&
                            n.name.compare(n.name.size() - kSidecarLen, kSidecarLen,
                                           kSidecar) == 0) {
                            continue;
                        }
                        n.kind = classify(de.path(), n.name);
                        files.push_back(std::move(n));
                    }
                }
                catch (const std::exception&) {
                    continue; // unrepresentable name / vanished entry
                }
            }
        }
        catch (const std::exception&) {
            // iterator advance failure (dir vanished mid-scan): keep what we have
        }

        auto byName = [](const Node& a, const Node& b) { return a.name < b.name; };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);
        out.children = std::move(dirs);
        out.children.insert(out.children.end(),
                            std::make_move_iterator(files.begin()),
                            std::make_move_iterator(files.end()));
    }

    bool AssetIndex::sameTree_(const Node& a, const Node& b)
    {
        if (a.name != b.name || a.relPath != b.relPath || a.kind != b.kind ||
            a.children.size() != b.children.size()) return false;
        for (size_t i = 0; i < a.children.size(); ++i) {
            if (!sameTree_(a.children[i], b.children[i])) return false;
        }
        return true;
    }

    const AssetIndex::Node* AssetIndex::find(const std::string& relPath) const
    {
        if (relPath == root_.relPath) return &root_;
        // relPaths are hierarchical: descend only into directories whose
        // path is a prefix of the target
        const Node* cur = &root_;
        for (;;) {
            const Node* next = nullptr;
            for (const Node& c : cur->children) {
                if (c.relPath == relPath) return &c;
                if (c.kind == Kind::Directory &&
                    relPath.size() > c.relPath.size() &&
                    relPath.compare(0, c.relPath.size(), c.relPath) == 0 &&
                    relPath[c.relPath.size()] == '/') {
                    next = &c;
                    break;
                }
            }
            if (!next) return nullptr;
            cur = next;
        }
    }

} // namespace MyCoreEngine
