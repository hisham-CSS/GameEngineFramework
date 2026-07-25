// UI asset hot reload: markup + stylesheet reloaded from disk while running.
//
// Pure CPU — no GL context, because nothing here paints. What is actually being
// tested is the set of promises that make hot reload usable rather than
// dangerous:
//
//  - it NOTICES a change (both kinds of change: new mtime, and new size with an
//    unchanged mtime, which is why the stamp carries both);
//  - it does NOT re-read files that did not change, and it stats at most a few
//    times a second;
//  - a BROKEN edit leaves the running UI alone — half-typed files are the
//    normal state while iterating, so this is the common case, not the edge;
//  - a fixed file RECOVERS, i.e. the failure path does not latch;
//  - handlers are RE-BOUND, because a reload rebuilds the tree and invalidates
//    every pointer and listener the app registered. This is the failure mode
//    every hot-reload system has, and it is silent: the UI looks right and the
//    buttons quietly stop working.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIElement.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

const char* kMarkupA = R"(<UI name="hud">
  <Element name="bar" class="bar"/>
  <Button name="go" class="btn" text="GO"/>
</UI>)";

// Same shape, different names, so a reload is unambiguous.
const char* kMarkupB = R"(<UI name="hud">
  <Element name="bar2" class="bar"/>
  <Button name="go" class="btn" text="GO"/>
</UI>)";

const char* kStyleA = ".bar { width: 100px; height: 10px; }\n"
                      ".btn { width: 80px; height: 20px; }\n";
const char* kStyleB = ".bar { width: 200px; height: 10px; }\n"
                      ".btn { width: 80px; height: 20px; }\n";

// Writes and then STAMPS the file with an explicit modification time. Real
// edits arrive seconds apart; a test writes them microseconds apart, and on a
// filesystem with coarse mtime granularity two writes can land on the same
// tick. Setting the time explicitly makes "the file changed" mean exactly what
// the test says rather than depending on the host's clock resolution.
void writeAt(const std::string& path, const std::string& text, int secondsFromNow) {
    { std::ofstream o(path, std::ios::binary); o << text; }
    std::filesystem::last_write_time(
        path, std::filesystem::file_time_type::clock::now() +
                  std::chrono::seconds(secondsFromNow));
}

void clickAt(UIDocument& doc, float x, float y) {
    UIPointerState p;
    p.inside = true;
    p.position = { x, y };
    p.buttonDown = true;  doc.UpdatePointer(p);
    p.buttonDown = false; doc.UpdatePointer(p);
}

class UIHotReloadTest : public ::testing::Test {
protected:
    // Relative to the working directory on purpose: UIMarkup runs the same
    // containment gate as every other authored path, so an absolute temp
    // directory would be (correctly) refused.
    std::string markup_ = "test_ui_hot.uxml";
    std::string style_ = "test_ui_hot.uss";

    void SetUp() override {
        writeAt(markup_, kMarkupA, 0);
        writeAt(style_, kStyleA, 0);
    }
    void TearDown() override {
        std::remove(markup_.c_str());
        std::remove(style_.c_str());
    }

    // Poll on every Update, so tests drive reloads directly instead of
    // simulating the passage of time. The interval itself is tested separately.
    static void pollEveryFrame(UIAssetDocument& d) { d.SetPollInterval(0.0f); }
};

} // namespace

TEST_F(UIHotReloadTest, LoadAppliesBothMarkupAndStylesheet) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    EXPECT_TRUE(d.ok()) << (d.errors().empty() ? "" : d.errors()[0]);

    UIElement* bar = d.document().root().Find("bar");
    ASSERT_NE(bar, nullptr) << "markup was not loaded";
    EXPECT_FLOAT_EQ(bar->style().width.value, 100.0f) << "stylesheet was not applied";
}

