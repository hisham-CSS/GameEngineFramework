#include "AssetBrowserPanel.h"
#include "imgui.h"
#include "Engine.h"

#include <cstdio>
#include <vector>

using MyCoreEngine::AssetIndex;

namespace {

    const char* kindTag(AssetIndex::Kind k) {
        switch (k) {
        case AssetIndex::Kind::Directory: return "[DIR]";
        case AssetIndex::Kind::Model:     return "[MDL]";
        case AssetIndex::Kind::SceneJson: return "[SCN]";
        case AssetIndex::Kind::Texture:   return "[TEX]";
        case AssetIndex::Kind::Shader:    return "[SHD]";
        default:                          return "[ - ]";
        }
    }

    // is `path` equal to `dir` or anywhere below it?
    bool isSameOrUnder(const std::string& path, const std::string& dir) {
        if (path == dir) return true;
        return path.size() > dir.size() &&
               path.compare(0, dir.size(), dir) == 0 &&
               path[dir.size()] == '/';
    }

} // namespace

void AssetBrowserPanel::navigateTo_(const std::string& relPath) {
    selectedDir_ = relPath;
    revealSelection_ = true; // open the tree down to it next draw
}

void AssetBrowserPanel::drawBreadcrumbs_() {
    // "Exported > Model > ..." — every segment is clickable
    const std::string& sel = selectedDir_;
    size_t start = 0;
    bool first = true;
    while (start < sel.size()) {
        size_t slash = sel.find('/', start);
        if (slash == std::string::npos) slash = sel.size();
        const std::string segment = sel.substr(start, slash - start);
        const std::string upToHere = sel.substr(0, slash);

        if (!first) {
            ImGui::SameLine(0.f, 2.f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.f, 2.f);
        }
        first = false;

        ImGui::PushID((int)start);
        if (upToHere == sel) {
            // current folder: plain text, nothing to navigate to
            ImGui::TextUnformatted(segment.c_str());
        }
        else if (ImGui::SmallButton(segment.c_str())) {
            navigateTo_(upToHere);
        }
        ImGui::PopID();

        start = slash + 1;
    }
}

void AssetBrowserPanel::drawFolderTree_(const void* nodePtr, bool isRoot) {
    const auto& node = *static_cast<const AssetIndex::Node*>(nodePtr);

    bool hasSubdirs = false;
    for (const auto& c : node.children) {
        if (c.kind == AssetIndex::Kind::Directory) { hasSubdirs = true; break; }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasSubdirs) flags |= ImGuiTreeNodeFlags_Leaf;
    if (node.relPath == selectedDir_) flags |= ImGuiTreeNodeFlags_Selected;
    if (isRoot) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    // reveal a folder picked via double-click/breadcrumb: force ancestors open
    if (revealSelection_ && node.relPath != selectedDir_ &&
        isSameOrUnder(selectedDir_, node.relPath)) {
        ImGui::SetNextItemOpen(true);
    }

    const bool open = ImGui::TreeNodeEx(node.relPath.c_str(), flags, "%s", node.name.c_str());
    // opening the ancestors isn't enough in a tall tree: bring the target
    // into the visible scroll region too
    if (revealSelection_ && node.relPath == selectedDir_) ImGui::SetScrollHereY();
    // click (not the expand arrow) selects the folder for the content pane
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        selectedDir_ = node.relPath;
    }
    if (open) {
        for (const auto& c : node.children) {
            if (c.kind == AssetIndex::Kind::Directory) drawFolderTree_(&c, false);
        }
        ImGui::TreePop();
    }
}

