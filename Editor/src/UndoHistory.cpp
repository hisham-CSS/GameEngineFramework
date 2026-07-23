#include "UndoHistory.h"

namespace {

    bool materialEq(const MyCoreEngine::Material& a, const MyCoreEngine::Material& b) {
        // scalar params only: the editor never edits texture bindings
        return a.baseColor == b.baseColor && a.emissive == b.emissive &&
               a.metallic == b.metallic && a.roughness == b.roughness && a.ao == b.ao;
    }

    bool snapEq(const EntitySnapshot& a, const EntitySnapshot& b) {
        if (a.hasName != b.hasName) return false;
        if (a.hasName && a.name.value != b.name.value) return false;
        if (a.hasTransform != b.hasTransform) return false;
        if (a.hasTransform && (a.transform.position != b.transform.position ||
                               a.transform.rotation != b.transform.rotation ||
                               a.transform.scale != b.transform.scale)) return false;
        if (a.hasModelComponent != b.hasModelComponent) return false;
        if (a.modelPath != b.modelPath) return false;
        if (a.noShadow != b.noShadow) return false;
        if (a.hasCamera != b.hasCamera) return false;
        if (a.hasCamera && (a.camera.fovDeg != b.camera.fovDeg ||
                            a.camera.nearClip != b.camera.nearClip ||
                            a.camera.farClip != b.camera.farClip ||
                            a.camera.priority != b.camera.priority ||
                            a.camera.enabled != b.camera.enabled)) return false;
        if (a.hasLight != b.hasLight) return false;
        if (a.hasLight && (a.light.type != b.light.type ||
                           a.light.color != b.light.color ||
                           a.light.intensity != b.light.intensity ||
                           a.light.range != b.light.range ||
                           a.light.innerAngleDeg != b.light.innerAngleDeg ||
                           a.light.outerAngleDeg != b.light.outerAngleDeg ||
                           a.light.enabled != b.light.enabled)) return false;
        // physics: without these, an edit that ONLY changes a physics field
        // compares equal and the undo entry is dropped as a no-op
        // scripting: same reasoning -- retargeting an entity to a different
        // .lua file changes nothing else, so without this the edit is
        // swallowed as a no-op and cannot be undone
        if (a.hasScript != b.hasScript) return false;
        if (a.hasScript && (a.script.path != b.script.path ||
                            a.script.enabled != b.script.enabled)) return false;
        if (a.hasRigidBody != b.hasRigidBody) return false;
        if (a.hasRigidBody && (a.rigidBody.type != b.rigidBody.type ||
                               a.rigidBody.mass != b.rigidBody.mass ||
                               a.rigidBody.friction != b.rigidBody.friction ||
                               a.rigidBody.restitution != b.rigidBody.restitution ||
                               a.rigidBody.linearDamping != b.rigidBody.linearDamping ||
                               a.rigidBody.angularDamping != b.rigidBody.angularDamping ||
                               a.rigidBody.isTrigger != b.rigidBody.isTrigger ||
                               a.rigidBody.initialLinearVelocity != b.rigidBody.initialLinearVelocity)) return false;
        if (a.hasBoxCollider != b.hasBoxCollider) return false;
        if (a.hasBoxCollider && (a.boxCollider.halfExtents != b.boxCollider.halfExtents ||
                                 a.boxCollider.offset != b.boxCollider.offset)) return false;
        if (a.hasSphereCollider != b.hasSphereCollider) return false;
        if (a.hasSphereCollider && (a.sphereCollider.radius != b.sphereCollider.radius ||
                                    a.sphereCollider.offset != b.sphereCollider.offset)) return false;
        if (a.hasCapsuleCollider != b.hasCapsuleCollider) return false;
        if (a.hasCapsuleCollider && (a.capsuleCollider.radius != b.capsuleCollider.radius ||
                                     a.capsuleCollider.halfHeight != b.capsuleCollider.halfHeight ||
                                     a.capsuleCollider.offset != b.capsuleCollider.offset)) return false;
        if (a.hasPlaneCollider != b.hasPlaneCollider) return false;
        if (a.hasPlaneCollider && a.planeCollider.offset != b.planeCollider.offset) return false;
        // audio: without these, an edit that ONLY changes an audio field (or
        // adds/removes the source or listener) compares equal and the undo
        // entry is dropped as a no-op — the mutation happens but can't be undone
        if (a.hasAudioSource != b.hasAudioSource) return false;
        if (a.hasAudioSource && (a.audioSource.clip != b.audioSource.clip ||
                                 a.audioSource.volume != b.audioSource.volume ||
                                 a.audioSource.pitch != b.audioSource.pitch ||
                                 a.audioSource.loop != b.audioSource.loop ||
                                 a.audioSource.spatial != b.audioSource.spatial ||
                                 a.audioSource.playOnStart != b.audioSource.playOnStart ||
                                 a.audioSource.minDistance != b.audioSource.minDistance ||
                                 a.audioSource.maxDistance != b.audioSource.maxDistance)) return false;
        if (a.hasAudioListener != b.hasAudioListener) return false;
        if (a.hasParent != b.hasParent) return false;
        if (a.hasParent && a.parent != b.parent) return false;
        if (a.hasOverrides != b.hasOverrides) return false;
        if (a.hasOverrides) {
            if (a.overrides.byIndex.size() != b.overrides.byIndex.size()) return false;
            for (const auto& [i, ha] : a.overrides.byIndex) {
                auto it = b.overrides.byIndex.find(i);
                if (it == b.overrides.byIndex.end()) return false;
                const auto& hb = it->second;
                if (!ha != !hb) return false;
                if (ha && !materialEq(*ha, *hb)) return false;
            }
        }
        return true; // AABB is derived from the model; never compared
    }

