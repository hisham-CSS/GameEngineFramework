#pragma once
#include "InspectorPanel.h"
#include "imgui.h"
#include "Engine.h"

static bool DragFloat3(const char* label, float v[3], float speed = 0.1f) {
    return ImGui::DragFloat3(label, v, speed);
}

void InspectorPanel::Draw(entt::registry& reg, entt::entity selected) {
    if (ImGui::Begin("Inspector")) {
        if (selected == entt::null || !reg.valid(selected)) {
            ImGui::TextUnformatted("No entity selected.");
            ImGui::End();
            return;
        }

        if (auto* name = reg.try_get<Name>(selected)) {
            ImGui::InputText("Name", const_cast<char*>(name->value), 0); // simple const-char* is not editable; keep display only or move to std::string if you prefer
            ImGui::Text("ID: %u", (uint32_t)selected);
        }
        else {
            ImGui::Text("ID: %u", (uint32_t)selected);
        }

        if (auto* t = reg.try_get<Transform>(selected)) {
            float pos[3] = { t->position.x, t->position.y, t->position.z };
            float rot[3] = { t->rotation.x, t->rotation.y, t->rotation.z };
            float scl[3] = { t->scale.x,    t->scale.y,    t->scale.z };
            if (DragFloat3("Position", pos)) { t->position = { pos[0],pos[1],pos[2] }; t->dirty = true; }
            if (DragFloat3("Rotation", rot)) { t->rotation = { rot[0],rot[1],rot[2] }; t->dirty = true; }
            if (DragFloat3("Scale", scl)) { t->scale = { scl[0],scl[1],scl[2] }; t->dirty = true; }
        }
        else {
            ImGui::TextUnformatted("Transform: <none>");
        }
    }
    ImGui::End();
}
