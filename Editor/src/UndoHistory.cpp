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
        if (a.modelPath != b.modelPath) return false;
        if (a.noShadow != b.noShadow) return false;
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
    if (auto* mc = reg.try_get<ModelComponent>(e); mc && mc->model)
        s.modelPath = mc->model->SourcePath();
    if (auto* bv = reg.try_get<AABB>(e))     { s.hasAABB = true; s.aabb = *bv; }
    if (auto* mo = reg.try_get<MaterialOverrides>(e)) {
        // deep copy: history must not alias the live, still-editable materials
        s.hasOverrides = true;
        s.overrides = deepCopy(*mo);
    }
    s.noShadow = reg.any_of<NoShadow>(e);
    return s;
}

void UndoHistory::apply_(entt::registry& reg, MyCoreEngine::AssetManager* assets,
                         entt::entity e, const std::optional<EntitySnapshot>& snap) {
    if (!snap) {
        if (reg.valid(e)) reg.destroy(e);
        return;
    }
    const EntitySnapshot& s = *snap;
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

    bool hasModel = false;
    if (!s.modelPath.empty() && assets) {
        if (auto model = assets->GetModel(s.modelPath); model && !model->Meshes().empty()) {
            reg.emplace_or_replace<ModelComponent>(e, ModelComponent{ model });
            hasModel = true;
        }
    }
    if (!hasModel) reg.remove<ModelComponent>(e);

    if (s.hasAABB) reg.emplace_or_replace<AABB>(e, s.aabb);
    else reg.remove<AABB>(e);

    if (s.hasOverrides) reg.emplace_or_replace<MaterialOverrides>(e, deepCopy(s.overrides));
    else reg.remove<MaterialOverrides>(e);

    if (s.noShadow) { if (!reg.any_of<NoShadow>(e)) reg.emplace<NoShadow>(e); }
    else reg.remove<NoShadow>(e);
}

bool UndoHistory::same_(const std::optional<EntitySnapshot>& a,
                        const std::optional<EntitySnapshot>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return snapEq(*a, *b);
}

void UndoHistory::beginEdit(entt::registry& reg, entt::entity e, std::string label) {
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
    Entry en;
    en.label = std::move(pendingLabel_);
    en.entity = pendingEntity_;
    en.before = std::move(pendingBefore_);
    en.after = capture(reg, pendingEntity_);
    if (same_(en.before, en.after)) return;
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

void UndoHistory::record(entt::registry& reg, entt::entity e, std::string label,
                         const std::function<void()>& mutate) {
    Entry en;
    en.label = std::move(label);
    en.entity = e;
    en.before = capture(reg, e);
    mutate();
    en.after = reg.valid(e) ? std::optional<EntitySnapshot>(capture(reg, e))
                            : std::nullopt;
    if (same_(en.before, en.after)) return;
    push_(std::move(en));
}

void UndoHistory::recordCreate(entt::registry& reg, entt::entity e, std::string label) {
    Entry en;
    en.label = std::move(label);
    en.entity = e;
    en.before = std::nullopt;
    en.after = capture(reg, e);
    push_(std::move(en));
}

void UndoHistory::recordDelete(entt::registry& reg, entt::entity e, std::string label) {
    if (!reg.valid(e)) return;
    Entry en;
    en.label = std::move(label);
    en.entity = e;
    en.before = capture(reg, e);
    en.after = std::nullopt;
    reg.destroy(e);
    push_(std::move(en));
}

void UndoHistory::recordTransformChange(entt::registry& reg, entt::entity e,
                                        const Transform& before, std::string label) {
    if (!reg.valid(e)) return;
    EntitySnapshot after = capture(reg, e);
    EntitySnapshot beforeSnap = after; // only the transform differed during the drag
    beforeSnap.hasTransform = true;
    beforeSnap.transform = before;
    Entry en;
    en.label = std::move(label);
    en.entity = e;
    en.before = std::move(beforeSnap);
    en.after = std::move(after);
    if (same_(en.before, en.after)) return; // click without drag
    push_(std::move(en));
}

void UndoHistory::undo(entt::registry& reg, MyCoreEngine::AssetManager* assets) {
    if (pendingActive_) cancelEdit(); // a live drag can't survive a history rewind
    if (!canUndo()) return;
    const Entry& en = entries_[--cursor_];
    apply_(reg, assets, en.entity, en.before);
}

void UndoHistory::redo(entt::registry& reg, MyCoreEngine::AssetManager* assets) {
    if (pendingActive_) cancelEdit();
    if (!canRedo()) return;
    const Entry& en = entries_[cursor_++];
    apply_(reg, assets, en.entity, en.after);
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