TEST_F(UIHotReloadTest, ReloadsWhenTheMarkupChanges) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    pollEveryFrame(d);

    EXPECT_FALSE(d.Update(0.0f)) << "reloaded with nothing changed";

    writeAt(markup_, kMarkupB, 2);
    EXPECT_TRUE(d.Update(0.0f)) << "an edited markup file was not picked up";
    EXPECT_EQ(d.document().root().Find("bar"), nullptr);
    EXPECT_NE(d.document().root().Find("bar2"), nullptr);

    // The stylesheet is re-applied to the NEW tree, not just to the old one.
    UIElement* bar2 = d.document().root().Find("bar2");
    ASSERT_NE(bar2, nullptr);
    EXPECT_FLOAT_EQ(bar2->style().width.value, 100.0f);
}

TEST_F(UIHotReloadTest, ReloadsWhenOnlyTheStylesheetChanges) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    pollEveryFrame(d);

    writeAt(style_, kStyleB, 2);
    EXPECT_TRUE(d.Update(0.0f)) << "an edited stylesheet was not picked up";

    UIElement* bar = d.document().root().Find("bar");
    ASSERT_NE(bar, nullptr);
    EXPECT_FLOAT_EQ(bar->style().width.value, 200.0f);
}

// The stamp is (mtime, size) rather than mtime alone. Both halves earn their
// keep: an editor that preserves the timestamp still changes the size, and an
// edit that keeps the size still moves the timestamp.
TEST_F(UIHotReloadTest, DetectsASameSizeEditAndASameTimeEdit) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    pollEveryFrame(d);

    // Same byte count, later timestamp.
    const std::string sameSize = std::string(kStyleA).replace(
        std::string(kStyleA).find("100px"), 5, "150px");
    ASSERT_EQ(sameSize.size(), std::string(kStyleA).size());
    writeAt(style_, sameSize, 2);
    ASSERT_TRUE(d.Update(0.0f)) << "a same-size edit went unnoticed";
    EXPECT_FLOAT_EQ(d.document().root().Find("bar")->style().width.value, 150.0f);

    // Different byte count, timestamp forced back to the value just recorded.
    const auto stamp = std::filesystem::last_write_time(style_);
    { std::ofstream o(style_, std::ios::binary); o << ".bar { width: 175px; }\n"; }
    std::filesystem::last_write_time(style_, stamp);
    ASSERT_TRUE(d.Update(0.0f)) << "a same-timestamp edit went unnoticed";
    EXPECT_FLOAT_EQ(d.document().root().Find("bar")->style().width.value, 175.0f);
}

TEST_F(UIHotReloadTest, PollsOnlyOnceEveryInterval) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    d.SetPollInterval(0.25f);

    writeAt(markup_, kMarkupB, 2);
    // Well short of the interval: the change exists on disk but must not have
    // been looked for yet. Statting two files every frame is wasted work in a
    // shipped game, where the files never change at all.
    EXPECT_FALSE(d.Update(0.05f));
    EXPECT_FALSE(d.Update(0.05f));
    EXPECT_NE(d.document().root().Find("bar"), nullptr) << "reloaded before the interval";

    EXPECT_TRUE(d.Update(0.20f)) << "the interval elapsed and nothing was polled";
    EXPECT_NE(d.document().root().Find("bar2"), nullptr);
}

TEST_F(UIHotReloadTest, DisablingHotReloadStopsPollingEntirely) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    pollEveryFrame(d);
    d.SetHotReloadEnabled(false);

    writeAt(markup_, kMarkupB, 2);
    EXPECT_FALSE(d.Update(1.0f));
    EXPECT_NE(d.document().root().Find("bar"), nullptr) << "reloaded while disabled";

    // ...but an explicit Reload still works: disabling the WATCH must not
    // disable the mechanism.
    EXPECT_TRUE(d.Reload());
    EXPECT_NE(d.document().root().Find("bar2"), nullptr);
}

