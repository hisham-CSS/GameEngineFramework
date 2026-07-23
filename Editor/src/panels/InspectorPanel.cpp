#pragma once
#include "InspectorPanel.h"
#include "../UndoHistory.h"
#include "imgui.h"
#include "Engine.h"

#include <cstdio>
#include <filesystem>

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
                          UndoHistory& undo, MyCoreEngine::AssetManager* assets,
                          const MyCoreEngine::ScriptWorld* scripts,
                          MyCoreEngine::AudioWorld* audio, bool* pOpen) {
    bool shadowsDirty = false;

    // Stop an orphaned preview voice the instant its Audio Source section will
    // no longer be drawn — selection moved, the entity was deleted (stale
    // handle), or the component was removed. Without this the Stop button
    // vanishes with a still-playing voice. (Previews are also one-shot below,
    // so anything this misses — e.g. the asset-view path — self-reaps anyway.)
    if (previewVoice_) {
        const bool ownerStillDraws =
            previewEntity_ == selected && reg.valid(previewEntity_) &&
            reg.any_of<MyCoreEngine::AudioSourceComponent>(previewEntity_);
        if (!ownerStillDraws && audio && audio->Backend()) {
            audio->Backend()->stop(previewVoice_);
            previewVoice_ = 0;
            previewEntity_ = entt::null;
        }
    }
    if (ImGui::Begin("Inspector", pOpen)) {
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
                    // Only show material slots that meshes actually USE. Import
                    // often leaves a spurious empty default material (e.g.
                    // Assimp adds one for OBJ) that no mesh references -- listing
                    // it as "Material 1" is pure confusion: making it unique or
                    // editing it has no visible effect because nothing renders
                    // with it. Slots are relabelled with a running 1-based
                    // number over the USED ones, so the first real material is
                    // "Material 1" even if its import slot is 1.
                    std::vector<bool> slotUsed(mats.size(), false);
                    for (const auto& mesh : mc->model->Meshes()) {
                        const size_t s = mesh.MaterialIndex();
                        if (s < slotUsed.size()) slotUsed[s] = true;
                    }
                    if (!mats.empty() && ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto* overrides = reg.try_get<MaterialOverrides>(selected);
                        int shownNo = 0; // friendly 1-based number over used slots
                        for (size_t i = 0; i < mats.size(); ++i) {
                            if (!slotUsed[i]) continue; // hide phantom/unused slots
                            auto& shared = mats[i];
                            ++shownNo;
                            ImGui::PushID((int)i);
                            if (ImGui::TreeNode("Mat", "Material %d", shownNo)) {
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
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip(
                                            "TINT, multiplied over the albedo texture -- not a fill colour.\n"
                                            "White (1,1,1) leaves the texture untouched; black makes the whole\n"
                                            "material black. It applies to EVERY mesh that uses this material,\n"
                                            "so on a single-material model it recolours the whole object.");
                                    }
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

                                    // --- Shading model (per-material) ---
                                    const char* kShading[] = { "PBR", "Toon (cel)" };
                                    int sm = static_cast<int>(editing->shadingModel);
                                    if (ImGui::Combo("Shading", &sm, kShading, IM_ARRAYSIZE(kShading))) {
                                        char lbl[48];
                                        snprintf(lbl, sizeof(lbl), "Material %d shading", (int)i + 1);
                                        undo.record(reg, selected, lbl, [&] {
                                            editing->shadingModel = static_cast<MyCoreEngine::ShadingModel>(sm);
                                        });
                                    }
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip("Toon: banded cel shading + hard specular + rim light.\n"
                                                          "Pair it with the Ink outline post effect\n"
                                                          "(Settings > Rendering > Post & Toggles > Post-process).");
                                    }

                                    // Shader-specific settings: shown only for the active shading
                                    // model, so the panel stays relevant to what's selected.
                                    if (editing->shadingModel == MyCoreEngine::ShadingModel::Toon) {
                                        ImGui::SeparatorText("Toon settings");
                                        int tb = editing->toonBands;
                                        if (ImGui::SliderInt("Bands##toon", &tb, 2, 8)) {
                                            char lbl[52]; snprintf(lbl, sizeof(lbl), "Material %d toon bands", (int)i + 1);
                                            undo.record(reg, selected, lbl, [&] { editing->toonBands = tb; });
                                        }
                                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Number of hard diffuse steps.");

                                        const float preSS = editing->toonSpecStrength;
                                        ImGui::SliderFloat("Specular##toon", &editing->toonSpecStrength, 0.0f, 1.0f);
                                        char lS[52]; snprintf(lS, sizeof(lS), "Material %d toon specular", (int)i + 1);
                                        trackSliderItem(lS, editing->toonSpecStrength, preSS);

                                        const float preSZ = editing->toonSpecSize;
                                        ImGui::SliderFloat("Spec size##toon", &editing->toonSpecSize, 0.0f, 1.0f);
                                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = tiny sharp dot, 1 = broad soft sheen.");
                                        char lZ[52]; snprintf(lZ, sizeof(lZ), "Material %d toon spec size", (int)i + 1);
                                        trackSliderItem(lZ, editing->toonSpecSize, preSZ);

                                        const float preRim = editing->toonRimStrength;
                                        ImGui::SliderFloat("Rim##toon", &editing->toonRimStrength, 0.0f, 1.0f);
                                        char lR[52]; snprintf(lR, sizeof(lR), "Material %d toon rim", (int)i + 1);
                                        trackSliderItem(lR, editing->toonRimStrength, preRim);
                                    }

                                    // --- Transparency ---
                                    const char* kAlpha[] = { "Opaque", "Mask (cutout)", "Blend" };
                                    int am = static_cast<int>(editing->alphaMode);
                                    if (ImGui::Combo("Alpha mode", &am, kAlpha, IM_ARRAYSIZE(kAlpha))) {
                                        char lbl[48];
                                        snprintf(lbl, sizeof(lbl), "Material %d alpha mode", (int)i + 1);
                                        undo.record(reg, selected, lbl, [&] {
                                            editing->alphaMode = static_cast<MyCoreEngine::AlphaMode>(am);
                                        });
                                    }
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip("Blend: true translucency (glass/water), sorted and composited.\n"
                                                          "Mask: hard cutout (foliage) via the albedo alpha channel.\n"
                                                          "Both need an albedo texture with an alpha channel to vary per-pixel.");
                                    }
                                    if (editing->alphaMode == MyCoreEngine::AlphaMode::Blend) {
                                        const float preOp = editing->opacity;
                                        ImGui::SliderFloat("Opacity", &editing->opacity, 0.0f, 1.0f);
                                        char lbl[48]; snprintf(lbl, sizeof(lbl), "Material %d opacity", (int)i + 1);
                                        trackSliderItem(lbl, editing->opacity, preOp);
                                    }
                                    if (editing->alphaMode == MyCoreEngine::AlphaMode::Mask) {
                                        const float preCut = editing->alphaCutoff;
                                        ImGui::SliderFloat("Cutoff", &editing->alphaCutoff, 0.0f, 1.0f);
                                        char lbl[48]; snprintf(lbl, sizeof(lbl), "Material %d cutoff", (int)i + 1);
                                        trackSliderItem(lbl, editing->alphaCutoff, preCut);
                                    }
                                    if (editing->alphaMode != MyCoreEngine::AlphaMode::Opaque) {
                                        bool ds = editing->doubleSided;
                                        if (ImGui::Checkbox("Double sided", &ds)) {
                                            char lbl[52]; snprintf(lbl, sizeof(lbl), "Material %d double sided", (int)i + 1);
                                            undo.record(reg, selected, lbl, [&] { editing->doubleSided = ds; });
                                        }
                                    }

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

        // ---- Light ---------------------------------------------------------
        if (auto* lc = reg.try_get<LightComponent>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Light", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* kTypes[] = { "Point", "Spot" };
                int type = static_cast<int>(lc->type);
                if (ImGui::Combo("Type", &type, kTypes, IM_ARRAYSIZE(kTypes))) {
                    undo.record(reg, selected, "Change light type", [&] {
                        reg.get<LightComponent>(selected).type = static_cast<LightType>(type);
                    });
                }

                bool on = lc->enabled;
                if (ImGui::Checkbox("Enabled", &on)) {
                    undo.record(reg, selected, "Toggle light", [&] {
                        reg.get<LightComponent>(selected).enabled = on;
                    });
                }

                ImGui::ColorEdit3("Color", &lc->color.x);
                trackItem("Change light color");
                ImGui::DragFloat("Intensity", &lc->intensity, 0.25f, 0.f, 10000.f);
                trackItem("Change light intensity");
                ImGui::DragFloat("Range", &lc->range, 0.25f, 0.f, 10000.f, "%.2f m");
                trackItem("Change light range");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Distance at which the light reaches zero.\n"
                                      "Also the cull bound — smaller is cheaper.");
                }

                if (lc->type == LightType::Spot) {
                    ImGui::DragFloat("Inner angle", &lc->innerAngleDeg, 0.5f, 0.f, 89.9f, "%.1f deg");
                    trackItem("Change spot inner angle");
                    ImGui::DragFloat("Outer angle", &lc->outerAngleDeg, 0.5f, 0.f, 89.9f, "%.1f deg");
                    trackItem("Change spot outer angle");
                    // An outer cone smaller than the inner makes the falloff run
                    // backwards and lights everything OUTSIDE the cone instead.
                    if (lc->outerAngleDeg < lc->innerAngleDeg) {
                        ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
                                           "Outer angle is clamped to the inner angle.");
                    }
                    ImGui::TextDisabled("Aims down the entity's -Z axis.");
                }

                ImGui::TextDisabled("Unshadowed. The sun (Settings > Sun) casts shadows.");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove light",
                            [&] { reg.remove<LightComponent>(selected); });
            }
        }

        // ---- Script --------------------------------------------------------
        if (auto* sc = reg.try_get<MyCoreEngine::ScriptComponent>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Script", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                char buf[260];
                std::snprintf(buf, sizeof(buf), "%s", sc->path.c_str());
                if (ImGui::InputText("File", buf, sizeof(buf))) {
                    sc->path = buf;
                }
                trackItem("Change script file");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Path relative to the project's Scripts folder,\n"
                                      "e.g. spinner.lua");
                }

                bool on = sc->enabled;
                if (ImGui::Checkbox("Enabled", &on)) {
                    undo.record(reg, selected, "Toggle script", [&] {
                        reg.get<MyCoreEngine::ScriptComponent>(selected).enabled = on;
                    });
                }

                // Surface this entity's compile/runtime error HERE. A script
                // error that only reaches the console is easy to miss while
                // you are looking straight at the field that caused it.
                if (scripts) {
                    for (const auto& st : scripts->Statuses()) {
                        if (st.entity != selected) continue;
                        if (st.failed) {
                            ImGui::TextColored(ImVec4(1.f, 0.35f, 0.3f, 1.f), "Error");
                            ImGui::TextWrapped("%s", st.error.c_str());
                        } else if (st.loaded) {
                            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.f), "Loaded");
                        }
                        break;
                    }
                }
                ImGui::TextDisabled("Hooks: OnStart, OnUpdate(dt),\n"
                                    "OnFixedUpdate(dt), OnCollision(c), OnDestroy");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove script",
                            [&] { reg.remove<MyCoreEngine::ScriptComponent>(selected); });
            }
        }

        // ---- Audio Source --------------------------------------------------
        if (auto* as = reg.try_get<MyCoreEngine::AudioSourceComponent>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Audio Source", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                char buf[260];
                std::snprintf(buf, sizeof(buf), "%s", as->clip.c_str());
                if (ImGui::InputText("Clip", buf, sizeof(buf))) {
                    as->clip = buf;
                }
                trackItem("Change audio clip");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Sound file path relative to the working dir,\n"
                                      "e.g. Exported/Audio/footstep.wav (WAV/MP3/FLAC/OGG).");
                }

                const float preVol = as->volume;
                ImGui::SliderFloat("Volume", &as->volume, 0.0f, 1.0f);
                trackSliderItem("Change audio volume", as->volume, preVol);

                const float prePitch = as->pitch;
                ImGui::SliderFloat("Pitch", &as->pitch, 0.25f, 4.0f, "%.2fx",
                                   ImGuiSliderFlags_AlwaysClamp);
                trackSliderItem("Change audio pitch", as->pitch, prePitch);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also scales playback speed.");

                bool loop = as->loop;
                if (ImGui::Checkbox("Loop", &loop)) {
                    undo.record(reg, selected, "Toggle audio loop", [&] {
                        reg.get<MyCoreEngine::AudioSourceComponent>(selected).loop = loop;
                    });
                }
                ImGui::SameLine();
                bool playOnStart = as->playOnStart;
                if (ImGui::Checkbox("Play on start", &playOnStart)) {
                    undo.record(reg, selected, "Toggle play on start", [&] {
                        reg.get<MyCoreEngine::AudioSourceComponent>(selected).playOnStart = playOnStart;
                    });
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Begins automatically on Play / when the shipped game boots.");
                }

                bool spatial = as->spatial;
                if (ImGui::Checkbox("Spatial (3D)", &spatial)) {
                    undo.record(reg, selected, "Toggle audio spatial", [&] {
                        reg.get<MyCoreEngine::AudioSourceComponent>(selected).spatial = spatial;
                    });
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("3D: attenuates with distance from the listener.\n"
                                      "2D: constant volume, ignores position (music/UI).");
                }

                // Distance falloff only means anything for a 3D source.
                if (as->spatial) {
                    ImGui::DragFloat("Min distance", &as->minDistance, 0.05f, 0.f, 100000.f, "%.2f m");
                    as->minDistance = std::max(0.f, as->minDistance);
                    trackItem("Change audio min distance");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Full volume within this radius.");
                    ImGui::DragFloat("Max distance", &as->maxDistance, 0.25f, 0.f, 100000.f, "%.2f m");
                    // keep max strictly past min so the falloff span never collapses
                    as->maxDistance = std::max(as->maxDistance, as->minDistance + 1e-3f);
                    trackItem("Change audio max distance");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Attenuated to silence out here.");
                }

                // Preview auditions the clip through the editor's always-on
                // audio backend, so no Play press is needed. Forced to 2D (so
                // it is always audible regardless of the entity's distance from
                // the listener) and one-shot (so an orphaned audition always
                // self-reaps even if this panel stops being drawn). The Stop
                // button just cuts a long audition short.
                ImGui::Spacing();
                if (audio && audio->HasBackend()) {
                    const bool playing = previewVoice_ != 0 && previewEntity_ == selected &&
                                         audio->Backend() && audio->Backend()->isPlaying(previewVoice_);
                    if (!playing && previewVoice_ != 0 && previewEntity_ == selected) {
                        // The audition finished on its own. In EDIT mode the
                        // AudioWorld::Update reaper never runs (it's gated on
                        // gameplay), so the backend won't free this voice for us
                        // — stop() it explicitly (safe on an already-finished id)
                        // or the decoded buffer leaks until the next stopAll().
                        if (auto* b = audio->Backend()) b->stop(previewVoice_);
                        previewVoice_ = 0;
                    }
                    ImGui::BeginDisabled(as->clip.empty() && !playing);
                    if (ImGui::Button(playing ? "Stop##audiopreview" : "Preview##audiopreview",
                                      ImVec2(90, 0))) {
                        if (auto* b = audio->Backend()) {
                            if (previewVoice_) { b->stop(previewVoice_); previewVoice_ = 0; }
                            if (!playing && !as->clip.empty()) {
                                MyCoreEngine::SoundParams p;
                                p.spatial = false;     // audition 2D so it's always heard
                                p.loop    = false;     // one-shot: an orphan self-reaps
                                p.volume  = as->volume;
                                p.pitch   = as->pitch;
                                previewVoice_  = b->play(as->clip, p);
                                previewEntity_ = selected;
                            }
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextDisabled("Auditions the clip (2D) in the editor.");
                } else {
                    ImGui::TextDisabled("No audio backend available for preview.");
                }
            }
            if (!keep) {
                // Stop a preview owned by the component being removed.
                if (previewVoice_ && previewEntity_ == selected && audio && audio->Backend()) {
                    audio->Backend()->stop(previewVoice_);
                    previewVoice_ = 0; previewEntity_ = entt::null;
                }
                undo.record(reg, selected, "Remove audio source",
                            [&] { reg.remove<MyCoreEngine::AudioSourceComponent>(selected); });
            }
        }

        // ---- Audio Listener (tag: the "ears", usually the camera) ----------
        if (reg.any_of<MyCoreEngine::AudioListenerComponent>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Audio Listener", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("This entity's transform is the audio listener.");
                ImGui::TextDisabled("With none in the scene, the render camera is used.");
                ImGui::TextDisabled("The first listener found wins.");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove audio listener",
                            [&] { reg.remove<MyCoreEngine::AudioListenerComponent>(selected); });
            }
        }

        // ---- Rigid Body ---------------------------------------------------
        if (auto* rb = reg.try_get<RigidBody>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Rigid Body", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* kTypes[] = { "Static", "Kinematic", "Dynamic" };
                int type = static_cast<int>(rb->type);
                if (ImGui::Combo("Body type", &type, kTypes, IM_ARRAYSIZE(kTypes))) {
                    undo.record(reg, selected, "Change body type", [&] {
                        reg.get<RigidBody>(selected).type = static_cast<BodyType>(type);
                    });
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Static: never moves (level geometry)\n"
                                      "Kinematic: moved by code, pushes dynamics\n"
                                      "Dynamic: fully simulated");
                }

                // Dynamic-only tuning: mass/damping/velocity mean nothing on a
                // static body, so don't offer knobs that silently do nothing.
                if (rb->type == BodyType::Dynamic) {
                    ImGui::DragFloat("Mass", &rb->mass, 0.05f, 0.0f, 10000.f, "%.3f kg");
                    trackItem("Change mass");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("0 = let the backend compute it from the shape");
                    }
                    ImGui::DragFloat3("Initial velocity", &rb->initialLinearVelocity.x, 0.05f);
                    trackItem("Change initial velocity");
                    ImGui::DragFloat("Linear damping", &rb->linearDamping, 0.005f, 0.f, 10.f);
                    trackItem("Change linear damping");
                    ImGui::DragFloat("Angular damping", &rb->angularDamping, 0.005f, 0.f, 10.f);
                    trackItem("Change angular damping");
                }

                const float preFric = rb->friction;
                ImGui::SliderFloat("Friction", &rb->friction, 0.0f, 1.0f);
                trackSliderItem("Change friction", rb->friction, preFric);
                const float preRest = rb->restitution;
                ImGui::SliderFloat("Restitution", &rb->restitution, 0.0f, 1.0f);
                trackSliderItem("Change restitution", rb->restitution, preRest);

                bool trig = rb->isTrigger;
                if (ImGui::Checkbox("Is trigger", &trig)) {
                    undo.record(reg, selected, "Toggle trigger", [&] {
                        reg.get<RigidBody>(selected).isTrigger = trig;
                    });
                }

                if (!reg.any_of<BoxCollider, SphereCollider, CapsuleCollider, PlaneCollider>(selected)) {
                    ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
                                       "No collider: this body will be skipped.");
                }
                ImGui::TextDisabled("Simulated on Play (edit mode is static).");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove rigid body", [&] {
                    reg.remove<RigidBody>(selected);
                });
            }
        }

        // ---- Colliders ----------------------------------------------------
        if (auto* c = reg.try_get<BoxCollider>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Box Collider", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Half extents", &c->halfExtents.x, 0.01f, 0.001f, 1000.f);
                trackItem("Change box extents");
                ImGui::DragFloat3("Offset##box", &c->offset.x, 0.01f);
                trackItem("Change box offset");
                ImGui::TextDisabled("Scaled by the entity's Transform scale.");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove box collider",
                            [&] { reg.remove<BoxCollider>(selected); });
            }
        }
        if (auto* c = reg.try_get<SphereCollider>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Sphere Collider", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Radius##sph", &c->radius, 0.01f, 0.001f, 1000.f);
                trackItem("Change sphere radius");
                ImGui::DragFloat3("Offset##sph", &c->offset.x, 0.01f);
                trackItem("Change sphere offset");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove sphere collider",
                            [&] { reg.remove<SphereCollider>(selected); });
            }
        }
        if (auto* c = reg.try_get<CapsuleCollider>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Capsule Collider", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Radius##cap", &c->radius, 0.01f, 0.001f, 1000.f);
                trackItem("Change capsule radius");
                ImGui::DragFloat("Half height", &c->halfHeight, 0.01f, 0.0001f, 1000.f);
                trackItem("Change capsule half height");
                ImGui::DragFloat3("Offset##cap", &c->offset.x, 0.01f);
                trackItem("Change capsule offset");
                ImGui::TextDisabled("Y-up; half height excludes the caps.");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove capsule collider",
                            [&] { reg.remove<CapsuleCollider>(selected); });
            }
        }
        if (auto* c = reg.try_get<PlaneCollider>(selected)) {
            bool keep = true;
            if (ImGui::CollapsingHeader("Plane Collider", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Offset##pln", &c->offset.x, 0.01f);
                trackItem("Change plane offset");
                ImGui::TextDisabled("Infinite ground plane; use on a Static body.");
            }
            if (!keep) {
                undo.record(reg, selected, "Remove plane collider",
                            [&] { reg.remove<PlaneCollider>(selected); });
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
            if (!reg.any_of<LightComponent>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Light")) {
                    undo.record(reg, selected, "Add light", [&] {
                        reg.emplace<LightComponent>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
            }
            if (!reg.any_of<MyCoreEngine::ScriptComponent>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Script")) {
                    undo.record(reg, selected, "Add script", [&] {
                        reg.emplace<MyCoreEngine::ScriptComponent>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
            }
            if (!reg.any_of<MyCoreEngine::AudioSourceComponent>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Audio Source")) {
                    undo.record(reg, selected, "Add audio source", [&] {
                        reg.emplace<MyCoreEngine::AudioSourceComponent>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
            }
            if (!reg.any_of<MyCoreEngine::AudioListenerComponent>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Audio Listener")) {
                    undo.record(reg, selected, "Add audio listener", [&] {
                        reg.emplace<MyCoreEngine::AudioListenerComponent>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
            }
            if (!reg.any_of<RigidBody>(selected)) {
                ++missing;
                if (ImGui::MenuItem("Rigid Body")) {
                    undo.record(reg, selected, "Add rigid body", [&] {
                        reg.emplace<RigidBody>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
            }
            // Colliders are mutually exclusive: PhysicsWorld uses the FIRST
            // shape it finds, so offering a second one would silently do
            // nothing. Only advertise them when the entity has none.
            if (!reg.any_of<BoxCollider, SphereCollider, CapsuleCollider, PlaneCollider>(selected)) {
                missing += 4;
                if (ImGui::MenuItem("Box Collider")) {
                    undo.record(reg, selected, "Add box collider", [&] {
                        reg.emplace<BoxCollider>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
                if (ImGui::MenuItem("Sphere Collider")) {
                    undo.record(reg, selected, "Add sphere collider", [&] {
                        reg.emplace<SphereCollider>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
                if (ImGui::MenuItem("Capsule Collider")) {
                    undo.record(reg, selected, "Add capsule collider", [&] {
                        reg.emplace<CapsuleCollider>(selected);
                        if (!reg.any_of<Transform>(selected)) reg.emplace<Transform>(selected);
                    });
                }
                if (ImGui::MenuItem("Plane Collider")) {
                    undo.record(reg, selected, "Add plane collider", [&] {
                        reg.emplace<PlaneCollider>(selected);
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

void InspectorPanel::DrawAsset(const void* indexNode, bool* pOpen) {
    using MyCoreEngine::AssetIndex;
    const auto& node = *static_cast<const AssetIndex::Node*>(indexNode);

    if (node.relPath != assetPath_) {
        // highlighted asset changed: refresh the cached file info + settings
        assetPath_ = node.relPath;
        assetStatus_.clear();
        std::error_code ec;
        const auto sz = std::filesystem::file_size(node.relPath, ec);
        assetSize_ = ec ? 0 : sz;
        importMaxDim_ = MyCoreEngine::LoadImportSettings(node.relPath).maxDimension;
    }

    if (ImGui::Begin("Inspector", pOpen)) {
        ImGui::TextWrapped("%s", node.name.c_str());
        const char* kindLabel = "Asset";
        switch (node.kind) {
        case AssetIndex::Kind::Model:     kindLabel = "Model"; break;
        case AssetIndex::Kind::Texture:   kindLabel = "Texture"; break;
        case AssetIndex::Kind::SceneJson: kindLabel = "Scene"; break;
        case AssetIndex::Kind::Shader:    kindLabel = "Shader"; break;
        default: break;
        }
        if (assetSize_ >= 1024 * 1024) {
            ImGui::TextDisabled("%s - %.1f MB", kindLabel, (double)assetSize_ / (1024.0 * 1024.0));
        }
        else {
            ImGui::TextDisabled("%s - %.1f KB", kindLabel, (double)assetSize_ / 1024.0);
        }
        ImGui::TextDisabled("%s", node.relPath.c_str());
        if (ImGui::SmallButton("Copy Path")) ImGui::SetClipboardText(node.relPath.c_str());
        ImGui::Separator();

        if (node.kind == AssetIndex::Kind::Texture) {
            if (ImGui::CollapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                // the sidecar seam (P4-3 phase 4): one real setting today,
                // enforced by AssetCooker validate; the P4-2 texture cook
                // will apply it. More settings land with the import
                // pipeline (see ImportSettings.h).
                static const int kDims[] = { 0, 512, 1024, 2048, 4096 };
                static const char* kLabels[] = { "Unlimited", "512", "1024", "2048", "4096" };
                int current = -1;
                for (int i = 0; i < 5; ++i) {
                    if (kDims[i] == importMaxDim_) { current = i; break; }
                }
                // a hand-edited sidecar can hold any value: show it honestly
                // instead of masquerading as "Unlimited"
                char preview[32];
                if (current >= 0) std::snprintf(preview, sizeof(preview), "%s", kLabels[current]);
                else std::snprintf(preview, sizeof(preview), "%d (custom)", importMaxDim_);
                if (ImGui::BeginCombo("Max Dimension", preview)) {
                    for (int i = 0; i < 5; ++i) {
                        if (ImGui::Selectable(kLabels[i], i == current)) {
                            MyCoreEngine::ImportSettings s;
                            s.maxDimension = kDims[i];
                            if (MyCoreEngine::SaveImportSettings(node.relPath, s)) {
                                importMaxDim_ = kDims[i];
                                assetStatus_ = "saved";
                            }
                            else {
                                // revert the UI to what's actually on disk
                                importMaxDim_ =
                                    MyCoreEngine::LoadImportSettings(node.relPath).maxDimension;
                                assetStatus_ = "save FAILED (file not writable?)";
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled("Enforced by AssetCooker validate;");
                ImGui::TextDisabled("the texture cook (P4-2) will apply it.");
                if (!assetStatus_.empty()) ImGui::TextDisabled("%s", assetStatus_.c_str());
            }
        }
        else {
            ImGui::TextDisabled("No import settings for this asset type yet.");
        }
    }
    ImGui::End();
}
