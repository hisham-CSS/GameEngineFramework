#pragma once
#include "InspectorPanel.h"
#include "../UndoHistory.h"
#include "imgui.h"
#include "Engine.h"

#include <cstdio>

// Component-based Inspector: an entity shows ONLY the components attached
// to it — each in its own collapsible section with a native close (✕) to
// remove it — plus an Add Component popup for everything missing. This
// mirrors the ECS truth underneath: an absent component occupies zero
// memory (EnTT sparse sets), so what the panel shows is exactly what is
// stored. A default entity is Name + Transform; everything else is opt-in.
// Transform is not removable (everything positional depends on it),
// matching the Unity convention.

static bool DragFloat3(const char* label, float v[3], float speed = 0.1f) {
    return ImGui::DragFloat3(label, v, speed);
}

bool InspectorPanel::Draw(entt::registry& reg, entt::entity selected,
                          UndoHistory& undo, MyCoreEngine::AssetManager* assets) {
    bool shadowsDirty = false;
    if (ImGui::Begin("Inspector")) {
        if (selected == entt::null || !reg.valid(selected)) {
            ImGui::TextUnformatted("No entity selected.");
            ImGui::End();
            return false;
        }

        // Undo bookkeeping for drag/text widgets: capture the pre-edit state
        // when the item activates, push one coalesced entry when it
        // deactivates. Call right after the widget it tracks.
        auto trackItem = [&](const char* label) {
            if (ImGui::IsItemActivated()) undo.beginEdit(reg, selected, label);
            else if (ImGui::IsItemActive()) undo.touchEdit(label); // still alive
            if (ImGui::IsItemDeactivated()) {
                if (ImGui::IsItemDeactivatedAfterEdit()) undo.endEditIf(reg, label);
                else undo.cancelEditIf(label); // activated but nothing changed
            }
        };

        // For ABSOLUTE sliders (SliderFloat): clicking the track jumps the
        // value on the activation frame, BEFORE any begin-edit could run —
        // so the undo "before" must be the value read prior to the widget.
        // We briefly restore it around beginEdit's capture. (Drag widgets
        // are delta-based and don't need this.)
        auto trackSliderItem = [&](const char* label, float& liveValue, float preValue) {
            if (ImGui::IsItemActivated()) {
                const float jumped = liveValue;
                liveValue = preValue;
                undo.beginEdit(reg, selected, label);
                liveValue = jumped;
            }
            else if (ImGui::IsItemActive()) undo.touchEdit(label);
            if (ImGui::IsItemDeactivated()) {
                if (ImGui::IsItemDeactivatedAfterEdit()) undo.endEditIf(reg, label);
                else undo.cancelEditIf(label);
            }
        };

        // ---- entity identity (not a component section) --------------------
        if (auto* name = reg.try_get<Name>(selected)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s", name->value.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) {
                name->value = buf;
            }
            trackItem("Rename");
        }
        ImGui::TextDisabled("Entity ID: %u", (uint32_t)selected);
        ImGui::Spacing();

        // ---- Transform (not removable: everything positional needs it) ----
        if (auto* t = reg.try_get<Transform>(selected)) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (auto* par = reg.try_get<Parent>(selected); par && reg.valid(par->value)) {
                    const char* pname = "(Entity)";
                    if (auto* pn = reg.try_get<Name>(par->value)) pname = pn->value.c_str();
                    ImGui::TextDisabled("Local to parent '%s'", pname);
                }
                float pos[3] = { t->position.x, t->position.y, t->position.z };
                float rot[3] = { t->rotation.x, t->rotation.y, t->rotation.z };
                float scl[3] = { t->scale.x,    t->scale.y,    t->scale.z };
                if (DragFloat3("Position", pos)) { t->position = { pos[0],pos[1],pos[2] }; t->dirty = true; }
                trackItem("Position");
                if (DragFloat3("Rotation", rot)) { t->rotation = { rot[0],rot[1],rot[2] }; t->dirty = true; }
                trackItem("Rotation");
                if (DragFloat3("Scale", scl)) { t->scale = { scl[0],scl[1],scl[2] }; t->dirty = true; }
                trackItem("Scale");
            }
        }

        // ---- Camera -------------------------------------------------------
        if (auto* cam = reg.try_get<CameraComponent>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Camera", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                const float preFov = cam->fovDeg;
                // AlwaysClamp: Ctrl+Click typing must not escape the range —
                // fov outside (0,180) degenerates the projection
                ImGui::SliderFloat("FOV (deg)", &cam->fovDeg, 20.f, 120.f, "%.0f",
                                   ImGuiSliderFlags_AlwaysClamp);
                trackSliderItem("Camera FOV", cam->fovDeg, preFov);

                // clip planes. Widget bounds are FIXED constants and the
                // near < far invariant is enforced in code right after each
                // widget (before trackItem snapshots the edit): dynamic
                // bounds can collapse to v_min == v_max, which our ImGui
                // (1.91.0) treats as UNBOUNDED — clamping silently off —
                // and absolute epsilons round away at large depths. Editing
                // near past far pushes far along (Unity-style).
                const float preNear = cam->nearClip;
                ImGui::DragFloat("Near Clip", &cam->nearClip, 0.01f,
                                 0.001f, 100000.f, "%.3f",
                                 ImGuiSliderFlags_AlwaysClamp);
                cam->nearClip = glm::clamp(cam->nearClip, 0.001f, 100000.f);
                cam->farClip = std::max(cam->farClip, MinFarClipFor(cam->nearClip));
                trackItem("Camera near clip");
                const float preFar = cam->farClip;
                ImGui::DragFloat("Far Clip", &cam->farClip, 1.f,
                                 0.002f, 200000.f, "%.1f",
                                 ImGuiSliderFlags_AlwaysClamp);
                cam->farClip = glm::clamp(cam->farClip,
                                          MinFarClipFor(cam->nearClip), 200000.f);
                trackItem("Camera far clip");

                // lens changes move the view frustum: the CSM cascades fit
                // it, so refit even though no caster/camera moved
                if (cam->fovDeg != preFov || cam->nearClip != preNear ||
                    cam->farClip != preFar) {
                    shadowsDirty = true;
                }

                ImGui::DragInt("Priority", &cam->priority, 0.1f);
                trackItem("Camera priority");
                bool enabled = cam->enabled;
                if (ImGui::Checkbox("Enabled", &enabled)) {
                    undo.record(reg, selected, enabled ? "Enable camera"
                                                       : "Disable camera", [&] {
                        cam->enabled = enabled;
                    });
                }
                ImGui::TextDisabled("Highest-priority enabled camera renders;");
                ImGui::TextDisabled("switches blend via the camera director.");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove camera", [&] {
                    reg.remove<CameraComponent>(selected);
                });
            }
        }

        // ---- Model (owns the AABB, shadow casting, and material overrides) -
        if (reg.any_of<ModelComponent>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Model", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                auto* mc = reg.try_get<ModelComponent>(selected);
                if (mc->model) {
                    ImGui::TextWrapped("Path: %s", mc->model->SourcePath().c_str());
                }
                else {
                    ImGui::TextDisabled("(no model loaded — set a path or use the Assets panel)");
                }
                if (assets) {
                    static char modelPath[260] = "Exported/Model/backpack.obj";
                    ImGui::InputText("##modelpath", modelPath, sizeof(modelPath));
                    ImGui::SameLine();
                    if (ImGui::SmallButton(mc->model ? "Replace" : "Load")) {
                        if (auto model = assets->GetModel(modelPath); model && !model->Meshes().empty()) {
                            undo.record(reg, selected, mc->model ? "Replace model" : "Load model", [&] {
                                reg.emplace_or_replace<ModelComponent>(selected, ModelComponent{ model });
                                reg.emplace_or_replace<AABB>(selected, generateAABB(*model));
                                if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                            });
                            shadowsDirty = true; // swapped caster, clean transform
                            mc = reg.try_get<ModelComponent>(selected);
                        }
                    }
                }

                // shadow casting is a property of rendered geometry — it
                // lives with the model (NoShadow tag underneath)
                bool castsShadows = !reg.any_of<NoShadow>(selected);
                if (ImGui::Checkbox("Casts Shadows", &castsShadows)) {
                    undo.record(reg, selected,
                                castsShadows ? "Enable shadow casting" : "Disable shadow casting", [&] {
                        if (castsShadows) reg.remove<NoShadow>(selected);
                        else if (!reg.any_of<NoShadow>(selected)) reg.emplace<NoShadow>(selected);
                    });
                    shadowsDirty = true; // caster set changed, transform untouched
                }

                // --- material overrides (per model slot) ---
                if (mc && mc->model) {
                    const auto& mats = mc->model->Materials();
                    if (!mats.empty() && ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* overrides = reg.try_get<MaterialOverrides>(selected);
                        for (size_t i = 0; i < mats.size(); ++i) {
                            auto& shared = mats[i];
                            ImGui::PushID((int)i);
                            if (ImGui::TreeNode("Mat", "Material %d", (int)i + 1)) {
                                char lblBase[48], lblEmi[48], lblMet[48], lblRgh[48], lblAO[48];
                                snprintf(lblBase, sizeof(lblBase), "Material %d base color", (int)i + 1);
                                snprintf(lblEmi,  sizeof(lblEmi),  "Material %d emissive",   (int)i + 1);
                                snprintf(lblMet,  sizeof(lblMet),  "Material %d metallic",   (int)i + 1);
                                snprintf(lblRgh,  sizeof(lblRgh),  "Material %d roughness",  (int)i + 1);
                                snprintf(lblAO,   sizeof(lblAO),   "Material %d AO",         (int)i + 1);

                                MyCoreEngine::Material* editing = nullptr;
                                if (overrides) {
                                    auto it = overrides->byIndex.find(i);
                                    if (it != overrides->byIndex.end() && it->second) {
                                        editing = it->second.get();
                                    }
                                }
                                if (!editing) {
                                    // read-only shared summary: editing the shared
                                    // material would silently change every entity
                                    // using this model (and bypass undo) — that's
                                    // what "Make unique" is for
                                    ImGui::TextDisabled("(shared)");
                                    ImGui::BeginDisabled();
                                    float base[3] = { shared->baseColor.r, shared->baseColor.g, shared->baseColor.b };
                                    ImGui::ColorEdit3("Base Color", base, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoInputs);
                                    float emi[3] = { shared->emissive.r,  shared->emissive.g,  shared->emissive.b };
                                    ImGui::ColorEdit3("Emissive", emi, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoInputs);
                                    ImGui::SliderFloat("Metallic", &shared->metallic, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoInput);
                                    ImGui::SliderFloat("Roughness", &shared->roughness, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoInput);
                                    ImGui::SliderFloat("AO", &shared->ao, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_NoInput);
                                    ImGui::EndDisabled();
                                    if (ImGui::Button("Make unique for this entity")) {
                                        char lbl[48];
                                        snprintf(lbl, sizeof(lbl), "Make material %d unique", (int)i + 1);
                                        undo.record(reg, selected, lbl, [&] {
                                            if (!overrides) {
                                                overrides = &reg.emplace<MaterialOverrides>(selected);
                                            }
                                            auto clone = std::make_shared<MyCoreEngine::Material>(*shared);
                                            overrides->byIndex[i] = clone;
                                            editing = clone.get();
                                        });
                                    }
                                }
                                if (editing) {
                                    ImGui::TextDisabled("(override)");
                                    float base[3] = { editing->baseColor.r, editing->baseColor.g, editing->baseColor.b };
                                    if (ImGui::ColorEdit3("Base Color", base)) editing->baseColor = { base[0], base[1], base[2] };
                                    trackItem(lblBase);
                                    float emi[3] = { editing->emissive.r,  editing->emissive.g,  editing->emissive.b };
                                    if (ImGui::ColorEdit3("Emissive", emi))  editing->emissive = { emi[0], emi[1], emi[2] };
                                    trackItem(lblEmi);
                                    const float preMet = editing->metallic;
                                    ImGui::SliderFloat("Metallic", &editing->metallic, 0.0f, 1.0f);
                                    trackSliderItem(lblMet, editing->metallic, preMet);
                                    const float preRgh = editing->roughness;
                                    ImGui::SliderFloat("Roughness", &editing->roughness, 0.0f, 1.0f);
                                    trackSliderItem(lblRgh, editing->roughness, preRgh);
                                    const float preAO = editing->ao;
                                    ImGui::SliderFloat("AO", &editing->ao, 0.0f, 1.0f);
                                    trackSliderItem(lblAO, editing->ao, preAO);
                                    if (ImGui::Button("Revert to shared")) {
                                        char lbl[48];
                                        snprintf(lbl, sizeof(lbl), "Revert material %d to shared", (int)i + 1);
                                        undo.record(reg, selected, lbl, [&] {
                                            overrides->byIndex.erase(i);
                                            if (overrides->byIndex.empty()) {
                                                reg.remove<MaterialOverrides>(selected);
                                                overrides = nullptr;
                                            }
                                        });
                                        editing = nullptr;
                                    }
                                }
                                ImGui::SeparatorText("Textures");
                                const MyCoreEngine::Material* src = editing ? editing : shared.get();
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
                        ImGui::TreePop();
                    }
                }
            }
            if (!keep) {
                undo.record(reg, selected, "Remove model", [&] {
                    reg.remove<ModelComponent>(selected);
                    reg.remove<AABB>(selected);
                });
                shadowsDirty = true; // removed caster leaves a baked shadow
            }
        }

        // ---- Add Component ------------------------------------------------
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Add Component", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            ImGui::OpenPopup("AddComponentPopup");
        }
        if (ImGui::BeginPopup("AddComponentPopup")) {
            int missing = 0;
            if (!reg.any_of<Name>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Name")) {
                    undo.record(reg, selected, "Add name", [&] {
                        reg.emplace<Name>(selected, Name{ "Entity" });
                    });
                }
            }
            if (!reg.any_of<Transform>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Transform")) {
                    undo.record(reg, selected, "Add transform", [&] {
                        reg.emplace<Transform>(selected);
                    });
                }
            }
            if (!reg.any_of<ModelComponent>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Model")) {
                    undo.record(reg, selected, "Add model component", [&] {
                        reg.emplace<ModelComponent>(selected); // empty; load via the section
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
            }
            if (!reg.any_of<CameraComponent>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Camera")) {
                    undo.record(reg, selected, "Add camera", [&] {
                        reg.emplace<CameraComponent>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
            }
            if (missing == 0) ImGui::TextDisabled("(all components added)");
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    return shadowsDirty;
};