// The important one. A half-typed file is the normal state while editing, so a
// parse failure must be a no-op on the live UI, not a blank screen.
TEST_F(UIHotReloadTest, BrokenMarkupKeepsTheLastGoodTree) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    pollEveryFrame(d);

    writeAt(markup_, "<UI name=\"hud\"><Element name=\"bar\">", 2); // unclosed
    EXPECT_FALSE(d.Update(0.0f)) << "a failed parse reported success";
    EXPECT_FALSE(d.ok());
    EXPECT_FALSE(d.errors().empty()) << "a failed reload said nothing about why";

    UIElement* bar = d.document().root().Find("bar");
    ASSERT_NE(bar, nullptr) << "a broken edit destroyed the running UI";
    EXPECT_FLOAT_EQ(bar->style().width.value, 100.0f) << "styling was lost too";

    // Polling again must NOT keep re-reading the same broken file: the stamps
    // are refreshed even on failure, or every poll would re-report the same
    // error until the file is fixed.
    EXPECT_FALSE(d.Update(0.0f));
}

TEST_F(UIHotReloadTest, RecoversWhenTheBrokenFileIsFixed) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    pollEveryFrame(d);

    writeAt(markup_, "<UI name=\"hud\"><Element", 2);
    ASSERT_FALSE(d.Update(0.0f));

    // Fixing it must reload — the failure path refreshes the stamps, and if it
    // did so without leaving the next change detectable, one typo would kill
    // hot reload for the rest of the session.
    writeAt(markup_, kMarkupB, 4);
    EXPECT_TRUE(d.Update(0.0f)) << "hot reload latched off after one bad edit";
    EXPECT_TRUE(d.ok());
    EXPECT_NE(d.document().root().Find("bar2"), nullptr);
}

// A stylesheet is cosmetic; structure without styling is far more useful to
// look at than nothing at all, so a bad .uss is reported but not fatal.
TEST_F(UIHotReloadTest, BrokenStylesheetIsNotFatalAndKeepsTheLastGoodStyling) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, style_));
    pollEveryFrame(d);

    writeAt(style_, ".bar { width: potato; }\n", 2);
    // Reload SUCCEEDED (the markup is fine) even though errors were reported.
    EXPECT_TRUE(d.Update(0.0f));
    EXPECT_FALSE(d.errors().empty()) << "a bad stylesheet parsed silently";

    UIElement* bar = d.document().root().Find("bar");
    ASSERT_NE(bar, nullptr);
    EXPECT_FLOAT_EQ(bar->style().width.value, 100.0f)
        << "a typo in the stylesheet threw away the last good styling";
}

// Reloading rebuilds the tree, so cached pointers dangle and listeners vanish.
// The bind callback is the only place an app can put that back together, which
// is why Load takes one.
TEST_F(UIHotReloadTest, BindRunsOnEveryLoadAndRebindsHandlers) {
    UIAssetDocument d;
    int binds = 0, clicks = 0;
    UIElement* boundButton = nullptr;

    ASSERT_TRUE(d.Load(markup_, style_, [&](UIDocument& doc) {
        ++binds;
        boundButton = doc.root().Find("go");
        if (boundButton) boundButton->OnClick([&](UIEvent&) { ++clicks; });
    }));
    EXPECT_EQ(binds, 1) << "bind did not run on the initial load";
    pollEveryFrame(d);

    UIElement* first = boundButton;
    ASSERT_NE(first, nullptr);

    d.document().Layout(400.0f, 200.0f);
    clickAt(d.document(), 40.0f, 20.0f);
    ASSERT_EQ(clicks, 1) << "the handler never fired before a reload";

    writeAt(markup_, kMarkupB, 2);
    ASSERT_TRUE(d.Update(0.0f));
    EXPECT_EQ(binds, 2) << "bind did not run after a reload";
    EXPECT_NE(boundButton, first)
        << "the tree was not rebuilt, so this test proves nothing";

    // The click still works on the NEW tree. Without re-binding this is exactly
    // the silent failure hot reload is notorious for: the button is drawn, it
    // highlights, and nothing happens.
    d.document().Layout(400.0f, 200.0f);
    clickAt(d.document(), 40.0f, 20.0f);
    EXPECT_EQ(clicks, 2) << "the handler was not re-attached after the reload";
}