    MaterialOverrides deepCopy(const MaterialOverrides& src) {
        MaterialOverrides dst;
        for (const auto& [i, h] : src.byIndex)
            dst.byIndex[i] = h ? std::make_shared<MyCoreEngine::Material>(*h) : nullptr;
        return dst;
    }

} // namespace

EntitySnapshot UndoHistory::capture(entt::registry& reg, entt::entity e) {
    EntitySnapshot s;
    if (!reg.valid(e)) return s;
    if (auto* n = reg.try_get<Name>(e))      { s.hasName = true; s.name = *n; }
    if (auto* t = reg.try_get<Transform>(e)) { s.hasTransform = true; s.transform = *t; }
    if (auto* mc = reg.try_get<ModelComponent>(e)) {
        s.hasModelComponent = true;
        if (mc->model) s.modelPath = mc->model->SourcePath();
    }
    if (auto* bv = reg.try_get<AABB>(e))     { s.hasAABB = true; s.aabb = *bv; }
    if (auto* mo = reg.try_get<MaterialOverrides>(e)) {
        // deep copy: history must not alias the live, still-editable materials
        s.hasOverrides = true;
        s.overrides = deepCopy(*mo);
    }
    s.noShadow = reg.any_of<NoShadow>(e);
    if (auto* p = reg.try_get<Parent>(e)) { s.hasParent = true; s.parent = p->value; }
    if (auto* c = reg.try_get<CameraComponent>(e)) { s.hasCamera = true; s.camera = *c; }
    if (auto* c = reg.try_get<LightComponent>(e))  { s.hasLight = true; s.light = *c; }
    if (auto* c = reg.try_get<ScriptComponent>(e)) { s.hasScript = true; s.script = *c; }
    if (auto* c = reg.try_get<RigidBody>(e))       { s.hasRigidBody = true; s.rigidBody = *c; }
    if (auto* c = reg.try_get<BoxCollider>(e))     { s.hasBoxCollider = true; s.boxCollider = *c; }
    if (auto* c = reg.try_get<SphereCollider>(e))  { s.hasSphereCollider = true; s.sphereCollider = *c; }
    if (auto* c = reg.try_get<CapsuleCollider>(e)) { s.hasCapsuleCollider = true; s.capsuleCollider = *c; }
    if (auto* c = reg.try_get<PlaneCollider>(e))   { s.hasPlaneCollider = true; s.planeCollider = *c; }
    if (auto* c = reg.try_get<AudioSourceComponent>(e)) { s.hasAudioSource = true; s.audioSource = *c; }
    s.hasAudioListener = reg.any_of<AudioListenerComponent>(e);
    return s;
}

