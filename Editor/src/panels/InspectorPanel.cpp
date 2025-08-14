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
                    // fetch existing overrides (if any)
                    auto * overrides = reg.try_get<MaterialOverrides>(selected);
                    for (size_t i = 0; i < mats.size(); ++i) {
                        //auto& mat = mats[i];
                        auto& shared = mats[i];
                        ImGui::PushID((int)i);
                        /*if (ImGui::TreeNode("Mat", "Material %d", (int)i + 1)) {
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
                            ImGui::Text("Emissive:  %s", mat->hasEmissive() ? "yes" : "no");*/
                        if (ImGui::TreeNode("Mat", "Material %d", (int)i + 1)) {
                            MyCoreEngine::Material * editing = nullptr;
                            bool isOverride = false;
                            if (overrides) {
                                auto it = overrides->byIndex.find(i);
                                if (it != overrides->byIndex.end() && it->second) {
                                    editing = it->second.get();
                                    isOverride = true;
                                }
                            }
                            if (!editing) {
                            // show shared summary + button to make unique
                                ImGui::TextDisabled("(shared)");
                                float base[3] = { shared->baseColor.r, shared->baseColor.g, shared->baseColor.b };
                                ImGui::ColorEdit3("Base Color", base, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoInputs);
                                float emi[3] = { shared->emissive.r,  shared->emissive.g,  shared->emissive.b };
                                ImGui::ColorEdit3("Emissive", emi, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoInputs);
                                ImGui::SliderFloat("Metallic", &shared->metallic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoInput);
                                ImGui::SliderFloat("Roughness", &shared->roughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoInput);
                                ImGui::SliderFloat("AO", &shared->ao, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoInput);
                                if (ImGui::Button("Make unique for this entity")) {
                                    if (!overrides) {
                                        overrides = &reg.emplace<MaterialOverrides>(selected);
                                    }
                                    auto clone = std::make_shared<MyCoreEngine::Material>(*shared);
                                    overrides->byIndex[i] = clone;
                                    editing = clone.get();
                                    isOverride = true;
                                }
                            }
                            if (editing) {
                                ImGui::TextDisabled("(override)");
                                float base[3] = { editing->baseColor.r, editing->baseColor.g, editing->baseColor.b };
                                if (ImGui::ColorEdit3("Base Color", base)) editing->baseColor = { base[0], base[1], base[2] };
                                float emi[3] = { editing->emissive.r,  editing->emissive.g,  editing->emissive.b };
                                if (ImGui::ColorEdit3("Emissive", emi))  editing->emissive = { emi[0], emi[1], emi[2] };
                                ImGui::SliderFloat("Metallic", &editing->metallic, 0.0f, 1.0f);
                                ImGui::SliderFloat("Roughness", &editing->roughness, 0.0f, 1.0f);
                                ImGui::SliderFloat("AO", &editing->ao, 0.0f, 1.0f);
                                if (ImGui::Button("Revert to shared")) {
                                    overrides->byIndex.erase(i);
                                    if (overrides->byIndex.empty()) reg.remove<MaterialOverrides>(selected);
                                    editing = nullptr;
                                    isOverride = false;
                                }
                            }
                            ImGui::SeparatorText("Textures");
                            const MyCoreEngine::Material * src = editing ? editing : shared.get();
                            ImGui::Text("Albedo:    %s", src->hasAlbedo() ? "yes" : "no");
                            ImGui::Text("Normal:    %s", src->hasNormal() ? "yes" : "no");
                            ImGui::Text("Metallic:  %s", src->hasMetallic() ? "yes" : "no");
                            ImGui::Text("Roughness: %s", src->hasRoughness() ? "yes" : "no");
                            ImGui::Text("AO:        %s", src->hasAO() ? "yes" : "no");
                            ImGui::Text("Emissive:  %s", src->hasEmissive() ? "yes" : "no");
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
    }
    ImGui::End();
};
