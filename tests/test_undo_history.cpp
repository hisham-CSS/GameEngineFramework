// UndoHistory (editor undo/redo, P2-7): headless tests.
// AssetManager is passed as null everywhere — model restore is skipped, all
// other components round-trip. Model reload itself needs GL and is covered
// by using the editor.
#include <gtest/gtest.h>

#include "UndoHistory.h"

namespace {

entt::entity makeEntity(entt::registry& reg, const char* name) {
    entt::entity e = reg.create();
    reg.emplace<Name>(e, Name{ name });
    reg.emplace<Transform>(e);
    return e;
}

} // namespace

TEST(UndoHistory, CoalescedEditUndoRedo) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.beginEdit(reg, e, "Position");
    // several per-frame changes during one drag...
    reg.get<Transform>(e).position = { 1.f, 0.f, 0.f };
    reg.get<Transform>(e).position = { 2.f, 3.f, 4.f };
    h.endEdit(reg);

    // ...coalesce into exactly one entry
    ASSERT_EQ(h.entries().size(), 1u);
    EXPECT_TRUE(h.canUndo());
    EXPECT_FALSE(h.canRedo());

    h.undo(reg, nullptr);
    EXPECT_EQ(reg.get<Transform>(e).position, glm::vec3(0.f));
    EXPECT_TRUE(h.canRedo());

    h.redo(reg, nullptr);
    EXPECT_EQ(reg.get<Transform>(e).position, glm::vec3(2.f, 3.f, 4.f));
}

TEST(UndoHistory, NoOpEditIsDropped) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.beginEdit(reg, e, "Position");
    h.endEdit(reg); // nothing changed
    EXPECT_FALSE(h.canUndo());
    EXPECT_TRUE(h.entries().empty());
}

TEST(UndoHistory, RepeatedBeginSameEditKeepsFirstCapture) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.beginEdit(reg, e, "Position");
    reg.get<Transform>(e).position = { 5.f, 0.f, 0.f };
    h.beginEdit(reg, e, "Position"); // same edit still active: must not re-capture
    reg.get<Transform>(e).position = { 9.f, 0.f, 0.f };
    h.endEdit(reg);

    h.undo(reg, nullptr);
    EXPECT_EQ(reg.get<Transform>(e).position, glm::vec3(0.f));
}

TEST(UndoHistory, RecordInstantEdit) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.record(reg, e, "Disable shadow casting", [&] { reg.emplace<NoShadow>(e); });
    EXPECT_TRUE(reg.any_of<NoShadow>(e));

    h.undo(reg, nullptr);
    EXPECT_FALSE(reg.any_of<NoShadow>(e));
    h.redo(reg, nullptr);
    EXPECT_TRUE(reg.any_of<NoShadow>(e));
}

TEST(UndoHistory, DeleteRestoresAllComponents) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "Victim");
    reg.get<Transform>(e).position = { 1.f, 2.f, 3.f };
    reg.emplace<AABB>(e, glm::vec3(-1.f), glm::vec3(1.f));
    reg.emplace<NoShadow>(e);
    MaterialOverrides mo;
    auto mat = std::make_shared<MyCoreEngine::Material>();
    mat->baseColor = { 0.25f, 0.5f, 0.75f };
    mat->roughness = 0.125f;
    mo.byIndex[2] = mat;
    reg.emplace<MaterialOverrides>(e, std::move(mo));

    h.recordDelete(reg, e, "Delete 'Victim'");
    EXPECT_FALSE(reg.valid(e));

    h.undo(reg, nullptr);
    ASSERT_TRUE(reg.valid(e)); // resurrected under the SAME handle
    EXPECT_EQ(reg.get<Name>(e).value, "Victim");
    EXPECT_EQ(reg.get<Transform>(e).position, glm::vec3(1.f, 2.f, 3.f));
    EXPECT_TRUE(reg.get<Transform>(e).dirty);
    EXPECT_TRUE(reg.any_of<NoShadow>(e));
    EXPECT_EQ(reg.get<AABB>(e).min, glm::vec3(-1.f));
    const auto& rmo = reg.get<MaterialOverrides>(e);
    ASSERT_EQ(rmo.byIndex.count(2), 1u);
    EXPECT_EQ(rmo.byIndex.at(2)->baseColor, glm::vec3(0.25f, 0.5f, 0.75f));
    EXPECT_FLOAT_EQ(rmo.byIndex.at(2)->roughness, 0.125f);

    h.redo(reg, nullptr);
    EXPECT_FALSE(reg.valid(e));
}