void UndoHistory::apply_(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                         entt::entity e, const std::optional<EntitySnapshot>& snap) {
    if (!snap) {
        if (reg.valid(e)) reg.destroy(e);
        return;
    }
    apply(reg, assets, e, *snap);
}

void UndoHistory::apply(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                        entt::entity e, const EntitySnapshot& s) {
    if (!reg.valid(e)) {
        // resurrect under the same handle so later history entries still
        // reference the right entity (entt honors the hint if the slot is free)
        e = reg.create(e);
    }

    if (s.hasName) reg.emplace_or_replace<Name>(e, s.name);
    else reg.remove<Name>(e);

    if (s.hasTransform) {
        Transform t = s.transform;
        t.dirty = true; // snapshot's modelMatrix may be stale; force recompute
        reg.emplace_or_replace<Transform>(e, t);
    }
    else reg.remove<Transform>(e);

    if (s.hasModelComponent) {
        ModelComponent mc{};
        if (!s.modelPath.empty() && assets) {
            if (auto model = assets->GetModel(s.modelPath); model && !model->Meshes().empty()) {
                mc.model = model;
            }
        }
        // empty component restores as empty (present-but-unloaded is a
        // legitimate authoring state)
        reg.emplace_or_replace<ModelComponent>(e, std::move(mc));
    }
    else {
        reg.remove<ModelComponent>(e);
    }

    if (s.hasAABB) reg.emplace_or_replace<AABB>(e, s.aabb);
    else reg.remove<AABB>(e);

    if (s.hasOverrides) reg.emplace_or_replace<MaterialOverrides>(e, deepCopy(s.overrides));
    else reg.remove<MaterialOverrides>(e);

    if (s.noShadow) { if (!reg.any_of<NoShadow>(e)) reg.emplace<NoShadow>(e); }
    else reg.remove<NoShadow>(e);

    if (s.hasCamera) reg.emplace_or_replace<CameraComponent>(e, s.camera);
    else reg.remove<CameraComponent>(e);

    if (s.hasLight) reg.emplace_or_replace<LightComponent>(e, s.light);
    else reg.remove<LightComponent>(e);

    if (s.hasScript) reg.emplace_or_replace<ScriptComponent>(e, s.script);
    else reg.remove<ScriptComponent>(e);

    // physics (see EntitySnapshot: absent flag => component removed)
    if (s.hasRigidBody) reg.emplace_or_replace<RigidBody>(e, s.rigidBody);
    else reg.remove<RigidBody>(e);
    if (s.hasBoxCollider) reg.emplace_or_replace<BoxCollider>(e, s.boxCollider);
    else reg.remove<BoxCollider>(e);
    if (s.hasSphereCollider) reg.emplace_or_replace<SphereCollider>(e, s.sphereCollider);
    else reg.remove<SphereCollider>(e);
    if (s.hasCapsuleCollider) reg.emplace_or_replace<CapsuleCollider>(e, s.capsuleCollider);
    else reg.remove<CapsuleCollider>(e);
    if (s.hasPlaneCollider) reg.emplace_or_replace<PlaneCollider>(e, s.planeCollider);
    else reg.remove<PlaneCollider>(e);

    // audio (see EntitySnapshot: absent flag => component removed)
    if (s.hasAudioSource) reg.emplace_or_replace<AudioSourceComponent>(e, s.audioSource);
    else reg.remove<AudioSourceComponent>(e);
    if (s.hasAudioListener) { if (!reg.any_of<AudioListenerComponent>(e)) reg.emplace<AudioListenerComponent>(e); }
    else reg.remove<AudioListenerComponent>(e);

    // parent link — skipped if the target doesn't exist (yet); batch paths
    // (multi-op entries, restoreScene) run a fixup pass afterwards
    if (s.hasParent && reg.valid(s.parent)) {
        reg.emplace_or_replace<Parent>(e, Parent{ s.parent });
    }
    else {
        reg.remove<Parent>(e);
    }
}

