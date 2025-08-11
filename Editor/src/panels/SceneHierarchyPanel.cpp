#pragma once
#include "SceneHierarchyPanel.h"
#include "imgui.h"
#include "Engine.h" // for Name, Transform, etc.

static const char* GetEntityLabel(entt::registry& reg, entt::entity e) {
    if (reg.try_get<Name>(e)) return reg.get<Name>(e).value;
    return "(Entity)";
}

bool SceneHierarchyPanel::Draw(entt::registry& reg, entt::entity& selected) {
    bool changed = false;
    if (ImGui::Begin("Scene Hierarchy")) {
        auto all = reg.view<entt::entity>();
        for (auto e : all) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (e == selected) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)e, flags, "%s [%u]",
                GetEntityLabel(reg, e), (uint32_t)e);

            if (ImGui::IsItemClicked()) { selected = e; changed = true; }
        }
    }
    ImGui::End();
    return changed;
}