AssetBrowserActions AssetBrowserPanel::drawContents_(const void* nodePtr,
                                                     entt::registry& reg,
                                                     entt::entity selected,
                                                     bool playing) {
    AssetBrowserActions actions;
    const auto& dir = *static_cast<const AssetIndex::Node*>(nodePtr);

    if (dir.children.empty()) ImGui::TextDisabled("(empty)");
    for (const auto& e : dir.children) {
        ImGui::PushID(e.relPath.c_str());

        char row[320];
        std::snprintf(row, sizeof(row), "%s %s", kindTag(e.kind), e.name.c_str());
        ImGui::Selectable(row, e.relPath == selectedAsset_,
                          ImGuiSelectableFlags_AllowDoubleClick);

        const bool doubleClicked =
            ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        // single-click on a FILE highlights it for the Inspector asset view;
        // suppressed when this click completed a double-click — the double's
        // action (spawn/drill/load) owns the frame
        if (!doubleClicked && e.kind != AssetIndex::Kind::Directory &&
            ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selectedAsset_ = e.relPath;
            actions.assetClicked = true;
        }

        // models drag into the viewport to spawn where they land
        if (e.kind == AssetIndex::Kind::Model && ImGui::BeginDragDropSource()) {
            char payload[260] = {};
            std::snprintf(payload, sizeof(payload), "%s", e.relPath.c_str());
            ImGui::SetDragDropPayload(kAssetPayload, payload, sizeof(payload));
            ImGui::Text("Spawn %s", e.name.c_str());
            ImGui::EndDragDropSource();
        }

        switch (e.kind) {
        case AssetIndex::Kind::Directory:
            if (doubleClicked) navigateTo_(e.relPath);
            break;
        case AssetIndex::Kind::Model:
            if (doubleClicked) actions.spawnModel = e.relPath;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Spawn in Scene")) actions.spawnModel = e.relPath;
                const bool canAssign = reg.valid(selected);
                if (ImGui::MenuItem("Assign to Selected Entity", nullptr, false, canAssign)) {
                    actions.assignModel = e.relPath; // editor resolves it async
                }
                if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(e.relPath.c_str());
                ImGui::EndPopup();
            }
            break;
        case AssetIndex::Kind::SceneJson:
            if (doubleClicked && !playing) actions.loadScene = e.relPath;
            if (ImGui::BeginPopupContextItem()) {
                // loading is blocked during play (Stop's restore would
                // stomp it); startup-scene setting is safe anytime
                if (ImGui::MenuItem("Load Scene", nullptr, false, !playing)) {
                    actions.loadScene = e.relPath;
                }
                if (ImGui::MenuItem("Set as Startup Scene")) actions.setStartup = e.relPath;
                if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(e.relPath.c_str());
                ImGui::EndPopup();
            }
            break;
        default:
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(e.relPath.c_str());
                ImGui::EndPopup();
            }
            break;
        }

        ImGui::PopID();
    }
    return actions;
}

AssetBrowserActions AssetBrowserPanel::Draw(entt::registry& reg, entt::entity selected,
                                            AssetIndex& index, bool playing,
                                            int loadingCount, bool validating,
                                            bool* pOpen) {
    AssetBrowserActions actions;

    if (selectedDir_.empty()) selectedDir_ = index.root().relPath;

    ImGui::SetNextWindowSize(ImVec2(460, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Assets", pOpen)) {
        // the selected folder can vanish between rescans — or be REPLACED
        // by a file with the same name: fall back to the nearest existing
        // ancestor DIRECTORY (worst case the root itself)
        const AssetIndex::Node* dir = index.find(selectedDir_);
        while (!dir || dir->kind != AssetIndex::Kind::Directory) {
            const size_t slash = selectedDir_.find_last_of('/');
            if (slash == std::string::npos) {
                selectedDir_ = index.root().relPath;
                dir = &index.root();
                break;
            }
            selectedDir_.resize(slash);
            dir = index.find(selectedDir_);
        }

        // toolbar: refresh (a request to the engine index — the panel owns
        // no disk state) + clickable breadcrumbs
        if (ImGui::SmallButton("Refresh")) index.forceRescan();
        ImGui::SameLine(0.f, 4.f);
        if (validating) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Validate")) actions.validateRequested = true;
        if (validating) ImGui::EndDisabled();
        ImGui::SameLine(0.f, 8.f);
        drawBreadcrumbs_();
        if (loadingCount > 0) {
            ImGui::SameLine(0.f, 12.f);
            ImGui::TextDisabled("loading %d model%s...", loadingCount,
                                loadingCount == 1 ? "" : "s");
        }
        ImGui::Separator();

        // left: folder tree | splitter | right: selected folder's contents
        const float height = ImGui::GetContentRegionAvail().y > 1.f
                                 ? ImGui::GetContentRegionAvail().y : 1.f;
        // clamp UNCONDITIONALLY (floor 80): skipping the upper clamp in a
        // narrow panel let a wide treeWidth_ push the splitter and the
        // whole contents pane outside the clip rect — unreachable until
        // the window was widened again
        const float maxTree = ImGui::GetContentRegionAvail().x - 120.f;
        const float hiTree = maxTree > 80.f ? maxTree : 80.f;
        treeWidth_ = treeWidth_ < 80.f ? 80.f : (treeWidth_ > hiTree ? hiTree : treeWidth_);
        ImGui::BeginChild("##foldertree", ImVec2(treeWidth_, height));
        drawFolderTree_(&index.root(), true);
        revealSelection_ = false; // one-shot: ancestors were forced open above
        ImGui::EndChild();

        ImGui::SameLine(0.f, 0.f);
        ImGui::InvisibleButton("##splitter", ImVec2(6.f, height));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            treeWidth_ += ImGui::GetIO().MouseDelta.x;
        {
            // visible 1px divider inside the splitter hitbox
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const float x = (mn.x + mx.x) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, mn.y), ImVec2(x, mx.y),
                ImGui::GetColorU32(ImGuiCol_Separator));
        }
        ImGui::SameLine(0.f, 0.f);

        ImGui::BeginChild("##contents", ImVec2(0, height));
        actions = drawContents_(dir, reg, selected, playing);
        ImGui::EndChild();
    }
    ImGui::End();
    return actions;
}