void UndoHistory::fixupParents_(entt::registry& reg, const Entry& en, bool beforeSide) {
    for (const auto& op : en.ops) {
        const auto& snap = beforeSide ? op.before : op.after;
        if (snap && snap->hasParent && reg.valid(op.entity) && reg.valid(snap->parent)) {
            reg.emplace_or_replace<Parent>(op.entity, Parent{ snap->parent });
        }
    }
}

UndoHistory::SceneSnapshot UndoHistory::captureScene(entt::registry& reg) {
    SceneSnapshot out;
    auto view = reg.view<entt::entity>();
    for (auto e : view) out.emplace_back(e, capture(reg, e));
    return out;
}

void UndoHistory::restoreScene(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                               const SceneSnapshot& snap) {
    reg.clear(); // play-created entities vanish; everything else rebuilds
    for (const auto& [e, s] : snap) apply(reg, assets, e, s);
    // parent links: a child rebuilt before its parent lost the link in
    // apply's validity check — repair now that every entity exists
    for (const auto& [e, s] : snap) {
        if (s.hasParent && reg.valid(e) && reg.valid(s.parent)) {
            reg.emplace_or_replace<Parent>(e, Parent{ s.parent });
        }
    }
}

bool UndoHistory::same_(const std::optional<EntitySnapshot>& a,
                        const std::optional<EntitySnapshot>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return snapEq(*a, *b);
}

void UndoHistory::beginEdit(entt::registry& reg, entt::entity e, std::string label) {
    if (!recordingEnabled_) return;
    if (pendingActive_ && pendingEntity_ == e && pendingLabel_ == label) {
        pendingTouched_ = true;
        return;
    }
    if (pendingActive_) endEdit(reg); // finalize a stray previous edit first
    pendingActive_ = true;
    pendingTouched_ = true;
    pendingEntity_ = e;
    pendingLabel_ = std::move(label);
    pendingBefore_ = capture(reg, e);
}

void UndoHistory::endEdit(entt::registry& reg) {
    if (!pendingActive_) return;
    pendingActive_ = false;
    if (!reg.valid(pendingEntity_)) return; // entity vanished mid-edit: drop
    SubOp op;
    op.entity = pendingEntity_;
    op.before = std::move(pendingBefore_);
    op.after = capture(reg, pendingEntity_);
    if (same_(op.before, op.after)) return;
    Entry en;
    en.label = std::move(pendingLabel_);
    en.ops.push_back(std::move(op));
    push_(std::move(en));
}

void UndoHistory::endEditIf(entt::registry& reg, const std::string& label) {
    if (pendingActive_ && pendingLabel_ == label) endEdit(reg);
}

void UndoHistory::cancelEditIf(const std::string& label) {
    if (pendingActive_ && pendingLabel_ == label) cancelEdit();
}

void UndoHistory::touchEdit(const std::string& label) {
    if (pendingActive_ && pendingLabel_ == label) pendingTouched_ = true;
}

void UndoHistory::tickFrame(entt::registry& reg) {
    if (pendingActive_ && !pendingTouched_) endEdit(reg); // widget vanished
    pendingTouched_ = false;
}

namespace {
    // root + every descendant, derived from the Parent links. Each entity
    // has at most one parent, so each appears in at most one children list
    // and the walk is bounded even on corrupt data.
    std::vector<entt::entity> collectSubtree(entt::registry& reg, entt::entity root) {
        std::unordered_map<entt::entity, std::vector<entt::entity>> children;
        for (auto [e, p] : reg.view<Parent>().each()) {
            if (reg.valid(p.value)) children[p.value].push_back(e);
        }
        std::vector<entt::entity> out{ root };
        for (size_t i = 0; i < out.size(); ++i) {
            if (auto it = children.find(out[i]); it != children.end()) {
                for (auto c : it->second) out.push_back(c);
            }
        }
        return out;
    }
} // namespace

