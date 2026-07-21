#pragma once
#include "Engine.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Undo/redo history for editor scene edits (P2-7).
//
// Every entry is a state transition on ONE entity: full before/after
// snapshots of its editor-managed components. Undo applies `before`,
// redo applies `after`; a missing side means "entity does not exist",
// which is how create and delete are expressed. Snapshots deep-copy
// material overrides and reference models by asset path, so a deleted
// entity can be rebuilt from scratch. Restores request the original
// entt handle back (create(hint)), so later history entries keep
// pointing at the right entity across delete/undo cycles.
//
// Continuous edits (gizmo drags, DragFloat/InputText widgets) go
// through beginEdit/endEdit so a whole drag coalesces into a single
// entry; transitions that end up changing nothing are dropped.
//
// Deliberately ImGui-free: unit-testable headless (pass a null
// AssetManager to skip model reloads).

struct EntitySnapshot {
    bool hasName = false;       Name name{};
    bool hasTransform = false;  Transform transform{};
    // A ModelComponent can exist EMPTY (added via the Inspector, model not
    // loaded yet) — presence and path are tracked separately so restores
    // don't silently drop the component.
    bool hasModelComponent = false;
    std::string modelPath;      // empty = component has no model loaded
    bool hasAABB = false;       AABB aabb{ glm::vec3(0.f), glm::vec3(0.f) };
    bool hasOverrides = false;  MaterialOverrides overrides{}; // deep copies
    bool noShadow = false;
    bool hasParent = false;     entt::entity parent = entt::null; // hierarchy link
    bool hasCamera = false;     CameraComponent camera{};
    // Physics. This struct is a CLOSED list: apply() removes any tracked
    // component whose flag is false, and never restores an untracked one, so
    // a component missing here is destroyed by play-stop and undo.
    bool hasLight = false;          LightComponent light{};
    bool hasScript = false;         ScriptComponent script{};
    bool hasRigidBody = false;      RigidBody rigidBody{};
    bool hasBoxCollider = false;    BoxCollider boxCollider{};
    bool hasSphereCollider = false; SphereCollider sphereCollider{};
    bool hasCapsuleCollider = false; CapsuleCollider capsuleCollider{};
    bool hasPlaneCollider = false;  PlaneCollider planeCollider{};
};

class UndoHistory {
public:
    static constexpr size_t kMaxEntries = 100;

    // One undoable step. Most edits touch a single entity; subtree deletes
    // (P2-8) carry one SubOp per deleted entity so undo restores the whole
    // hierarchy in a single Ctrl+Z. ops[0] is the primary entity for display.
    struct SubOp {
        entt::entity entity = entt::null;
        std::optional<EntitySnapshot> before; // nullopt = entity absent
        std::optional<EntitySnapshot> after;
    };
    struct Entry {
        std::string label;
        std::vector<SubOp> ops;
    };

    // --- recording gate (play-in-editor) ------------------------------------
    // While disabled, every record* call still performs its mutation (delete
    // still destroys, record still runs its lambda) but nothing is pushed:
    // play-mode changes are discarded wholesale by the stop-restore, so
    // history entries for them would dangle. Existing entries stay valid
    // because restoreScene rebuilds the pre-play state under the same handles.
    void setRecordingEnabled(bool on) {
        recordingEnabled_ = on;
        if (!on) cancelEdit();
    }
    bool recordingEnabled() const { return recordingEnabled_; }

    // --- continuous edits (drags, text fields) -----------------------------
    // beginEdit captures the pre-edit state once; endEdit captures the
    // post-edit state and pushes. A repeated beginEdit with the same
    // entity+label is a no-op (the drag is already being tracked); a
    // beginEdit for something else finalizes the previous edit first.
    void beginEdit(entt::registry& reg, entt::entity e, std::string label);
    void endEdit(entt::registry& reg);
    void cancelEdit() { pendingActive_ = false; }
    // Variants that only act when `label` matches the tracked edit — for
    // ImGui item lifecycles, where item A's deactivation can be observed
    // after item B already started a new edit in the same frame.
    void endEditIf(entt::registry& reg, const std::string& label);
    void cancelEditIf(const std::string& label);
    bool editActive() const { return pendingActive_; }

    // Liveness protocol: a pending edit must be touched every frame by its
    // widget (trackItem calls touchEdit while the item is active). tickFrame,
    // called once per frame after the UI, commits any edit whose widget
    // stopped being submitted — entity deselected while a text field was
    // focused, window collapsed, dock tab switched, component removed —
    // because its deactivation event can never be observed.
    void touchEdit(const std::string& label);
    void tickFrame(entt::registry& reg);

    // --- instantaneous edits ------------------------------------------------
    // Snapshot, run `mutate`, snapshot again, push (dropped if no change).
    void record(entt::registry& reg, entt::entity e, std::string label,
                const std::function<void()>& mutate);

    // --- entity lifecycle -----------------------------------------------------
    void recordCreate(entt::registry& reg, entt::entity e, std::string label);
    // Snapshots the entity AND its whole descendant subtree (Parent links),
    // then destroys them all; one undo entry restores the hierarchy intact.
    void recordDelete(entt::registry& reg, entt::entity e, std::string label);

    // --- gizmo helper: transform-only change with an explicit "before" -------
    void recordTransformChange(entt::registry& reg, entt::entity e,
                               const Transform& before, std::string label);

    // --- undo/redo -------------------------------------------------------------
    bool canUndo() const { return cursor_ > 0; }
    bool canRedo() const { return cursor_ < entries_.size(); }
    void undo(entt::registry& reg, MyCoreEngine::AssetManager* assets);
    void redo(entt::registry& reg, MyCoreEngine::AssetManager* assets);
    // Undo/redo until exactly `target` entries are applied.
    void jumpTo(entt::registry& reg, MyCoreEngine::AssetManager* assets, size_t target);

    const std::deque<Entry>& entries() const { return entries_; }
    size_t cursor() const { return cursor_; } // entries [0, cursor) are applied
    void clear();

    static EntitySnapshot capture(entt::registry& reg, entt::entity e);
    // Rebuild one entity from a snapshot, resurrecting its original handle
    // if it no longer exists (entt create(hint)).
    static void apply(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                      entt::entity e, const EntitySnapshot& snap);

    // --- whole-scene snapshot (play-in-editor, P2-6) ------------------------
    // captureScene records every entity; restoreScene clears the registry and
    // rebuilds each entity under its ORIGINAL handle, so selection and this
    // undo history stay valid across a play session.
    using SceneSnapshot = std::vector<std::pair<entt::entity, EntitySnapshot>>;
    static SceneSnapshot captureScene(entt::registry& reg);
    static void restoreScene(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                             const SceneSnapshot& snap);

private:
    static void apply_(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                       entt::entity e, const std::optional<EntitySnapshot>& snap);
    static bool same_(const std::optional<EntitySnapshot>& a,
                      const std::optional<EntitySnapshot>& b);
    // Re-emplaces Parent links for one applied entry side: during a batch
    // restore a child can be rebuilt before its parent exists, so apply_
    // drops the link and this pass repairs it once every entity is back.
    static void fixupParents_(entt::registry& reg, const Entry& en, bool beforeSide);
    void push_(Entry entry);

    std::deque<Entry> entries_;
    size_t cursor_ = 0;
    bool recordingEnabled_ = true;

    bool pendingActive_ = false;
    bool pendingTouched_ = false;
    entt::entity pendingEntity_ = entt::null;
    std::string pendingLabel_;
    EntitySnapshot pendingBefore_{};
};
