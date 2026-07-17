#include "AssetBrowserPanel.h"
#include "../UndoHistory.h"
#include "imgui.h"
#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {

    std::string lowerExt(const std::filesystem::path& p) {
        std::string e = p.extension().string();
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return e;
    }

    const char* kindTag(int k) {
        static const char* tags[] = { "[DIR]", "[MDL]", "[SCN]", "[TEX]", "[SHD]", "[ - ]" };
        return tags[k];
    }

} // namespace

void AssetBrowserPanel::rescan_() {
    framesSinceScan_ = 0;
    entries_.clear();

    std::error_code ec;
    if (!std::filesystem::is_directory(cwd_, ec)) cwd_ = "Exported"; // deleted/renamed dir

    std::vector<Entry> dirs, files;
    try {
        for (const auto& de : std::filesystem::directory_iterator(cwd_, ec)) {
            try {
                Entry en;
                // path::string() THROWS for names not representable in the
                // active code page (MSVC strict conversion — no '?' fallback).
                // Such files can't be opened by the engine's narrow-path IO
                // anyway: skip them instead of crashing the whole editor.
                en.name = de.path().filename().string();
                en.relPath = de.path().generic_string(); // forward slashes, engine style
                if (de.is_directory(ec)) {
                    en.kind = Kind::Directory;
                    dirs.push_back(std::move(en));
                    continue;
                }
                const std::string ext = lowerExt(de.path());
                if (ext == ".obj") en.kind = Kind::Model;
                else if (ext == ".json" && en.name != "project.json") en.kind = Kind::SceneJson;
                else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" ||
                         ext == ".tga" || ext == ".bmp") en.kind = Kind::Texture;
                else if (ext == ".glsl") en.kind = Kind::Shader;
                else en.kind = Kind::Other;
                files.push_back(std::move(en));
            }
            catch (const std::exception&) {
                continue; // unrepresentable name: skip this entry
            }
        }
    }
    catch (const std::exception&) {
        // iterator advance failure (dir vanished mid-scan etc.): keep what we have
    }
    auto byName = [](const Entry& a, const Entry& b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    entries_ = std::move(dirs);
    entries_.insert(entries_.end(), files.begin(), files.end());
}

AssetBrowserActions AssetBrowserPanel::Draw(entt::registry& reg, entt::entity selected,
                                            UndoHistory& undo,
                                            MyCoreEngine::AssetManager* assets,
                                            bool playing) {
    AssetBrowserActions actions;

    ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Assets")) {
        // toolbar: up / current dir / refresh
        const bool atRoot = (cwd_ == std::filesystem::path("Exported"));
        if (atRoot) ImGui::BeginDisabled();
        if (ImGui::Button("Up")) {
            cwd_ = cwd_.parent_path();
            framesSinceScan_ = 1 << 20;
        }
        if (atRoot) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) framesSinceScan_ = 1 << 20;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", cwd_.generic_string().c_str());
        ImGui::Separator();

        // rescan periodically (files change on disk: saves, builds, OneDrive)
        if (++framesSinceScan_ > 120) rescan_();

        if (entries_.empty()) ImGui::TextDisabled("(empty)");
        for (const Entry& e : entries_) {
            ImGui::PushID(e.relPath.c_str());

            char row[320];
            std::snprintf(row, sizeof(row), "%s %s", kindTag((int)e.kind), e.name.c_str());
            ImGui::Selectable(row, false, ImGuiSelectableFlags_AllowDoubleClick);

            const bool doubleClicked =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            // models drag into the viewport to spawn where they land
            if (e.kind == Kind::Model && ImGui::BeginDragDropSource()) {
                char payload[260] = {};
                std::snprintf(payload, sizeof(payload), "%s", e.relPath.c_str());
                ImGui::SetDragDropPayload(kAssetPayload, payload, sizeof(payload));
                ImGui::Text("Spawn %s", e.name.c_str());
                ImGui::EndDragDropSource();
            }

            switch (e.kind) {
            case Kind::Directory:
                if (doubleClicked) {
                    cwd_ = e.relPath;
                    framesSinceScan_ = 1 << 20;
                }
                break;
            case Kind::Model:
                if (doubleClicked) actions.spawnModel = e.relPath;
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Spawn in Scene")) actions.spawnModel = e.relPath;
                    const bool canAssign = assets && reg.valid(selected);
                    if (ImGui::MenuItem("Assign to Selected Entity", nullptr, false, canAssign)) {
                        if (auto model = assets->GetModel(e.relPath);
                            model && !model->Meshes().empty()) {
                            undo.record(reg, selected, "Assign model", [&] {
                                reg.emplace_or_replace<ModelComponent>(selected, ModelComponent{ model });
                                reg.emplace_or_replace<AABB>(selected, generateAABB(*model));
                                if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                            });
                            actions.shadowsDirty = true; // swapped caster, clean transform
                        }
                    }
                    if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(e.relPath.c_str());
                    ImGui::EndPopup();
                }
                break;
            case Kind::SceneJson:
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
    }
    ImGui::End();
    return actions;
}