TEST(UndoHistory, CreateUndoRedo) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "New");
    h.recordCreate(reg, e, "Create entity");

    h.undo(reg, nullptr);
    EXPECT_FALSE(reg.valid(e));

    h.redo(reg, nullptr);
    ASSERT_TRUE(reg.valid(e));
    EXPECT_EQ(reg.get<Name>(e).value, "New");
}

TEST(UndoHistory, MaterialSnapshotIsDeepCopy) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");
    MaterialOverrides mo;
    mo.byIndex[0] = std::make_shared<MyCoreEngine::Material>();
    reg.emplace<MaterialOverrides>(e, std::move(mo));

    h.beginEdit(reg, e, "Material 1 metallic");
    reg.get<MaterialOverrides>(e).byIndex[0]->metallic = 0.9f;
    h.endEdit(reg);

    // mutate the live material AFTER the snapshot; undo must restore the
    // captured value, not whatever the live object holds now
    reg.get<MaterialOverrides>(e).byIndex[0]->metallic = 0.1f;
    h.undo(reg, nullptr);
    EXPECT_FLOAT_EQ(reg.get<MaterialOverrides>(e).byIndex[0]->metallic, 0.f);
    h.redo(reg, nullptr);
    EXPECT_FLOAT_EQ(reg.get<MaterialOverrides>(e).byIndex[0]->metallic, 0.9f);
}

TEST(UndoHistory, NewEditDropsRedoTail) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.record(reg, e, "one", [&] { reg.get<Transform>(e).position.x = 1.f; });
    h.record(reg, e, "two", [&] { reg.get<Transform>(e).position.x = 2.f; });
    h.undo(reg, nullptr);
    EXPECT_TRUE(h.canRedo());

    h.record(reg, e, "three", [&] { reg.get<Transform>(e).position.x = 3.f; });
    EXPECT_FALSE(h.canRedo());
    ASSERT_EQ(h.entries().size(), 2u);
    EXPECT_EQ(h.entries().back().label, "three");
}

TEST(UndoHistory, CapEvictsOldestEntries) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    for (int i = 1; i <= (int)UndoHistory::kMaxEntries + 20; ++i) {
        h.record(reg, e, "edit", [&] { reg.get<Transform>(e).position.x = (float)i; });
    }
    EXPECT_EQ(h.entries().size(), UndoHistory::kMaxEntries);
    EXPECT_EQ(h.cursor(), UndoHistory::kMaxEntries);

    // fully unwinding lands on the oldest RETAINED state, not the original
    while (h.canUndo()) h.undo(reg, nullptr);
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 20.f);
}

TEST(UndoHistory, JumpToRewindsAndReplays) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.record(reg, e, "one",   [&] { reg.get<Transform>(e).position.x = 1.f; });
    h.record(reg, e, "two",   [&] { reg.get<Transform>(e).position.x = 2.f; });
    h.record(reg, e, "three", [&] { reg.get<Transform>(e).position.x = 3.f; });

    h.jumpTo(reg, nullptr, 0);
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 0.f);
    h.jumpTo(reg, nullptr, 2);
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 2.f);
    h.jumpTo(reg, nullptr, 3);
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 3.f);
}

TEST(UndoHistory, TransformChangeHelper) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    Transform before = reg.get<Transform>(e); // pre-drag copy
    reg.get<Transform>(e).position = { 7.f, 8.f, 9.f }; // the drag
    h.recordTransformChange(reg, e, before, "Move (gizmo)");

    ASSERT_EQ(h.entries().size(), 1u);
    h.undo(reg, nullptr);
    EXPECT_EQ(reg.get<Transform>(e).position, glm::vec3(0.f));
    h.redo(reg, nullptr);
    EXPECT_EQ(reg.get<Transform>(e).position, glm::vec3(7.f, 8.f, 9.f));

    // no-op drag (click without movement) pushes nothing
    Transform same = reg.get<Transform>(e);
    h.recordTransformChange(reg, e, same, "Move (gizmo)");
    EXPECT_EQ(h.entries().size(), 1u);
}