// Bind runs LAST, on a finished tree: styles already applied and every child
// present. Anything else and an app would re-cache pointers into a half-built
// document or read styles that are about to be overwritten.
TEST_F(UIHotReloadTest, BindSeesAFullyStyledTree) {
    UIAssetDocument d;
    float seenWidth = -1.0f;
    size_t seenChildren = 0;
    ASSERT_TRUE(d.Load(markup_, style_, [&](UIDocument& doc) {
        seenChildren = doc.root().children().size();
        if (UIElement* bar = doc.root().Find("bar")) seenWidth = bar->style().width.value;
    }));
    EXPECT_EQ(seenChildren, 2u);
    EXPECT_FLOAT_EQ(seenWidth, 100.0f) << "bind ran before the stylesheet was applied";
}

// A failed markup load must not run bind either: there is no new tree, the
// app's existing pointers are still valid, and re-binding would double up every
// listener on the still-live document.
TEST_F(UIHotReloadTest, BindDoesNotRunWhenTheReloadFails) {
    UIAssetDocument d;
    int binds = 0, clicks = 0;
    ASSERT_TRUE(d.Load(markup_, style_, [&](UIDocument& doc) {
        ++binds;
        if (UIElement* b = doc.root().Find("go")) b->OnClick([&](UIEvent&) { ++clicks; });
    }));
    pollEveryFrame(d);

    writeAt(markup_, "<UI", 2);
    ASSERT_FALSE(d.Update(0.0f));
    EXPECT_EQ(binds, 1) << "bind ran on a failed reload";

    d.document().Layout(400.0f, 200.0f);
    clickAt(d.document(), 40.0f, 20.0f);
    EXPECT_EQ(clicks, 1) << "the surviving handler fired more than once";
}

TEST_F(UIHotReloadTest, MissingFilesFailCleanlyAndAreReported) {
    UIAssetDocument d;
    EXPECT_FALSE(d.Load("no_such_ui_98765.uxml", "no_such_ui_98765.uss"));
    EXPECT_FALSE(d.errors().empty());
    // Still safe to drive: a game must not have to null-check its HUD.
    EXPECT_FALSE(d.Update(1.0f));
    d.document().Layout(400.0f, 200.0f);
    EXPECT_EQ(d.document().root().children().size(), 0u);
}

// Markup is authored content headed for a parser, so the same containment gate
// applies here as to models, scripts, clips and HDRis.
TEST_F(UIHotReloadTest, RejectsMarkupOutsideTheProject) {
    UIAssetDocument d;
    EXPECT_FALSE(d.Load("../../evil.uxml", ""));
    ASSERT_FALSE(d.errors().empty());
    EXPECT_NE(d.errors()[0].find("outside the project"), std::string::npos)
        << "expected a containment rejection, got: " << d.errors()[0];
}

// A stylesheet is optional: markup alone must load, so a UI can be pure
// structure plus inline styles.
TEST_F(UIHotReloadTest, StylesheetIsOptional) {
    UIAssetDocument d;
    ASSERT_TRUE(d.Load(markup_, ""));
    EXPECT_TRUE(d.ok()) << (d.errors().empty() ? "" : d.errors()[0]);
    EXPECT_NE(d.document().root().Find("bar"), nullptr);

    pollEveryFrame(d);
    writeAt(markup_, kMarkupB, 2);
    EXPECT_TRUE(d.Update(0.0f)) << "watching broke without a stylesheet";
    EXPECT_NE(d.document().root().Find("bar2"), nullptr);
}
