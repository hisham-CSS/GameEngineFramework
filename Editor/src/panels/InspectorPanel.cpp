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

        // --- Materials ---
        if (auto* mc = reg.try_get<ModelComponent>(selected)) {
            if (mc->model) {
                const auto& mats = mc->model->Materials();
                if (!mats.empty() && ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (size_t i = 0; i < mats.size(); ++i) {
                        auto& mat = mats[i];
                        ImGui::PushID((int)i);
                        if (ImGui::TreeNode("Mat", "Material %d", (int)i + 1)) {
                            float base[3] = { mat->baseColor.r, mat->baseColor.g, mat->baseColor.b };
                            if (ImGui::ColorEdit3("Base Color", base)) mat->baseColor = { base[0], base[1], base[2] };
                            float emi[3] = { mat->emissive.r,  mat->emissive.g,  mat->emissive.b };
                            if (ImGui::ColorEdit3("Emissive", emi))  mat->emissive = { emi[0], emi[1], emi[2] };
                            ImGui::SliderFloat("Metallic", &mat->metallic, 0.0f, 1.0f);
                            ImGui::SliderFloat("Roughness", &mat->roughness, 0.0f, 1.0f);
                            ImGui::SliderFloat("AO", &mat->ao, 0.0f, 1.0f);

                            ImGui::SeparatorText("Textures");
                            ImGui::Text("Albedo:    %s", mat->hasAlbedo() ? "yes" : "no");
                            ImGui::Text("Normal:    %s", mat->hasNormal() ? "yes" : "no");
                            ImGui::Text("Metallic:  %s", mat->hasMetallic() ? "yes" : "no");
                            ImGui::Text("Roughness: %s", mat->hasRoughness() ? "yes" : "no");
                            ImGui::Text("AO:        %s", mat->hasAO() ? "yes" : "no");
                            ImGui::Text("Emissive:  %s", mat->hasEmissive() ? "yes" : "no");

                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
    }
    ImGui::End();
}