TEST(UndoHistory, TickFrameCommitsAbandonedEdit) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    // frame 0: widget activates and edits
    h.beginEdit(reg, e, "Rename");
    reg.get<Name>(e).value = "Renamed";
    h.tickFrame(reg); // beginEdit counts as this frame's touch
    EXPECT_TRUE(h.editActive());

    // frame 1: widget still alive
    h.touchEdit("Rename");
    h.tickFrame(reg);
    EXPECT_TRUE(h.editActive());

    // frame 2: widget no longer submitted (entity deselected / tab hidden):
    // the edit is committed, not lost
    h.tickFrame(reg);
    EXPECT_FALSE(h.editActive());
    ASSERT_EQ(h.entries().size(), 1u);
    h.undo(reg, nullptr);
    EXPECT_EQ(reg.get<Name>(e).value, "A");
}

TEST(UndoHistory, TickFrameDropsAbandonedNoOpEdit) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.beginEdit(reg, e, "Rename"); // activated, nothing typed
    h.tickFrame(reg);
    h.tickFrame(reg); // abandoned with no change: no phantom entry
    EXPECT_FALSE(h.editActive());
    EXPECT_TRUE(h.entries().empty());
}

// --- play-in-editor (P2-6): whole-scene snapshot/restore -------------------

TEST(SceneSnapshot, RestoreRoundTripPreservesHandlesAndDiscardsPlayChanges) {
    entt::registry reg;
    auto a = makeEntity(reg, "A");
    reg.get<Transform>(a).position = { 1.f, 0.f, 0.f };
    reg.emplace<NoShadow>(a);
    auto b = makeEntity(reg, "B");
    MaterialOverrides mo;
    auto mat = std::make_shared<MyCoreEngine::Material>();
    mat->metallic = 0.75f;
    mo.byIndex[1] = mat;
    reg.emplace<MaterialOverrides>(b, std::move(mo));
    reg.emplace<AABB>(b, glm::vec3(-1.f), glm::vec3(1.f));

    const auto snap = UndoHistory::captureScene(reg);

    // "play": mutate A, destroy B, spawn a new entity
    reg.get<Transform>(a).position = { 9.f, 9.f, 9.f };
    reg.get<Name>(a).value = "A-played";
    reg.destroy(b);
    auto playSpawn = makeEntity(reg, "PlaySpawn");
    (void)playSpawn;

    UndoHistory::restoreScene(reg, nullptr, snap);

    // original handles are back with their edit-mode state
    ASSERT_TRUE(reg.valid(a));
    ASSERT_TRUE(reg.valid(b));
    EXPECT_EQ(reg.get<Name>(a).value, "A");
    EXPECT_EQ(reg.get<Transform>(a).position, glm::vec3(1.f, 0.f, 0.f));
    EXPECT_TRUE(reg.any_of<NoShadow>(a));
    EXPECT_FLOAT_EQ(reg.get<MaterialOverrides>(b).byIndex.at(1)->metallic, 0.75f);
    EXPECT_EQ(reg.get<AABB>(b).min, glm::vec3(-1.f));

    // the play-spawned entity is gone: exactly the two originals remain
    size_t count = 0;
    for (auto e : reg.view<entt::entity>()) { (void)e; ++count; }
    EXPECT_EQ(count, 2u);
}

TEST(SceneSnapshot, UndoHistoryStaysValidAcrossPlaySession) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    // edit-mode history: position 0 -> 1
    h.record(reg, e, "move", [&] { reg.get<Transform>(e).position.x = 1.f; });

    // play session mutates the entity, stop restores it
    const auto snap = UndoHistory::captureScene(reg);
    reg.get<Transform>(e).position.x = 42.f;
    UndoHistory::restoreScene(reg, nullptr, snap);
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 1.f);

    // the pre-play history still applies cleanly
    h.undo(reg, nullptr);
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 0.f);
    h.redo(reg, nullptr);
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 1.f);
}

TEST(UndoHistory, RecordingDisabledMutatesWithoutHistory) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.setRecordingEnabled(false);

    h.record(reg, e, "move", [&] { reg.get<Transform>(e).position.x = 5.f; });
    EXPECT_FLOAT_EQ(reg.get<Transform>(e).position.x, 5.f); // mutation applied
    h.beginEdit(reg, e, "Position");
    reg.get<Transform>(e).position.x = 6.f;
    h.endEdit(reg);
    h.recordTransformChange(reg, e, Transform{}, "Move (gizmo)");
    auto victim = makeEntity(reg, "Victim");
    h.recordCreate(reg, victim, "Create entity");
    h.recordDelete(reg, victim, "Delete 'Victim'");
    EXPECT_FALSE(reg.valid(victim)); // delete still deletes
    EXPECT_TRUE(h.entries().empty()); // ...but nothing was recorded

    h.setRecordingEnabled(true);
    h.record(reg, e, "move", [&] { reg.get<Transform>(e).position.x = 7.f; });
    EXPECT_EQ(h.entries().size(), 1u); // recording works again
}