void UndoHistory::record(entt::registry& reg, entt::entity e, std::string label,
                         const std::function<void()>& mutate) {
    if (!recordingEnabled_) { mutate(); return; } // apply, don't remember
    SubOp op;
    op.entity = e;
    op.before = capture(reg, e);
    mutate();
    op.after = reg.valid(e) ? std::optional<EntitySnapshot>(capture(reg, e))
                            : std::nullopt;
    if (same_(op.before, op.after)) return;
    Entry en;
    en.label = std::move(label);
    en.ops.push_back(std::move(op));
    push_(std::move(en));
}

void UndoHistory::recordCreate(entt::registry& reg, entt::entity e, std::string label) {
    if (!recordingEnabled_) return; // entity already created by the caller
    Entry en;
    en.label = std::move(label);
    SubOp op;
    op.entity = e;
    op.before = std::nullopt;
    op.after = capture(reg, e);
    en.ops.push_back(std::move(op));
    push_(std::move(en));
}

void UndoHistory::recordDelete(entt::registry& reg, entt::entity e, std::string label) {
    if (!reg.valid(e)) return;
    const auto subtree = collectSubtree(reg, e);
    if (!recordingEnabled_) { // delete (with children), don't remember
        for (auto s : subtree) if (reg.valid(s)) reg.destroy(s);
        return;
    }
    Entry en;
    en.label = std::move(label);
    en.ops.reserve(subtree.size());
    for (auto s : subtree) {
        SubOp op;
        op.entity = s;
        op.before = capture(reg, s);
        op.after = std::nullopt;
        en.ops.push_back(std::move(op));
    }
    for (auto s : subtree) if (reg.valid(s)) reg.destroy(s);
    push_(std::move(en));
}

void UndoHistory::recordTransformChange(entt::registry& reg, entt::entity e,
                                        const Transform& before, std::string label) {
    if (!reg.valid(e)) return;
    if (!recordingEnabled_) return; // the drag already applied its change
    SubOp op;
    op.entity = e;
    EntitySnapshot after = capture(reg, e);
    EntitySnapshot beforeSnap = after; // only the transform differed during the drag
    beforeSnap.hasTransform = true;
    beforeSnap.transform = before;
    op.before = std::move(beforeSnap);
    op.after = std::move(after);
    if (same_(op.before, op.after)) return; // click without drag
    Entry en;
    en.label = std::move(label);
    en.ops.push_back(std::move(op));
    push_(std::move(en));
}

void UndoHistory::undo(entt::registry& reg, MyCoreEngine::AssetManager* assets) {
    if (pendingActive_) cancelEdit(); // a live drag can't survive a history rewind
    if (!canUndo()) return;
    const Entry& en = entries_[--cursor_];
    for (const auto& op : en.ops) apply_(reg, assets, op.entity, op.before);
    fixupParents_(reg, en, /*beforeSide=*/true);
}

void UndoHistory::redo(entt::registry& reg, MyCoreEngine::AssetManager* assets) {
    if (pendingActive_) cancelEdit();
    if (!canRedo()) return;
    const Entry& en = entries_[cursor_++];
    for (const auto& op : en.ops) apply_(reg, assets, op.entity, op.after);
    fixupParents_(reg, en, /*beforeSide=*/false);
}

void UndoHistory::jumpTo(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                         size_t target) {
    if (target > entries_.size()) target = entries_.size();
    while (cursor_ > target) undo(reg, assets);
    while (cursor_ < target) redo(reg, assets);
}

void UndoHistory::clear() {
    entries_.clear();
    cursor_ = 0;
    pendingActive_ = false;
}

void UndoHistory::push_(Entry entry) {
    // a new edit after undos discards the redo tail: history has diverged
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                   entries_.end());
    entries_.push_back(std::move(entry));
    if (entries_.size() > kMaxEntries) entries_.pop_front();
    cursor_ = entries_.size();
}
