#pragma once
#include "SceneHierarchyPanel.h"
#include "../UndoHistory.h"
#include "imgui.h"
#include "Engine.h" // for Name, Transform, etc.

static const char* GetEntityLabel(entt::registry& reg, entt::entity e) {
    if (auto* n = reg.try_get<Name>(e)) return n->value.c_str();
    return "(Entity)";
}

bool SceneHierarchyPanel::Draw(entt::registry& reg, entt::entity& selected, UndoHistory& undo) {
    bool changed = false;
    if (ImGui::Begin("Scene Hierarchy")) {
        if (ImGui::Button("+ Create Entity")) {
            entt::entity e = reg.create();
            reg.emplace<Name>(e, Name{ "Entity" });
            reg.emplace<Transform>(e);
            undo.recordCreate(reg, e, "Create entity");
            selected = e;
            changed = true;
        }
        ImGui::Separator();

        entt::entity toDelete = entt::null;
        auto all = reg.view<entt::entity>();
        for (auto e : all) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (e == selected) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)e, flags, "%s [%u]",
                GetEntityLabel(reg, e), (uint32_t)e);

            if (ImGui::IsItemClicked()) { selected = e; changed = true; }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete Entity")) {
                    toDelete = e;
                }
                ImGui::EndPopup();
            }
        }

        if (toDelete != entt::null) {
            std::string label = std::string("Delete '") + GetEntityLabel(reg, toDelete) + "'";
            undo.recordDelete(reg, toDelete, std::move(label)); // snapshots, then destroys
            if (selected == toDelete) selected = entt::null;
            changed = true;
        }
    }
    ImGui::End();
    return changed;
}