// --- hierarchy (P2-8): subtree delete + parent links in snapshots ----------

TEST(UndoHistory, DeleteRestoresWholeSubtree) {
    entt::registry reg;
    UndoHistory h;
    auto parent = makeEntity(reg, "P");
    auto child = makeEntity(reg, "C");
    auto grandchild = makeEntity(reg, "G");
    reg.emplace<Parent>(child, Parent{ parent });
    reg.emplace<Parent>(grandchild, Parent{ child });
    reg.get<Transform>(child).position = { 1.f, 2.f, 3.f };

    h.recordDelete(reg, parent, "Delete 'P'");
    EXPECT_FALSE(reg.valid(parent));
    EXPECT_FALSE(reg.valid(child));
    EXPECT_FALSE(reg.valid(grandchild));
    ASSERT_EQ(h.entries().size(), 1u);
    EXPECT_EQ(h.entries().back().ops.size(), 3u);

    h.undo(reg, nullptr);
    ASSERT_TRUE(reg.valid(parent));
    ASSERT_TRUE(reg.valid(child));
    ASSERT_TRUE(reg.valid(grandchild));
    EXPECT_EQ(reg.get<Parent>(child).value, parent);
    EXPECT_EQ(reg.get<Parent>(grandchild).value, child);
    EXPECT_EQ(reg.get<Transform>(child).position, glm::vec3(1.f, 2.f, 3.f));

    h.redo(reg, nullptr);
    EXPECT_FALSE(reg.valid(parent));
    EXPECT_FALSE(reg.valid(child));
    EXPECT_FALSE(reg.valid(grandchild));
}

TEST(UndoHistory, ReparentUndoRestoresOldParentAndLocal) {
    entt::registry reg;
    UndoHistory h;
    auto a = makeEntity(reg, "A");
    reg.get<Transform>(a).position = { 10.f, 0.f, 0.f };
    auto child = makeEntity(reg, "C");
    reg.get<Transform>(child).position = { 1.f, 2.f, 3.f };

    h.record(reg, child, "Parent 'C' under 'A'", [&] {
        ASSERT_TRUE(MyCoreEngine::SetParentKeepWorld(reg, child, a));
    });
    ASSERT_TRUE(reg.any_of<Parent>(child));
    EXPECT_EQ(reg.get<Parent>(child).value, a);

    h.undo(reg, nullptr);
    EXPECT_FALSE(reg.any_of<Parent>(child));
    EXPECT_EQ(reg.get<Transform>(child).position, glm::vec3(1.f, 2.f, 3.f));

    h.redo(reg, nullptr);
    ASSERT_TRUE(reg.any_of<Parent>(child));
    EXPECT_EQ(reg.get<Parent>(child).value, a);
}

TEST(SceneSnapshot, ParentLinksSurviveRestoreRegardlessOfOrder) {
    entt::registry reg;
    // child created BEFORE parent so the snapshot lists it first — the
    // restore fixup pass must still rebuild the link
    auto child = makeEntity(reg, "C");
    auto parent = makeEntity(reg, "P");
    reg.emplace<Parent>(child, Parent{ parent });

    const auto snap = UndoHistory::captureScene(reg);
    reg.get<Transform>(child).position = { 9.f, 9.f, 9.f };
    reg.destroy(child);
    UndoHistory::restoreScene(reg, nullptr, snap);

    ASSERT_TRUE(reg.valid(child));
    ASSERT_TRUE(reg.valid(parent));
    ASSERT_TRUE(reg.any_of<Parent>(child));
    EXPECT_EQ(reg.get<Parent>(child).value, parent);
}

TEST(UndoHistory, EntityVanishingMidEditDropsEntry) {
    entt::registry reg;
    UndoHistory h;
    auto e = makeEntity(reg, "A");

    h.beginEdit(reg, e, "Position");
    reg.destroy(e);
    h.endEdit(reg);
    EXPECT_TRUE(h.entries().empty());
}
