#pragma once
#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include "Engine.h" // for Name, Transform, etc.

static const char* GetEntityLabel(entt::registry& reg, entt::entity e) {
    if (auto* n = reg.try_get<Name>(e)) return n->value.c_str();
    return "(Entity)";
}

bool SceneHierarchyPanel::Draw(entt::registry& reg, entt::entity& selected) {
    bool changed = false;
    if (ImGui::Begin("Scene Hierarchy")) {
        if (ImGui::Button("+ Create Entity")) {
            entt::entity e = reg.create();
            reg.emplace<Name>(e, Name{ "Entity" });
            reg.emplace<Transform>(e);
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
            reg.destroy(toDelete);
            if (selected == toDelete) selected = entt::null;
            changed = true;
        }
    }
    ImGui::End();
    return changed;
}
