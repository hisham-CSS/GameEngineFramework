#pragma once
#include "SceneHierarchyPanel.h"
#include "../UndoHistory.h"
#include "imgui.h"
#include "Engine.h" // for Name, Transform, Parent, etc.

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

static const char* GetEntityLabel(entt::registry& reg, entt::entity e) {
    if (auto* n = reg.try_get<Name>(e)) return n->value.c_str();
    return "(Entity)";
}

namespace {
    // deferred so tree iteration never mutates the registry mid-walk
    struct PendingAction {
        enum class Kind { None, Delete, CreateChild, Reparent, Unparent } kind = Kind::None;
        entt::entity target = entt::null;
        entt::entity newParent = entt::null;
    };

    constexpr const char* kDragPayload = "CSE_ENTITY";
}

bool SceneHierarchyPanel::Draw(entt::registry& reg, entt::entity& selected, UndoHistory& undo,
                               bool* pOpen) {
    bool changed = false;
    if (ImGui::Begin("Scene Hierarchy", pOpen)) {
        if (ImGui::Button("+ Create Entity")) {
            entt::entity e = reg.create();
            reg.emplace<Name>(e, Name{ "Entity" });
            reg.emplace<Transform>(e);
            undo.recordCreate(reg, e, "Create entity");
            selected = e;
            changed = true;
        }
        ImGui::Separator();

        // children adjacency + roots, derived from the Parent links
        std::unordered_map<entt::entity, std::vector<entt::entity>> children;
        std::vector<entt::entity> roots;
        for (auto e : reg.view<entt::entity>()) {
            const auto* p = reg.try_get<Parent>(e);
            if (p && p->value != entt::null && reg.valid(p->value)) {
                children[p->value].push_back(e);
            }
            else {
                roots.push_back(e);
            }
        }

        PendingAction action;

        // recursive tree draw; each node is a drag source (reparent me) and
        // a drop target (become my child)
        std::function<void(entt::entity)> drawNode = [&](entt::entity e) {
            const auto itc = children.find(e);
            const bool leaf = (itc == children.end() || itc->second.empty());

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (e == selected) flags |= ImGuiTreeNodeFlags_Selected;

            const bool open = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)e, flags, "%s [%u]",
                GetEntityLabel(reg, e), (uint32_t)e);

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                selected = e;
                changed = true;
            }

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(kDragPayload, &e, sizeof(e));
                ImGui::Text("%s", GetEntityLabel(reg, e));
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(kDragPayload)) {
                    action.kind = PendingAction::Kind::Reparent;
                    action.target = *(const entt::entity*)pl->Data;
                    action.newParent = e;
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Create Child")) {
                    action.kind = PendingAction::Kind::CreateChild;
                    action.target = e;
                }
                if (reg.any_of<Parent>(e) && ImGui::MenuItem("Unparent")) {
                    action.kind = PendingAction::Kind::Unparent;
                    action.target = e;
                }
                if (ImGui::MenuItem(leaf ? "Delete Entity" : "Delete Entity (with children)")) {
                    action.kind = PendingAction::Kind::Delete;
                    action.target = e;
                }
                ImGui::EndPopup();
            }

            if (open && !leaf) {
                for (auto c : itc->second) drawNode(c);
                ImGui::TreePop();
            }
        };
        for (auto r : roots) drawNode(r);

        // remaining panel space accepts drops as "make it a root"
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x,
                            std::max(24.f, ImGui::GetContentRegionAvail().y)));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(kDragPayload)) {
                action.kind = PendingAction::Kind::Unparent;
                action.target = *(const entt::entity*)pl->Data;
            }
            ImGui::EndDragDropTarget();
        }

        // apply the deferred action. Validity-check the entities first: the
        // drag payload is a handle copied at drag START, and undo/redo can
        // destroy it before the drop lands.
        if (action.kind != PendingAction::Kind::None && !reg.valid(action.target)) {
            action.kind = PendingAction::Kind::None;
        }
        if (action.kind == PendingAction::Kind::Reparent && !reg.valid(action.newParent)) {
            action.kind = PendingAction::Kind::None;
        }
        switch (action.kind) {
        case PendingAction::Kind::Delete: {
            std::string label = std::string("Delete '") + GetEntityLabel(reg, action.target) + "'";
            undo.recordDelete(reg, action.target, std::move(label)); // snapshots subtree, destroys
            if (!reg.valid(selected)) selected = entt::null;
            changed = true;
            break;
        }
        case PendingAction::Kind::CreateChild: {
            entt::entity e = reg.create();
            reg.emplace<Name>(e, Name{ "Entity" });
            reg.emplace<Transform>(e);
            reg.emplace<Parent>(e, Parent{ action.target });
            undo.recordCreate(reg, e, "Create child entity");
            selected = e;
            changed = true;
            break;
        }
        case PendingAction::Kind::Reparent: {
            // dropping onto the CURRENT parent is a no-op, not a re-decompose
            const auto* curP = reg.try_get<Parent>(action.target);
            const bool sameParent = curP && curP->value == action.newParent;
            if (action.target != action.newParent && !sameParent) {
                std::string label = std::string("Parent '") + GetEntityLabel(reg, action.target) +
                    "' under '" + GetEntityLabel(reg, action.newParent) + "'";
                undo.record(reg, action.target, std::move(label), [&] {
                    // refuses cycles / missing transforms; keeps world pose
                    MyCoreEngine::SetParentKeepWorld(reg, action.target, action.newParent);
                });
                changed = true;
            }
            break;
        }
        case PendingAction::Kind::Unparent: {
            if (reg.any_of<Parent>(action.target)) {
                std::string label = std::string("Unparent '") + GetEntityLabel(reg, action.target) + "'";
                undo.record(reg, action.target, std::move(label), [&] {
                    MyCoreEngine::SetParentKeepWorld(reg, action.target, entt::null);
                });
                changed = true;
            }
            break;
        }
        default: break;
        }
    }
    ImGui::End();
    return changed;
}
