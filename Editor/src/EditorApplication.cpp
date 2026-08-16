#include <glad/glad.h> // raw GL for the Game view's framebuffer restore

#include "EditorApplication.h"

#include "Engine.h"                 // Renderer/Scene/Shader headers aggregated or include individually
#include "EditorImGuiLayer.h"
#include "EditorTitleBar.h"         // borderless window + custom title-bar caption
#include "ImGuiInputMap.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

#include "imgui.h"
#include "imgui_internal.h"  // BeginViewportSideBar: stack the title + menu rows
#include "ImGuizmo.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>


//for the future for any initalization things that are required
void EditorApplication::Initialize()
{
    // Load resources once during initialization:
    MyCoreEngine::SetImageFlipVerticallyOnLoad(true); // or false

#ifdef CSE_EDITOR_TITLE_PANELS
    // Panels supplied by the title this editor was built with. The macro is not
    // set in Editor/CMakeLists.txt -- it arrives as an INTERFACE compile
    // definition on the title's editor library, so this call exists exactly
    // when something is linked that defines it. See EditorPanel.h.
    //
    // Here rather than in Run(): the registry only allocates and stores, it
    // touches no GL and no ImGui context, and doing it before the window opens
    // means a title whose panel construction is expensive pays for it during
    // startup rather than in the first frame.
    editor::RegisterTitlePanels(titlePanels_);
#endif

#ifdef CSE_HOST_TITLE_MODES
    // The game modes this build ships (Engine/src/core/GameMode.h). Same seam
    // as the panels above and the same guard mechanism -- the macro is not set
    // in Editor/CMakeLists.txt, it rides in as an INTERFACE compile definition
    // on UntitledFighterModes, so LINKING THE LIBRARY IS TURNING THE HOOK ON.
    // An editor built with no title compiles this call out, keeps an empty
    // registry, and previews the menu it previewed before modes existed.
    //
    // DECLARED IN THE ENGINE PRECISELY SO THIS HOST CAN CALL IT. The Player has
    // been calling the identical function since the seam landed; that both hosts
    // enter modes through one registration function is what stops the editor's
    // preview and the shipped game from drifting into two lists.
    //
    // Here, beside the panels, for the reason written above them and for one
    // more: registration order is MENU order (GameMode.h), and the menu that
    // reads it is installed in Run(). Registering in the constructor-side pass
    // means the registry is finished before anything can look at it, whatever
    // order Run() ends up doing things in.
    //
    // Nothing here touches GL either: a mode is constructed once and only
    // allocates in Enter(), which is the point of the registry owning them for
    // the process rather than building one per visit.
    MyCoreEngine::RegisterTitleGameModes(modes_);
#endif

    // What ships. Read here, beside the two registrations above and for the same
    // reason: it touches no GL and no ImGui context, and doing it before the
    // window opens means the Build Settings panel's first draw shows the real
    // list rather than a default that flickers into the real one.
    //
    // The path is working-directory-relative, exactly like ProjectSettings --
    // which is the same directory this editor already stages Exported/ into.
    loadBuildSettings_();
}

void EditorApplication::Run() {
    using namespace MyCoreEngine;

    Scene scene;

    SetOnContextReady([this, &scene]() {
        // GL context + GLAD are ready here
        ui_.Init(GetNativeWindow());

        // Strip the OS title bar and drive our own (drawn in DrawMainMenuBar).
        // Must run after ui_.Init so we subclass ON TOP of ImGui's GLFW
        // wndproc hook and chain to it. Native resize/snap/maximise survive;
        // on non-Windows this is a no-op and the OS title bar stays.
        EditorTitleBar::Install(GetNativeWindow());

        // Multi-viewport input routing: keyboard/mouse polls go through
        // ImGui (aggregated across detached OS windows), and the editor
        // drives camera look/zoom itself in DrawViewport — the engine's
        // raw main-window mouse-look can't see panels on other monitors.
        installInput(std::make_unique<ImGuiInputMap>());
        setInternalCameraInput(false);

        // scene renders offscreen; the Scene panel displays (and resizes) it
        sceneTarget_.Create(1280, 720);
        SetSceneRenderTarget(&sceneTarget_);

        // The EDITOR module keeps its own GLAD pointer table — the engine's
        // loader only fills the DLL's. Raw GL calls from editor code (the
        // Game view's framebuffer restore) segfault on null pointers
        // without this. (Same per-module lesson as the test exes.)
        gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

        // Game view: separate target + renderer keyed to the game camera's
        // frustum (rendered on demand in DrawGameViewport)
        gameTarget_.Create(1280, 720);
        gameRenderer_.Setup(gameTarget_.width(), gameTarget_.height());

        // SAFE: capture 'this' (EditorApplication) whose lifetime spans the run loop
        SetUICaptureProvider([this] {
            // Camera keys act only when the user is actually IN the viewport:
            // focused, hovered, or mid-RMB-look. The old rule ("fly unless
            // typing") predates multi-viewports — with input aggregated
            // across detached OS windows, it moved the camera while
            // scrolling/keyboarding in panels on other monitors. Text editing
            // still blocks regardless (WantTextInput).
            const bool inViewport = viewportFocused_ || viewportHovered_ || camLooking_;
            MyCoreEngine::Application::UICapture caps;
            // The GAME's UI counts too, not just ImGui's. inViewport follows
            // HOVER of the Scene image, while the game UI keeps receiving input
            // as long as the Game surface holds focus -- so moving the cursor
            // from the Game panel across the Scene viewport used to make capK
            // false while a game TextField still had focus, and Escape then
            // reached the quit path and closed THE EDITOR, losing unsaved work.
            //
            // A GAME MODE IS THE SAME BUG ONE LAYER UP, and it is worse. A fight
            // is not a UI document, so uiWorld_.wantsKeyboard() is false for the
            // whole time one is on screen: with the Game surface focused and the
            // cursor merely resting over the Scene image, capK went false, the
            // fly camera took A/D/W (which are also Fight.Left/Right/Up) AND
            // RunLoop's "Quit" check ran -- and "Quit" is Escape and gamepad
            // BACK, which is exactly what a mode means by "leave". Pressing Back
            // in a fight would have closed the editor. IGameMode::OwnsScreen
            // documents the pairing; modeHasTheKeyboard_() is this host's half.
            // A MODE OWNS THE KEYBOARD FOR AS LONG AS IT OWNS THE SCREEN, and
            // NOT only while the Game surface holds focus. Two bugs made that
            // the safe reading, and the second is why it is not focus-gated.
            //
            // FIRST, THE STALE TERM. `uiWorld_.wantsKeyboard()` is
            // `document().focused() != nullptr`, and uiWorld_ is NOT UPDATED AT
            // ALL while a mode owns the screen -- exactly the staleness the
            // textInput line below already guards against, in the same function,
            // with the reason written next to it. It was left unguarded here.
            // And it is reliably TRUE on the way in: a <Button> is focusable by
            // default and click-to-focus fires on the press edge, so the very
            // press that enters the mode focuses TRAINING and freezes that
            // answer for the whole fight. capK was therefore true forever, the
            // fly camera never came back, and the toolbar tooltip told the
            // author to click the Scene view -- which did nothing. The label
            // added to explain the input rules was itself misdirecting the fix.
            //
            // SECOND, AND THE REASON THE FOCUS GATE IS GONE. Guarding the stale
            // term alone leaves capK = `!inViewport` when a mode is up and the
            // author has clicked away: cursor resting over the Scene image makes
            // it FALSE, RunLoop's "Quit" check runs, and "Quit" is Escape and
            // gamepad BACK -- which is what a mode means by "leave". Pressing
            // Back in a fight would close THE EDITOR with unsaved work. That is
            // verbatim the failure the paragraph above says was eliminated, and
            // a focus-gated claim re-opens it every time focus moves.
            //
            // So the claim follows OwnsScreen, which is the thing that is true
            // for exactly as long as the hazard exists. The cost is real and
            // small: the Scene view's fly camera cannot be flown while a fight
            // is running in the Game panel. Stop gives it back, and the tooltip
            // says so. modeHasTheKeyboard_() stays -- the TOOLBAR still wants
            // the narrower question, because "where is input going" and "may the
            // editor act on this key" are different questions and only the
            // second one is a safety property.
            caps.keyboard = modes_.activeOwnsScreen() || ui_.WantTextInput() ||
                            (!modes_.activeOwnsScreen() && uiWorld_.wantsKeyboard()) ||
                            !inViewport;
            // the viewport is an ImGui window too — camera controls
            // must keep working while the mouse is over it
            caps.mouse = ui_.WantCaptureMouse() && !viewportHovered_;
            // Reported separately because `keyboard` above is mostly about
            // WHERE the pointer is, not about typing. Gameplay input keys off
            // this narrow flag alone.
            // Same widening as above: a space typed into the GAME'''s text field
            // is content, and without this it also fires whatever gameplay
            // action is bound to Space (InputMap binds "Jump" there by default).
            //
            // The previewed document's half drops out entirely while a mode owns
            // the screen, and NOT because of focus -- uiWorld_ is not updated at
            // all then, so whatever a menu text field believed on the way in is
            // frozen there for the whole fight. RunLoop clears the press latches
            // every frame `typing` is true, which would eat the mode's input
            // silently. Same reasoning as PlayerMain's capture provider, same
            // line. (activeOwnsScreen() alone, without the focus AND: staleness
            // is a property of the world not ticking, not of who has focus.)
            caps.textInput = ui_.WantTextInput() ||
                             (!modes_.activeOwnsScreen() && uiWorld_.wantsTextInput());
            return caps;
        });
    });

    SetUIDraw([this, &scene](float dt) {

        // ---- the active mode's way out -------------------------------------
        //
        // A mode asks to leave (its Escape, its Back button, a match ending) and
        // the host drains the request at a frame boundary. GameMode.h is
        // emphatic about WHERE: not in a variable-rate update subscriber, which
        // is skipped while paused, at timeScale 0 and on the frame of a scene
        // swap -- a mode that asked to leave in any of those states would stay up
        // forever with its own Escape apparently dead. The editor has all three
        // states and a fourth of its own (edit mode gates the hooks off outright).
        //
        // THIS callback is the editor's "runs every frame regardless". It is
        // Application::SetUIDraw, called unconditionally from RunLoop after the
        // 3D pass, and it is the same slot PlayerMain drains from -- with one
        // difference worth stating, because the obvious alternative is wrong: the
        // GAME renderer's UI draw (where the mode's Draw lands) does NOT run when
        // the Game panel is closed, collapsed or camera-less, so draining there
        // would tie a mode's ability to leave to a panel being visible.
        //
        // FIRST in the frame, so a mode that asked to leave never draws again:
        // DrawGameViewport is called from further down this same lambda.
        modes_.DrainExitRequest();

        // Apply a requested layout between frames: LoadIniSettingsFromDisk
        // re-applies settings to live windows through the settings handlers'
        // ApplyAll, which must run outside NewFrame/Render.
        if (!pendingLayoutLoad_.empty()) {
            ImGui::LoadIniSettingsFromDisk(pendingLayoutLoad_.c_str());
            pendingLayoutLoad_.clear();
        }

        ui_.BeginFrame();
        ImGuizmo::BeginFrame();
        // one dockspace over the whole window: every panel becomes dockable
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        // File menu under the title bar (scene new/open/save/save-all).
        DrawMainMenuBar(scene);

        // Inspector arbitration baseline: captured BEFORE any panel runs so
        // viewport picks/drops count as "newly selected this frame" too
        const entt::entity selAtFrameStart = selected_;

        if (panels_.scene) DrawViewport(scene);
        else {
            // Same hazard the Game panel's else-branch below guards, and the
            // same fix. These three are written ONLY inside DrawViewport, so
            // closing the Scene panel froze them at whatever they were: close
            // it while hovering and `inViewport` stays true forever, so the
            // capture provider keeps handing the fly camera every keystroke and
            // Esc, and the game's UI never gets a key again. Closing it
            // mid-right-drag latched camLooking_ the same way.
            viewportHovered_ = false;
            viewportFocused_ = false;
            camLooking_ = false;
        }

        //Information Panel (reads the SCENE view's render stats — draw it
        //before the Game view renders and overwrites them)
        if (panels_.information) DrawInformationPanel(scene, dt);

        //Game view: what the primary camera entity sees
        if (panels_.game) DrawGameViewport(scene, *sceneShader_, dt);
        else {
            // The gate is only updated INSIDE the panel, so closing the Game
            // view while it had focus during Play left gameplay input enabled
            // forever: Scene-view keys then drove the fly camera AND the
            // running scripts at once, with the explanatory hint hidden along
            // with the panel. No panel means no game focus.
            gameViewFocused_ = false;
            setGameplayInputEnabled(false);
        }

        // Engine/render controls. These used to be bare CollapsingHeaders,
        // which ImGui collects into its implicit "Debug##Default" fallback
        // window — and the fallback window can never dock. A real named
        // window makes them a first-class dockable panel.
        if (panels_.settings) {
        ImGui::SetNextWindowSize(ImVec2(360, 540), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings", &panels_.settings)) {
            // Grouped into tabs so rendering options live in one place and
            // editor/workflow options in another, instead of one flat list of
            // headers mixing "save scene" with "shadow bias". Scene = the file
            // + world; Rendering = everything visual; Editor = the tool itself.
            if (ImGui::BeginTabBar("SettingsTabs")) {
                if (ImGui::BeginTabItem("Rendering")) {
                    // Quality preset (HDRP-lite tiers): applies a performance
                    // preset across the render settings below. Custom = leave the
                    // individual settings untouched.
                    {
                        const char* kQ[] = { "Low", "Medium", "High", "Custom" };
                        int q = static_cast<int>(scene.GetQualityLevel());
                        ImGui::SetNextItemWidth(160.f);
                        if (ImGui::Combo("Quality", &q, kQ, IM_ARRAYSIZE(kQ))) {
                            renderer().ApplyQualityTier(
                                static_cast<MyCoreEngine::Scene::QualityLevel>(q), scene);
                            forceAllCSMUpdate_(); // shadow cascades/res may have changed
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "Low / Medium / High apply a performance preset\n"
                                "(geometry LOD, projected-size cull, shadows, bloom, AA).\n"
                                "Aesthetic post (outline/grade/vignette) is left as you set it.\n"
                                "Custom leaves everything untouched.");
                        ImGui::Separator();
                    }

                    // Lighting: the sun + its shadows and the scene's direct
                    // light, together -- editing one usually means the other.
                    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::TreeNodeEx("Sun & Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                            DrawSunShadowControls(scene);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Direct Light")) {
                            DrawLightControls(scene);
                            ImGui::TreePop();
                        }
                    }
                    DrawIBLHDRControls(scene);   // "Environment"
                    DrawRenderingToggles(scene); // "Post & Toggles"
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Editor")) {
                    // What happened at startup: loaded the project's startup
                    // scene, or fell back to a generated default. This was
                    // computed and only ever printed to stdout, so a user with
                    // no terminal could not tell a loaded scene from a
                    // defaulted one — while four doc pages told them to read it
                    // here.
                    if (!bootStatus_.empty()) {
                        ImGui::TextDisabled("Startup");
                        ImGui::TextWrapped("%s", bootStatus_.c_str());
                        ImGui::Separator();
                    }
                    DrawTimeControls();
                    DrawInputPanel();
                    DrawLayoutControls();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Audio")) {
                    // The active backend after fallback: "Null" here means the
                    // device failed to open (headless / no sound card), so
                    // everything runs but stays silent.
                    ImGui::TextDisabled("Backend: %s", audio_.BackendName().c_str());
                    ImGui::Spacing();
                    ImGui::SetNextItemWidth(200.f);
                    // AlwaysClamp: Ctrl+Click accepts a typed value, and a >1
                    // master gain blew out the whole mix and was written to
                    // project.json, then silently clamped back on next boot.
                    if (ImGui::SliderFloat("Master volume", &masterVolume_, 0.0f, 1.0f, "%.2f",
                                           ImGuiSliderFlags_AlwaysClamp)) {
                        audio_.SetMasterVolume(masterVolume_); // live while dragging
                    }
                    // Persist once the drag settles, not every frame: master
                    // volume ships in project.json and the player boots at it.
                    if (ImGui::IsItemDeactivatedAfterEdit()) saveMasterVolume_();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Scales the whole mix. Saved to project.json;\n"
                                          "the shipped player boots at this volume.\n"
                                          "Per-source volume is on the Audio Source component.");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        }

        //asset browser (P2-5) + async model ops (P4-3 phase 3).
        // Drawn BEFORE hierarchy/inspector so an asset click can hand the
        // Inspector over this same frame; an entity click later in the
        // frame wins back (Unity-style: last selection wins).
        {
            // finished AssetCooker run: collect the report, free the child
            if (validateRun_ && validateRun_->done) {
                validateRun_->reader.join();
                const int rc = validateRun_->proc.wait(); // reap on the main thread
                validateReport_ = std::move(validateRun_->output);
                validateReport_ += "\n(exit code " + std::to_string(rc) +
                    (rc == 0 ? " - clean)" : rc == 1 ? " - errors found)" : ")");
                validateRun_.reset(); // Subprocess dtor closes the child handles
                validateRunning_ = false;
                validateOpen_ = true; // reopen even if closed mid-run
            }

            // A running build, drained EVERY FRAME and NOT gated on
            // panels_.build. Poll() is what moves the child's stdout out of the
            // pipe; stop calling it and the OS buffer fills and the compiler
            // BLOCKS ON A WRITE -- so closing the Build Settings window would
            // wedge a build that was working, with no error anywhere and a
            // Cancel button that is no longer being drawn. Same reason the
            // AssetCooker collection above sits outside `if (panels_.assets)`.
            pollBuild_();

            assetIndex_.tick(dt, &jobs()); // throttled; the walk runs on a worker
            // gated off during play: an edit-mode drop landing mid-play
            // would spawn into the play scene, record no undo (recording
            // disabled), and be silently destroyed by Stop's restore —
            // deferring applies it to the restored edit scene instead
            if (!playing_) pollPendingModelOps_(scene);
            // Panels the editor did not compile, supplied by the title (see
            // EditorPanel.h). A no-op in a title-free build.
            //
            // Drawn HERE, among the editor's own panels and after the asset
            // tick, rather than in a section of their own: a title panel is a
            // dockable ImGui window like any other and the ordering that
            // matters is the one below -- assets before hierarchy/inspector, so
            // an asset click can hand the Inspector over this same frame. A
            // title panel participates in no such arbitration and so has no
            // claim on a particular slot.
            {
                editor::PanelContext pctx;
                // The editor's asset root, so a panel that opens authored files
                // resolves them the same way every other editor path does
                // rather than hardcoding a guess of its own.
                pctx.contentRoot = "Exported";
                pctx.playing = playing_;
                titlePanels_.DrawVisible(pctx);
            }

            // The asset SCAN tick above always runs; only the panel draw and
            // its action handling are gated by visibility.
            if (panels_.assets) {
                const AssetBrowserActions aba = assetBrowser_.Draw(
                    scene.registry, selected_, assetIndex_, playing_,
                    (int)pendingModelOps_.size(), validateRunning_, &panels_.assets);
                if (!aba.loadScene.empty() && !playing_) {
                    loadSceneFromFile_(scene, aba.loadScene);
                }
                if (!aba.setStartup.empty()) {
                    setStartupScene_(aba.setStartup);
                }
                if (!aba.spawnModel.empty()) {
                    spawnModelEntity_(scene, aba.spawnModel,
                                      camera().Position + camera().Front * 10.f);
                }
                if (!aba.assignModel.empty()) {
                    assignModelToEntity_(scene, aba.assignModel, selected_);
                }
                if (aba.validateRequested && !validateRunning_) startValidate_();
                // hand the INSPECTOR to the asset view; the entity selection
                // itself survives — "Assign to Selected Entity" depends on it
                if (aba.assetClicked) inspectorShowsAsset_ = true;
            }

            // Build Settings. The DRAW is gated on visibility; pollBuild_()
            // above is not, so a build survives this window being closed.
            if (panels_.build) {
                BuildPanelInputs bin;
                bin.load            = buildLoad_;
                bin.dirty           = buildDirty_;
                bin.preflight       = buildPreflightValid_ ? &buildPreflight_ : nullptr;
                bin.preflightStale  = buildPreflightStale_;
                bin.building        = (buildJob_ != nullptr);
                bin.progress        = buildProgress_;
                bin.report          = buildHaveReport_ ? &buildReport_ : nullptr;
                bin.log             = &buildLog_;
                bin.currentScene    = currentScenePath_;
                bin.playing         = playing_;
                bin.environmentProblem = buildEnvironmentProblem_();
                bin.focus           = buildFocus_;
                buildFocus_ = false; // one-shot; the menu item sets it again

                const BuildSettingsActions bpa =
                    buildPanel_.Draw(buildSettings_, bin, &panels_.build);

                // A list edit invalidates the cached preflight but does NOT
                // trigger a new one: preflight stats the scene list and walks
                // the staged asset root, and running that per keystroke in the
                // output field is exactly the per-frame cost its header warns
                // against. It goes stale, the panel says so, and the next
                // explicit Check (or the Build itself) resolves it.
                if (bpa.settingsChanged) {
                    buildDirty_ = true;
                    buildPreflightStale_ = true;
                }
                if (bpa.reloadRequested)   loadBuildSettings_();
                // Save and Discard are the same write; they differ only in what
                // the author agreed to. saveBuildSettings_ refuses a Malformed
                // load, so Discard clears that standing FIRST -- otherwise the
                // confirmation the panel just collected would do nothing.
                if (bpa.discardRequested) {
                    buildLoad_ = MyCoreEngine::BuildSettingsLoadResult{};
                    buildLoad_.status = MyCoreEngine::BuildSettingsStatus::Ok;
                    saveBuildSettings_();
                }
                else if (bpa.saveRequested) {
                    saveBuildSettings_();
                }
                if (bpa.preflightRequested) runBuildPreflight_();
                if (bpa.buildRequested)     startBuild_();
                if (bpa.cancelRequested && buildJob_) buildJob_->RequestCancel();
                // Opening a scene from the build list goes down the SAME path as
                // the asset browser's double-click: it clears the undo history
                // and forces a CSM rebuild, and a second copy of that would be a
                // second place to forget one of them.
                if (!bpa.openScene.empty() && !playing_) {
                    loadSceneFromFile_(scene, bpa.openScene);
                }
            }
        }

        // validation report window (AssetCooker child-process output)
        if (validateOpen_) {
            ImGui::SetNextWindowSize(ImVec2(560, 320), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Asset Validation", &validateOpen_)) {
                if (validateRunning_) {
                    ImGui::TextDisabled("running AssetCooker validate...");
                }
                else {
                    ImGui::BeginChild("##valout", ImVec2(0, 0), 0,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::TextUnformatted(validateReport_.c_str());
                    ImGui::EndChild();
                }
            }
            ImGui::End();
        }

		//scene hierarchy
        if (panels_.hierarchy) {
            bool casterSetChanged = false;
            hierarchy_.Draw(scene.registry, selected_, undo_, &panels_.hierarchy,
                            &casterSetChanged);
            // A deleted entity leaves its shadow baked into the cascades: the
            // caster set changed but no transform dirtied, which is the only
            // thing the incremental CSM update watches.
            if (casterSetChanged) forceAllCSMUpdate_();
        }

		//inspector: an entity newly selected this frame (hierarchy click,
		// viewport pick, spawn landing) reclaims it from the asset view;
		// a highlighted asset that vanished from disk drops back too
        if (panels_.inspector) {
        if (selected_ != entt::null && selected_ != selAtFrameStart) {
            inspectorShowsAsset_ = false;
        }
        const MyCoreEngine::AssetIndex::Node* assetNode = nullptr;
        if (inspectorShowsAsset_) {
            if (!assetBrowser_.selectedAsset().empty()) {
                assetNode = assetIndex_.find(assetBrowser_.selectedAsset());
            }
            if (!assetNode) {
                assetBrowser_.clearAssetSelection();
                inspectorShowsAsset_ = false;
            }
        }
        if (assetNode) {
            inspector_.DrawAsset(assetNode, &panels_.inspector);
        }
        else if (inspector_.Draw(scene.registry, selected_, undo_, assets_.get(), &scripts_,
                                 &audio_, &panels_.inspector)) {
            // caster set changed without a transform dirtying (model swap /
            // remove / shadow toggle): stale shadows stay baked otherwise
            forceAllCSMUpdate_();
        }
        }

        // commit any edit whose widget stopped being submitted this frame
        // (deselect while a text field was focused, tab switch, collapse)
        undo_.tickFrame(scene.registry);

        //undo/redo history (P2-7)
        if (panels_.edit) DrawEditHistory(scene);

        // Ctrl+Z / Ctrl+Y (+ Ctrl+Shift+Z). Not while typing in a text
        // field (ImGui's own text-edit undo owns Ctrl+Z there), not while
        // a drag is in flight (rewinding history mid-manipulation corrupts
        // it — ImGuizmo would stomp the undone transform from its
        // drag-start anchors and the release-time push would erase the
        // entry that was just undone), and not during play (play-mode
        // changes are discarded on Stop, not undone).
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput && !gameIsTyping_() &&
            !ImGuizmo::IsUsing() && !undo_.editActive() &&
            ImGui::GetDragDropPayload() == nullptr &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            // popup gate: starting play with a modal open would soft-lock it
            // (modal buttons inherit the play-mode disabled flag, modals
            // can't be Escape-closed, and the modal blocks clicking Stop)
            // the drag gates also protect Ctrl+P: toggling play mid-drag
            // would snapshot/restore around a half-applied manipulation and
            // leak play-pose transforms into the edit scene and history.
            // The drag-drop gate stops undo/redo from destroying an entity
            // whose handle is mid-flight in a hierarchy drag payload.
            if (!playing_) {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                    if (io.KeyShift) doRedo_(scene); else doUndo_(scene);
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                    doRedo_(scene);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
                if (playing_) stopPlay_(scene); else startPlay_(scene);
            }
        }

        ui_.EndFrame();
    });

    // Edit mode by default: gameplay hooks (FixedUpdate/Update) only tick
    // between Play and Stop. The Player never touches this and always ticks.
    setGameplayEnabled(false);

    // Make GL ready before creating any GL objects (Shaders, Models)
    InitGL();
    assets_ = std::make_unique<AssetManager>(); // create after GL is ready

    // Scene swapping goes through one place now. Created here because it needs
    // both the Scene and the AssetManager, and BEFORE the Install* calls below
    // so physics/scripts/audio can subscribe their own teardown to it.
    sceneLoader_ = std::make_unique<MyCoreEngine::SceneLoader>(scene, *assets_);
    // The Application's pool, so RequestSwapAsync can warm a scene's models on
    // workers instead of importing them inside the swap. Without this the async
    // request is simply the synchronous one.
    sceneLoader_->SetJobSystem(&jobs());
    setSceneLoader(sceneLoader_.get());
    editModeGate_.playing = &playing_;
    sceneLoader_->AddObserver(&editModeGate_);
    // The editor's own derived state. This used to live inside
    // loadSceneFromFile_, which is exactly why a GAME-initiated swap (a menu
    // button in the running game) would have left the editor holding stale
    // handles: there was no way to reach it except through the File menu.
    sceneLoader_->AddObserver(
        [this](MyCoreEngine::Scene&) {          // will unload
            selected_ = entt::null;   // every entity handle is about to die
            undo_.clear();            // ...including all of the history's
            gameDirector_.reset();    // ...and the Game view's camera handles
            pendingModelOps_.clear(); // in-flight ops were aimed at the old scene
            // A mode does not survive the scene it was entered from. It holds no
            // entity handle -- UntitledFighterMode never touches ctx_.scene at
            // all -- so this is not the stale-handle reset the four lines above
            // are. It is that a mode is entered from a MENU in a particular
            // scene, and a swap means the game went somewhere else; leaving it up
            // over the new scene would also leave its action names (Fight.*)
            // bound in the shared InputMap of a scene that never asked for them.
            //
            // HERE rather than at the call sites, for the reason the four lines
            // above are here: this observer is the only thing every swap goes
            // through, including the GAME-initiated ones the editor did not
            // start. Leave() is safe and idempotent with nothing active, so the
            // paths that already left (stopPlay_) pay nothing to pass through.
            modes_.Leave();
            // Play is now standing on a scene it did not start in, so Stop
            // cannot restore its snapshot over the top. Deliberately does NOT
            // clear playSnapshot_: it is the fallback if the reload fails.
            if (playing_) playSwapped_ = true;
        },
        [this](MyCoreEngine::Scene& s) {        // did load
            // Re-apply the quality tier: the perf toggles serialize, but the
            // CSM cascade/resolution half of a tier lives on the Renderer and
            // is not serialized, so a loaded High scene would get default
            // shadows without this.
            if (s.GetQualityLevel() != MyCoreEngine::Scene::QualityLevel::Custom)
                renderer().ApplyQualityTier(s.GetQualityLevel(), s);
            // Wholesale replacement bypasses the departure-sphere dirty-caster
            // flow: the old scene's shadows would stay baked into cascades the
            // new content never touches.
            forceAllCSMUpdate_();
        });
    // Report what a swap did, wherever it came from — including the ones the
    // editor did not initiate.
    sceneLoader_->SetOnSwapComplete([this](const MyCoreEngine::SceneSwapResult& r) {
        // The menu's log first: it touches only the shared data source, which
        // is safe even on the re-entrant path (RequestSwap reports Invalid and
        // Superseded synchronously, from inside a UI action).
        MyCoreEngine::MenuUIReportSwap(uiWorld_, r);
        switch (r.status) {
        case MyCoreEngine::SceneSwapStatus::Ok:
            // The File menu's target follows the scene that is actually open,
            // so a game-initiated swap doesn't leave Save pointing at the file
            // the author left three scenes ago.
            std::snprintf(currentScenePath_, sizeof(currentScenePath_), "%s",
                          r.path.c_str());
            setSceneStatus_(r.report.complete()
                ? "Loaded " + r.path
                : "Loaded " + r.path + " - " +
                      std::to_string(r.report.failedModels.size() +
                                     r.report.rejectedModels.size()) +
                      " model(s) missing (see log)");
            break;
        case MyCoreEngine::SceneSwapStatus::Superseded:
            break; // a request replaced before it ran is not news
        default:
            setSceneStatus_("Scene load FAILED: " + r.message);
            break;
        }
    });
    std::unique_ptr<Shader> shader = std::make_unique<Shader>("Exported/Shaders/vertex.glsl",
        "Exported/Shaders/frag.glsl");
    sceneShader_ = shader.get(); // the Game view renders with the same shader

    assert(glfwGetCurrentContext() != nullptr);

    // Boot content comes from the SCENE FILE, never from code.
    //
    // This used to build a 20x20 backpack grid + Ground + Hero + Main Camera
    // here on every launch, which made the editor lie about what a scene
    // contains: your saved file was never what you saw at startup, so
    // authored components (physics especially) looked like they "didn't
    // save" — they saved fine, the hardcoded scene just replaced them before
    // you ever saw them. The editor now opens the same startup scene the
    // player ships with, so the two agree by construction.
    {
        MyCoreEngine::ProjectSettings settings;
        settings.Load(); // Exported/project.json
        masterVolume_ = settings.masterVolume; // mirror it (AudioWorld has no getter)
        const std::string startup = settings.startupScene.empty()
            ? std::string("Exported/scene.json") : settings.startupScene;

        MyCoreEngine::SceneSerializer bootSerializer(scene, *assets_);
        if (bootSerializer.Load(startup)) {
            bootStatus_ = "Loaded startup scene: " + startup;
            // Same re-apply loadSceneFromFile_ does. The CSM half of a tier
            // lives on the Renderer and is deliberately not serialized, so
            // without this the boot path was the ONE entry into a file that
            // left default shadows: opening the editor and then pressing Load
            // Scene on the very same file visibly changed shadow quality, and
            // only the second state matched the shipped player.
            if (scene.GetQualityLevel() != MyCoreEngine::Scene::QualityLevel::Custom)
                renderer().ApplyQualityTier(scene.GetQualityLevel(), scene);
        }
        else {
            createDefaultScene_(scene);
            bootStatus_ = "No scene at '" + startup + "' — created a default scene";
        }
        std::cout << "EDITOR: " << bootStatus_ << std::endl;
    }

    // Demo gameplay (shared with the standalone player): spin entities named
    // "Hero". Only ticks between Play and Stop here — the gameplay gate is
    // off in edit mode.
    // Physics steps on the fixed tick as a SUBSCRIBER, so a game's own
    // gameplay hook (Application::SetFixedUpdate) still composes with it.
    // Bodies are built on Play (startPlay_) and destroyed on Stop, so edit
    // mode stays static.
    MyCoreEngine::InstallPhysics(*this, scene, physics_);

    // AFTER physics, so a script's OnFixedUpdate sees the poses the solver
    // just produced rather than last tick's.
    // Input is deliberately NOT bound here: installInput() can swap the map
    // (the editor uses an ImGui-routed one that aggregates across detached
    // viewports), which would leave a dangling pointer. startPlay_ binds
    // whatever map is current instead.
    {
        MyCoreEngine::ScriptSettings ss;
        ss.scriptDirectory = "Exported/Scripts";
        MyCoreEngine::InstallScripting(*this, scene, scripts_, &physics_, nullptr, {}, ss);

        // In-game UI on the GAME renderer only. The Scene view is the authoring
        // camera, so game UI would just be in the way there; the Game view is
        // the "what ships" preview and must show the same HUD the Player draws,
        // built from the same definition so the two cannot drift.
        // The UI comes from the SCENE: entities carrying a UIDocumentComponent,
        // whose .cxml and .cstyle hot-reload. Edit either while the editor runs and
        // the Game view updates in place, with no rebuild and without losing
        // the scene you were testing.
        // The atlas size is the unit `font-scale` multiplies, so it is a shared
        // constant rather than a literal here -- see Font.h.
        if (!uiFont_.LoadFromFile("Exported/Fonts/Roboto.ttf",
                                  MyCoreEngine::kUIFontAtlasPixels)) {
            std::cout << "EDITOR: UI font missing - drawing without text" << std::endl;
        }
        uiWorld_.SetFont(&uiFont_);
        // The image cache lives with the host, one per GL context: the editor
        // runs a SECOND renderer for its Game view, and a process-wide cache
        // would hand one context's texture names to the other.
        uiWorld_.SetTextureCache(&uiTextures_);
        // ImGui's clipboard rather than GLFW's: it is already wired to the
        // platform backend and is what the rest of the editor uses.
        uiWorld_.SetClipboardHandlers(
            [](const std::string& t) { ImGui::SetClipboardText(t.c_str()); },
            [] { const char* t = ImGui::GetClipboardText(); return std::string(t ? t : ""); });
        // The two things a file cannot carry: a named action and a converter.
        MyCoreEngine::InstallDemoUIContent(uiWorld_);

        // ---- game modes ------------------------------------------------------
        //
        // WHAT A MODE IS TO THIS HOST, because the answer decides everything
        // below and half of it is a refusal.
        //
        // A mode is a thing the host is IN: it takes the fixed tick, the screen
        // and the keyboard (GameMode.h). The editor is a host that is also an
        // editor, so all three of those are things it already has other users
        // for, and the shape that falls out is:
        //
        //   A MODE LIVES INSIDE A PLAY SESSION. Play does not enter one directly.
        //   Play starts the session exactly as it always has; the MENU previewed
        //   in the Game view enters the mode, which is precisely how the shipped
        //   player does it and is the flow the author actually reported (Play,
        //   click TRAINING, "No game modes in this build."). Stop leaves it.
        //
        // That is not a preference, it is what the existing gates already say
        // three times over, and it is worth writing them down because each one
        // would have to be UNDONE to get the alternative:
        //
        //   1. The menu's verbs are refused while stopped. `allowHostMutation`
        //      below is [this]{ return playing_; } and MenuUIContent checks it
        //      before every verb including this one, so a mode simply cannot be
        //      entered from edit mode. That hook predates modes entirely.
        //   2. A MODE'S FIXED TICK IS THE PLAY SESSION'S FIXED TICK. RunLoop
        //      skips the gameplay hooks wholesale while gameplayEnabled_ is off,
        //      which is the editor's definition of edit mode, so a mode entered
        //      while stopped would sit on screen and never be ticked once.
        //   3. Its keys are the session's keys. setGameplayInputEnabled is
        //      `playing_ && gameSurfaceFocused_`, and the mode binds A/D/W --
        //      the fly camera's keys -- so a mode reading input outside a
        //      session would drive the fight and the editor camera together.
        //
        // THE ALTERNATIVE, AND WHAT IT WOULD COST. Letting a mode be entered in
        // edit mode means calling setGameplayEnabled(true) for it, and that flag
        // is not a mode switch: it is the ONE gate on the whole fixed pipeline,
        // shared with the physics and scripting subscribers. Turning it on would
        // start stepping the solver over the scene the author is editing, with no
        // Play pressed, no snapshot taken and therefore nothing to restore from
        // -- edit-mode poses mutated by a fight that has nothing to do with them.
        // The narrower version (a second gate that ticks the mode but not the
        // subscribers) is a change to Application's loop, not to this seam, and it
        // buys an entry point the shipped player does not have.
        //
        // DOES STOP ALWAYS LEAVE, OR ONLY WHEN PLAY ENTERED? The question does
        // not arise, and that is the point of answering the first one properly:
        // Play is the only door in, so "a mode is active" implies "Play entered
        // it". stopPlay_ therefore leaves unconditionally, and Leave() is
        // documented safe with nothing active, so the ordinary Stop that never
        // saw a mode pays one branch. A half-answer here -- Stop leaving only the
        // modes it remembers starting -- is exactly what produces a mode still
        // ticking over a stopped scene.
        //
        // AND THE SCENE UNDERNEATH KEEPS RUNNING, deliberately. A fight is not a
        // scene (FightSession owns a POD GameState and no entities), so the
        // loaded scene is not the fight's world -- it is whatever the session was
        // standing in when the menu button was pressed, and the mode paints an
        // opaque backdrop over it. The editor does NOT stop simulating it, for
        // three reasons: the Player does not either, and this panel exists to
        // preview what ships; the scene's ticks come from the same accumulator as
        // the mode's, so suppressing one without the other means unsubscribing
        // physics mid-session; and everything it mutates is play-session state,
        // which Stop discards wholesale (stopPlay_'s snapshot restore) -- so the
        // author's FILE is never touched by a fight running over it. What was
        // worth fixing is not the ticking but the silence about it, and the Game
        // panel's toolbar now names the mode that is up.
        //
        // What DOES stop is the scene's UI: uiWorld_ is not updated while a mode
        // owns the screen (see the Game view's UI draw), because Update is what
        // dispatches clicks and moves focus, and a live menu sitting under a
        // fight takes presses nobody can see. Same rule as the Player, same line.

        // Everything a mode is handed on entry, built in ONE place so that this
        // host cannot hand out two different contexts.
        //
        // A FACTORY rather than a stored struct, for the reason PlayerMain gives:
        // it is evaluated at ENTRY, and the font may not have finished baking when
        // this lambda is written. Everything it names is host-lifetime -- `scene`
        // is Run()'s local, which outlives RunLoop, and the rest are members.
        const auto modeContext = [this, &scene] {
            MyCoreEngine::GameModeContext ctx;
            ctx.app = this;
            ctx.scene = &scene;
            // May be null, which GameModeContext documents as allowed: the font
            // load above prints and carries on.
            ctx.font = uiFont_.IsValid() ? &uiFont_ : nullptr;
            // The editor's asset root -- the same "Exported" every other editor
            // path resolves against, and the same string the title PANELS are
            // handed (editor::PanelContext::contentRoot). A mode that opens
            // authored content joins onto this rather than guessing.
            ctx.contentRoot = "Exported";
            return ctx;
        };

        // The active mode's simulation step. SetFixedUpdate is the PRIMARY fixed
        // slot, reserved by Application.h for "a game's own hook", and the editor
        // has never used it -- physics is an AddFixedUpdate subscriber and is
        // unaffected by taking it. Same slot the Player uses, so a mode is ticked
        // at the same point in the loop in both hosts.
        //
        // GATED BY gameplayEnabled_ ALREADY, which is the editor's edit/play
        // switch: this lambda is simply not called in edit mode. That is why
        // there is no `if (playing_)` here -- a second copy of the gate that
        // could disagree with the first.
        SetFixedUpdate([this](float dt) {
            if (MyCoreEngine::IGameMode* m = modes_.active()) m->FixedTick(dt);
        });
        // Per-frame. A SUBSCRIBER rather than SetUpdate, because scripting
        // already occupies the subscriber list and the primary variable slot is
        // the matching reservation to the one above.
        //
        // This is where the mode reads its Escape (UntitledFighterMode::Update
        // reads "Quit"), so the request is MADE here and DRAINED from the
        // top-level UI draw -- which runs even on the frames this does not.
        AddUpdate([this](float dt) {
            if (MyCoreEngine::IGameMode* m = modes_.active()) m->Update(dt);
        });

        // The menu's verbs, with the editor's three refusals wired in. The Game
        // panel dispatches the running document's clicks even while STOPPED, so
        // without these a menu button in a document being AUTHORED would change
        // the master volume, retune the renderer, or close the editor.
        menuHooks_.app = this;
        menuHooks_.scene = &scene;
        menuHooks_.renderer = &gameRenderer_;   // the view the game is previewed in
        menuHooks_.audio = &audio_;
        menuHooks_.initialVolume = masterVolume_;
        menuHooks_.allowHostMutation = [this] { return playing_; };
        // Quit must not take the EDITOR down. Stopping the session is the
        // honest analogue of a game closing.
        menuHooks_.onQuit = [this, &scene] {
            if (playing_) stopPlay_(scene);
        };
        // The editor keeps its own mirror of a value AudioWorld cannot be asked
        // for, so it owns persistence or the two desync.
        menuHooks_.onMasterVolume = [this](float v) {
            masterVolume_ = v;
            saveMasterVolume_();
        };
        // A tier's CSM half lives on the Renderer and is not serialized, and the
        // editor has TWO renderers.
        menuHooks_.onQualityChanged = [this] { forceAllCSMUpdate_(); };
        // The modes, and the verb that enters one. THIS IS THE LINE THAT USED TO
        // SAY "a known gap": the menu previewed here showed the four authored
        // verbs and none of the mode verbs the shipped player shows, which was
        // the one place this editor did not preview exactly what ships. A title
        // front end loaded into the Game view drew its TRAINING button (it is
        // typed markup, not a bound slot), the click reached `menuEnterMode0`,
        // and the status line answered "No game modes in this build."
        //
        // TWO FIELDS, BOTH OR NOTHING. MenuUIContent publishes
        // `(h.modes && h.onEnterMode) ? h.modes->Count() : 0`, so setting one of
        // them leaves every `if="menuModeN"` slot false and the feature entirely
        // invisible -- no error, no empty button, nothing to notice, on a build
        // whose binary contains the mode. PlayerMain.cpp:451 records that exact
        // one-field miss costing a debugging session; this comment is here so it
        // costs nothing to repeat.
        //
        // `modes` is a pointer, and the flags are re-derived from it every frame
        // by MenuUIPublishCounters below -- so an empty registry (a title-free
        // editor) publishes four absent slots and the previewed menu is identical
        // to the one from before modes existed.
        menuHooks_.modes = &modes_;
        // THE HOST ENTERS, NOT THE MENU: GameModeContext carries this
        // executable's Font and its asset root, which MenuUIContent has no
        // business acquiring. The returned string is empty on success or a
        // message for the menu's own status line -- the same line a failed scene
        // load lands on, which is what the author will be looking at.
        menuHooks_.onEnterMode = [this, modeContext](int index) -> std::string {
            // THE EDITOR'S OWN RULE, STATED BY THE EDITOR. It is currently
            // unreachable -- MenuUIContent consults allowHostMutation first and
            // refuses every verb while stopped -- and it is written anyway,
            // because that hook belongs to the menu and this one belongs to this
            // host. The day a second entry point exists (a toolbar button, a
            // command palette) it will call THIS lambda and not that gate, and
            // "a mode ticks on the play session's fixed tick" is a fact about
            // the editor rather than about the menu.
            if (!playing_) {
                return "Press Play first - a mode runs on the play session's tick";
            }
            std::string error;
            if (!modes_.Enter(index, modeContext(), error)) {
                std::cerr << "[editor] game mode " << index << " refused: "
                          << error << std::endl;
                return error;
            }
            // The Game surface is what feeds a mode its keys
            // (setGameplayInputEnabled is `playing_ && gameSurfaceFocused_`), and
            // it is ALREADY focused on every real path in: the menu button was
            // clicked on that surface, which is what latched the flag. Said out
            // loud rather than assumed, because the toolbar's "Click the image to
            // give input to the game" is the recovery if it ever is not.
            std::cout << "EDITOR: entered game mode '"
                      << modes_.DisplayNameAt(index) << "'." << std::endl;
            return {};
        };
        MyCoreEngine::InstallMenuUIContent(uiWorld_, menuHooks_);
        gameRenderer_.SetUIDraw([this, &scene](MyCoreEngine::Renderer2D& r2d,
                                              int w, int h, float dt) {
            // The Inspector reports what the scale settings resolve to, and this
            // is the only place that knows the real UI surface. FIRST, above the
            // mode branch below, because it is a fact about the SURFACE rather
            // than about what is drawn on it -- leaving it under the branch made
            // the Inspector report the size from before a fight started, for as
            // long as the fight was up.
            inspector_.SetUISurfaceSize(float(w), float(h));

            // ---- the active game mode, first -------------------------------
            //
            // w and h are gameTarget_'s, i.e. THE GAME VIEW'S LETTERBOXED
            // SURFACE, not the editor window's -- DrawGameViewport sizes the
            // target from the aspect-locked rect and RenderFrame passes those
            // through as the viewport UIPass hands us. That is the whole reason
            // the mode's Draw takes a width and a height instead of asking the
            // Application: this host has two surfaces and the fight belongs on
            // the small one. A HUD laid out against the main window's size would
            // be drawn at the wrong scale in the wrong place.
            //
            // The exit drain is NOT here. It lives in the editor's top-level UI
            // draw, which runs on frames this callback does not -- the Game panel
            // can be closed, collapsed or camera-less, and a mode must still be
            // able to leave. See the drain for the full argument.
            if (MyCoreEngine::IGameMode* mode = modes_.active()) {
                if (mode->OwnsScreen()) {
                    // THE SCENE'S UI DOCUMENTS DO NOT RUN. Not merely "are not
                    // drawn": uiWorld_.Update is what dispatches clicks and moves
                    // focus, so leaving it running would put a live menu
                    // underneath the fight, taking presses nobody can see -- and
                    // one of those presses is the button that enters this mode.
                    // The other half of the pairing is in the capture provider;
                    // both or neither.
                    //
                    // The 3D pass still ran and the mode paints over it. Same
                    // situation the Player is in and for the same reason: this
                    // callback is a pass at the END of RenderFrame, so skipping
                    // the world would mean skipping the call that gets us here.
                    //
                    // The repeat clock is reset for the same reason the else
                    // branch below resets it: nav is not polled while a mode is
                    // up, and a stick held on the way in must not deliver a
                    // burst of moves to the menu on the way out.
                    navSynth_.repeat.Reset();
                    mode->Draw(r2d, w, h, dt);
                    return;
                }
                // A mode layered OVER the running scene (OwnsScreen false) draws
                // after the documents do, at the bottom of this callback.
            }

            // Before Update, so the frame that draws the counters draws the
            // current ones. Pushed rather than polled -- see MenuUIContent.h.
            MyCoreEngine::MenuUIPublishCounters(uiWorld_, menuHooks_);
            // Nav, but ONLY while the Game surface has focus. The editor's own
            // panels are ImGui's and a pad must not drive both at once -- and
            // in edit mode the Game panel is a preview, not a game.
            //
            // ...and only while the SCENE VIEWPORT is not the thing under the
            // hand. gameSurfaceFocused_ is CLICK-LATCHED, not hover-tracked, so
            // it survives the cursor wandering off to the Scene view -- and the
            // fly camera's own gate (Application::RunLoop, !capK) opens exactly
            // there. Both would then act on one press: WASD and the left stick
            // are bound to MoveForward/MoveRight as well as to the nav actions.
            //
            // The visible symptom is worse than double movement. With nothing
            // focused in the game's UI, capK is false, so the first W flies the
            // camera AND hands focus to the HUD -- after which wantsKeyboard()
            // is true, capK is true, and the fly camera silently stops
            // responding with nothing on screen to say why.
            //
            // One condition rather than two that can both be true: whatever
            // makes the editor's camera eligible for the keys makes the game's
            // UI ineligible.
            const bool editorCameraHasTheKeys =
                viewportHovered_ || viewportFocused_ || camLooking_;
            if (playing_ && gameSurfaceFocused_ && !editorCameraHasTheKeys) {
                // WASD navigates the menu, but the SAME four keys type a
                // pilot name. The flag silences the key half of the nav
                // actions and leaves the pad half alone -- a pad types
                // nothing, so it has no reason to go quiet.
                uiWorld_.SetNav(navSynth_.Poll(input(), dt,
                                               !uiWorld_.wantsTextInput()));
            } else {
                navSynth_.repeat.Reset();
            }
            uiWorld_.Update(scene.registry, w, h, dt);
            uiWorld_.Draw(r2d);

            // A non-OwnsScreen mode: an overlay on the running scene, drawn last
            // so it sits on top of the scene's own HUD. No mode in this build
            // takes that branch, and it is written because the alternative --
            // an early return above that silently dropped such a mode's Draw --
            // is the kind of hole that is only found by the first mode to need it.
            if (MyCoreEngine::IGameMode* mode = modes_.active())
                mode->Draw(r2d, w, h, dt);
        });

        // Audio: per-frame listener/source update installed for the app's life;
        // voices are populated by audio_.Start() on Play. Boots at the saved
        // master volume so the editor and shipped player agree.
        // Listen from the GAME camera, not app.camera(). The latter is the
        // Scene-view fly camera in the editor, so distance-attenuated sources
        // were mixed for wherever the author had flown to rather than for what
        // the shipped game hears — the same scene was quiet in the Player and
        // loud in Play. gameCamera_ is a member, so the pointer stays valid.
        MyCoreEngine::InstallAudio(*this, scene, audio_, {},
                                   MyCoreEngine::AudioSettings{ masterVolume_ },
                                   &gameCamera_);
    }

    RunLoop(scene, *shader);

    // Leave whatever mode was live, HERE rather than leaving it to
    // ~GameModeRegistry. The registry is a member and would do it -- its
    // destructor calls Leave for exactly this reason -- but by then `scene`,
    // `sceneLoader_` and `shader`, all reachable from a GameModeContext, are
    // gone. A mode's Exit is allowed to touch what it was handed, and this is
    // the last moment that is true. Idempotent, so the ordinary case (Stop was
    // pressed, or a mode was never entered) costs one branch.
    modes_.Leave();
}

void EditorApplication::DrawViewport(MyCoreEngine::Scene& scene)
{
    using namespace MyCoreEngine;

    // End-of-look handling runs unconditionally (before any early-return):
    // the disabled cursor must be restored even if the viewport window
    // stops being drawn while RMB is still held.
    if (camLooking_ && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (lookWindow_) glfwSetInputMode(lookWindow_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        camLooking_ = false;
        lookWindow_ = nullptr;
    }

    ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
    // "Scene" = the editor god camera; the "Game" panel shows the primary
    // camera entity's view (renamed from "Viewport" — re-dock once)
    const bool open = ImGui::Begin("Scene", &panels_.scene,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) {
        viewportHovered_ = false;
        viewportFocused_ = false;
        ImGui::End();
        return;
    }
    viewportFocused_ = ImGui::IsWindowFocused();

    // gizmo mode toolbar
    ImGui::RadioButton("Translate", &gizmoOp_, 0); ImGui::SameLine();
    ImGui::RadioButton("Rotate", &gizmoOp_, 1); ImGui::SameLine();
    ImGui::RadioButton("Scale", &gizmoOp_, 2);

    // play-in-editor controls (P2-6)
    ImGui::SameLine(0.f, 32.f);
    if (!playing_) {
        if (ImGui::Button("Play")) startPlay_(scene);
        ImGui::SameLine();
        ImGui::TextDisabled("(Ctrl+P)");
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.20f, 0.20f, 1.f));
        if (ImGui::Button("Stop")) stopPlay_(scene);
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        // "entity changes": scene-level settings (lights, render toggles)
        // are outside the snapshot and stick — gameplay only mutates the
        // registry today
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.25f, 1.f),
                           "PLAYING%s — entity changes revert on Stop",
                           paused() ? " (paused)" : "");
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 8.f && avail.y >= 8.f) {
        sceneTarget_.Resize((int)avail.x, (int)avail.y); // takes effect next frame
    }
    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize(avail.x > 1.f ? avail.x : 1.f, avail.y > 1.f ? avail.y : 1.f);
    // Deliberately a NON-interactive item: an InvisibleButton here becomes
    // ImGui's active item on click, and ImGuizmo refuses to start a drag
    // while any item is active (gizmo hover works, dragging never engages).
    // Window-body drags are already prevented globally by
    // io.ConfigWindowsMoveFromTitleBarOnly.
    if (sceneTarget_.colorTexture()) {
        // GL textures are bottom-up: flip V
        ImGui::Image((ImTextureID)(intptr_t)sceneTarget_.colorTexture(),
            imageSize, ImVec2(0, 1), ImVec2(1, 0));
    }
    else {
        ImGui::Dummy(imageSize);
    }
    viewportHovered_ = ImGui::IsItemHovered();
    const bool viewportClicked = viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // asset drops spawn a model where the drag lands (ray resolved below,
    // once this frame's view/proj are computed)
    bool assetDropped = false;
    char droppedPath[260] = {};
    float dropU = 0.f, dropV = 0.f;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(AssetBrowserPanel::kAssetPayload)) {
            snprintf(droppedPath, sizeof(droppedPath), "%s", (const char*)pl->Data);
            const ImVec2 mouse = ImGui::GetMousePos();
            dropU = (mouse.x - imagePos.x) / imageSize.x;
            dropV = (mouse.y - imagePos.y) / imageSize.y;
            assetDropped = true;
        }
        ImGui::EndDragDropTarget();
    }

    // Camera look/zoom via ImGui input: viewport-aware, so it keeps working
    // when this panel is a detached OS window on another monitor (the
    // engine's raw main-window polling is disabled — setInternalCameraInput).
    // Note: applied after this frame's scene render, so the image reflects a
    // look/zoom one frame later; gizmo/pick math below uses the CURRENT
    // camera and stays self-consistent (interaction is RMB/LMB-exclusive).
    if (viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        camLooking_ = true;
        // disabled-cursor look, same contract as the engine's fly-cam:
        // hidden cursor + unbounded virtual deltas, so a single drag can
        // turn 360 deg instead of pinning at the desktop edge. The cursor
        // mode goes on THIS panel's platform window (may be a detached one).
        lookWindow_ = (GLFWwindow*)ImGui::GetWindowViewport()->PlatformHandle;
        if (lookWindow_) glfwSetInputMode(lookWindow_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    if (camLooking_) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        if (d.x != 0.f || d.y != 0.f) {
            camera().ProcessMouseMovement(d.x, -d.y); // yaw +x, pitch -y
        }
    }
    if (viewportHovered_) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) camera().ProcessMouseScroll(wheel);
    }

    // camera matrices matching what the renderer used for this target
    const float aspect = (sceneTarget_.height() > 0)
        ? float(sceneTarget_.width()) / float(sceneTarget_.height()) : 1.f;
    Camera& cam = camera();
    const glm::mat4 view = cam.GetViewMatrix();
    const glm::mat4 proj = glm::perspective(glm::radians(cam.Zoom), aspect,
                                            cam.NearClip, cam.FarClip);

    if (assetDropped && assets_) {
        // ray through the drop point; land on the ground plane (y=0) when
        // the ray points at it, else 10 units out
        const glm::mat4 invVP = glm::inverse(proj * view);
        glm::vec4 pn = invVP * glm::vec4(2.f * dropU - 1.f, 1.f - 2.f * dropV, -1.f, 1.f);
        glm::vec4 pf = invVP * glm::vec4(2.f * dropU - 1.f, 1.f - 2.f * dropV, 1.f, 1.f);
        pn /= pn.w;
        pf /= pf.w;
        const glm::vec3 origin(pn);
        const glm::vec3 dir = glm::normalize(glm::vec3(pf) - origin);
        glm::vec3 pos = origin + dir * 10.f;
        if (std::abs(dir.y) > 1e-4f) {
            const float t = -origin.y / dir.y;
            if (t > 0.f && t < 500.f) pos = origin + dir * t;
        }
        spawnModelEntity_(scene, droppedPath, pos);
    }

    // transform gizmo on the selected entity
    bool gizmoActive = false;
    bool gizmoDrawn = false;
    if (selected_ != entt::null && scene.registry.valid(selected_)) {
        if (auto* t = scene.registry.try_get<Transform>(selected_)) {
            gizmoDrawn = true;
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imagePos.x, imagePos.y, imageSize.x, imageSize.y);
            const ImGuizmo::OPERATION op = (gizmoOp_ == 1) ? ImGuizmo::ROTATE
                : (gizmoOp_ == 2) ? ImGuizmo::SCALE
                : ImGuizmo::TRANSLATE;

            // While idle, keep refreshing the pre-drag transform. IsUsing()
            // only flips true inside Manipulate on the frame a drag starts,
            // so this copy is guaranteed to hold the state from before the
            // drag's first applied delta — that's the undo point.
            if (!ImGuizmo::IsUsing()) gizmoBefore_ = *t;

            glm::mat4 m = t->modelMatrix;
            if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                    op, ImGuizmo::LOCAL, glm::value_ptr(m))) {
                // the gizmo edits the WORLD matrix; a parented entity stores
                // LOCAL TRS, so convert through the parent's world first
                glm::mat4 local = m;
                if (auto* par = scene.registry.try_get<Parent>(selected_);
                    par && scene.registry.valid(par->value) &&
                    scene.registry.all_of<Transform>(par->value)) {
                    local = glm::inverse(
                        scene.registry.get<Transform>(par->value).modelMatrix) * m;
                }
                // engine decompose, NOT ImGuizmo's: its euler convention
                // differs from localMatrix's Y*X*Z rebuild, which visibly
                // re-oriented compound-rotated entities on any drag
                MyCoreEngine::DecomposeTRS(local, t->position, t->rotation, t->scale);
                t->dirty = true;
            }
            gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

            // one history entry per drag, pushed on release
            const bool usingNow = ImGuizmo::IsUsing();
            if (gizmoWasUsing_ && !usingNow) {
                static const char* kGizmoLabels[3] = {
                    "Move (gizmo)", "Rotate (gizmo)", "Scale (gizmo)"
                };
                const int opIdx = (gizmoOp_ >= 0 && gizmoOp_ <= 2) ? gizmoOp_ : 0;
                undo_.recordTransformChange(scene.registry, selected_,
                                            gizmoBefore_, kGizmoLabels[opIdx]);
            }
            gizmoWasUsing_ = usingNow;
        }
    }
    if (!gizmoDrawn) gizmoWasUsing_ = false; // selection lost: no drag to close out

    // click-to-select (LMB press inside the viewport, not on the gizmo)
    if (viewportClicked && !gizmoActive) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float u = (mouse.x - imagePos.x) / imageSize.x;
        const float v = (mouse.y - imagePos.y) / imageSize.y;
        pickEntity_(scene, u, v, view, proj);
    }

    ImGui::End();
}

namespace {

// What the Game view can be locked to. `ratio` <= 0 means "whatever the panel
// happens to be", which is the honest label for it: a dockable panel's shape is
// a property of your editor layout, not of the game.
struct GameAspectOption { const char* label; float ratio; };
const GameAspectOption kGameAspects[] = {
    { "Free",  0.0f },
    { "16:9",  16.0f / 9.0f },
    { "16:10", 16.0f / 10.0f },
    { "4:3",   4.0f / 3.0f },
    { "21:9",  21.0f / 9.0f },
};

// The largest box of `ratio` that fits inside `avail`, centred, in whole
// pixels. Rounded rather than left fractional: the surface becomes a texture
// sampled 1:1 by ImGui, and a half-pixel offset makes every glyph in the HUD
// soft for no reason.
ImVec2 FitAspect(const ImVec2& avail, float ratio, ImVec2& padOut) {
    if (ratio <= 0.0f) { padOut = ImVec2(0.0f, 0.0f); return avail; }
    float w = avail.x;
    float h = w / ratio;
    if (h > avail.y) { h = avail.y; w = h * ratio; }
    w = std::floor(w);
    h = std::floor(h);
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;
    padOut = ImVec2(std::floor((avail.x - w) * 0.5f), std::floor((avail.y - h) * 0.5f));
    return ImVec2(w, h);
}

} // namespace

void EditorApplication::DrawGameViewport(MyCoreEngine::Scene& scene,
                                         MyCoreEngine::Shader& shader, float dt)
{
    using namespace MyCoreEngine;

    ImGui::SetNextWindowSize(ImVec2(640, 400), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
    // NoNavInputs while the game surface owns the keyboard.
    //
    // This panel is the one place in the editor where two different UIs share a
    // window: our own toolbar widgets above, and the GAME's UI inside the image.
    // ImGui keyboard nav is enabled globally, so with the panel focused a Tab
    // was consumed TWICE — ImGui moved its own focus to the Blend field while
    // the same keystroke also moved focus inside the game's HUD. io.NavActive
    // documents this flag as exactly the off switch: "a window is focused and it
    // doesn't use the ImGuiWindowFlags_NoNavInputs flag".
    //
    // Uses LAST frame's ownership because window flags are decided at Begin,
    // before this frame's clicks are known — the same one-frame sampling the
    // focus flag below has always used.
    const ImGuiWindowFlags gameFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        (gameSurfaceFocused_ ? ImGuiWindowFlags_NoNavInputs : 0);
    const bool open = ImGui::Begin("Game", &panels_.game, gameFlags);
    ImGui::PopStyleVar();
    if (!open) {
        // hidden/collapsed: skip the whole second scene render
        gameViewFocused_ = false;
        gameSurfaceFocused_ = false;
        setGameplayInputEnabled(false);
        ImGui::End();
        return;
    }

    // Gameplay reads input only while THIS panel is focused, matching Unity.
    // Without it a key pressed while the Scene view is focused also drove the
    // game, so there was no way to fly around a running scene -- and every
    // Space both jumped the player and did whatever the editor wanted.
    // Focus is sampled here (inside the UI pass) and applies from the next
    // frame's gameplay block; a one-frame delay is imperceptible.
    gameViewFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    // Losing the panel hands the keyboard back to the editor. Clicking WITHIN
    // the panel is resolved further down, where the image rect is known.
    if (!gameViewFocused_) gameSurfaceFocused_ = false;
    // Gameplay keys off the SURFACE, not the panel: typing 0.5 into the Blend
    // field above must not also drive the player.
    setGameplayInputEnabled(playing_ && gameSurfaceFocused_);

    // toolbar: camera override picker + blend duration. The director keys
    // switches off CameraComponent priorities on its own; the picker is a
    // manual override for previewing any camera.
    {
        auto camLabel = [&](entt::entity e) -> std::string {
            const auto* n = scene.registry.try_get<Name>(e);
            std::string s = n ? n->value : ("Entity " + std::to_string((uint32_t)e));
            if (const auto* cc = scene.registry.try_get<CameraComponent>(e); cc && !cc->enabled)
                s += " (disabled)";
            return s;
        };
        entt::entity ov = gameDirector_.overrideCamera();
        const bool ovValid = ov != entt::null && scene.registry.valid(ov) &&
                             scene.registry.all_of<CameraComponent, Transform>(ov);
        if (ov != entt::null && !ovValid) {
            // the overridden camera vanished (deleted, component removed):
            // drop the override instead of leaving it armed while the combo
            // reads "Auto" — an undo could resurrect the entity under the
            // same handle later and silently hijack the Game view
            gameDirector_.setOverride(entt::null);
            ov = entt::null;
        }
        ImGui::SetNextItemWidth(180.f);
        if (ImGui::BeginCombo("##gamecamera",
                              ovValid ? camLabel(ov).c_str() : "Auto (director)")) {
            if (ImGui::Selectable("Auto (director)", !ovValid)) {
                gameDirector_.setOverride(entt::null);
            }
            for (auto [e, cc] : scene.registry.view<CameraComponent>().each()) {
                if (!scene.registry.all_of<Transform>(e)) continue;
                ImGui::PushID((int)(uint32_t)e);
                if (ImGui::Selectable(camLabel(e).c_str(), ov == e)) {
                    gameDirector_.setOverride(e);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        float blend = gameDirector_.defaultBlendSeconds();
        ImGui::SetNextItemWidth(90.f);
        if (ImGui::DragFloat("Blend", &blend, 0.02f, 0.f, 10.f, "%.2fs",
                             ImGuiSliderFlags_AlwaysClamp)) {
            gameDirector_.setDefaultBlendSeconds(blend);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Seconds to blend when the rendered camera changes\n(0 = hard cut)");
        }

        // Aspect lock. Sits with the camera picker because it answers the same
        // question: what is this panel actually showing me?
        //
        // OUTSIDE the playing_ gate, deliberately. What shape the surface is has
        // nothing to do with play mode, and authoring a HUD against it is
        // something you do while STOPPED — which is exactly when this control
        // was invisible the first time round.
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72.f);
        if (ImGui::BeginCombo("##gameAspect", kGameAspects[gameAspect_].label)) {
            for (int i = 0; i < (int)IM_ARRAYSIZE(kGameAspects); ++i) {
                if (ImGui::Selectable(kGameAspects[i].label, i == gameAspect_)) {
                    gameAspect_ = i;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Letterboxes the game surface to a fixed aspect ratio.\n"
                "A dockable panel's shape is a property of your layout, not of the\n"
                "game - a HUD authored against this panel at 2.3:1 reads quite\n"
                "differently in a shipped 16:9 window.\n\n"
                "This constrains the PREVIEW only. The player uses its real window.");
        }
        // The surface resolution, which is the number that actually decides how
        // a HUD lays out. Last frame's, since this frame's panel size is not
        // known until after the toolbar.
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d", gameTarget_.width(), gameTarget_.height());

        // Say WHERE input is going. Silence here is what made a working jump
        // look broken: the key was fine, the panel just did not have focus.
        if (playing_) {
            ImGui::SameLine();
            // gameSurfaceFocused_, not gameViewFocused_: the NARROWER flag is
            // the one the gameplay gate, the key forwarding and the highlight
            // border all read. The wider one is set by any left click inside
            // the panel -- including on the Blend field, the camera combo or
            // the aspect lock -- so the label read "Input: game" while input
            // was in fact going nowhere near it.
            if (gameSurfaceFocused_) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.f), "| Input: game");
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f), "| Click the image to give input to the game");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Gameplay reads input only while the game SURFACE holds the\n"
                                  "keyboard -- clicking the toolbar above focuses the panel but\n"
                                  "not the game. The Scene view stays navigable while playing.");
            }

            // WHICH MODE THIS EDITOR IS IN, and the way out of it.
            //
            // The same argument as the input label beside it: silence is what
            // makes working behaviour look broken. A mode owns the screen AND the
            // keyboard, so while one is up the fly camera stands down even with
            // the cursor over the Scene view (see the capture provider) -- and
            // "the camera stopped responding" with nothing on screen to explain
            // it is a bug report this panel has already produced once.
            //
            // The button is not a duplicate of the mode's own Escape. Escape is
            // the mode's, is consumed by the mode, and only arrives while the
            // surface has the keyboard; this is the EDITOR's way out, and it
            // works when the mode's own does not -- which is the state an author
            // debugging a mode is most likely to be in.
            if (IGameMode* activeMode = modes_.active()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.95f, 1.f), "| Mode: %s",
                                   activeMode->DisplayName());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "A game mode is running in this panel. It owns the surface and\n"
                        "the keyboard for as long as it is running, so the Scene view's\n"
                        "fly camera stands down -- press Stop to take them back.\n\n"
                        "Not while the image has focus, but always: Escape and gamepad\n"
                        "BACK mean 'leave the mode' here, and if the editor could act on\n"
                        "them it would quit with your unsaved work.\n\n"
                        "The scene keeps simulating underneath (the mode paints over it),\n"
                        "and every entity change still reverts on Stop.");
                }
                ImGui::SameLine();
                // Leaves NOW, mid-toolbar, which is safely before this frame's
                // RenderFrame further down: the UI pass then takes the ordinary
                // document path rather than drawing a half-exited mode.
                if (ImGui::SmallButton("Leave Mode")) modes_.Leave();
            }
        }
    }

    // A camera-less scene is not a reason to hide a MODE. A fight is not a scene
    // -- FightSession owns a POD GameState and no entities -- so a mode draws
    // over whatever the 3D pass produced and needs nothing from the director. The
    // early return below was written when the only thing this panel could show
    // was the scene; leaving it unqualified meant a mode entered from a menu in a
    // scene whose camera was later deleted (or a title front end that never had
    // one) simply vanished, still ticking, with the panel explaining how to add a
    // Camera component to a fight.
    //
    // gameCamera_ keeps whatever the director last wrote, or its default -- it is
    // a member and is always a valid camera, and nothing on screen comes from it
    // while a mode owns the surface anyway.
    if (!gameDirector_.Update(scene.registry, dt, gameCamera_) &&
        !modes_.activeOwnsScreen()) {
        ImGui::TextDisabled("No camera in the scene.");
        ImGui::TextDisabled("Select an entity and use Inspector > Add Component > Camera.");
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // The SURFACE, not the panel. Everything downstream — the render target, the
    // pointer mapping, the focus border — is keyed to this rect, so a locked
    // aspect cannot leave the game's UI hit-testing against a box it was never
    // drawn in.
    ImVec2 pad(0.f, 0.f);
    const ImVec2 surface = FitAspect(avail, kGameAspects[gameAspect_].ratio, pad);
    // The panel's own top-left, captured ONCE. The image is drawn at
    // panelMin + pad further down, and the pointer below is mapped against the
    // same origin — reading the cursor twice would let the two disagree the
    // moment anything in between moved it.
    const ImVec2 panelMin = ImGui::GetCursorScreenPos();
    if (surface.x >= 8.f && surface.y >= 8.f) {
        gameTarget_.Resize((int)surface.x, (int)surface.y);
    }

    // Pointer for the in-game UI, in UI-LOCAL pixels.
    //
    // This mapping is the whole reason UIPointerState is host-supplied: the
    // Game view is an image inside a dockable panel that can be moved, resized
    // or dragged to another monitor, so raw window coordinates would only line
    // up by accident. Captured HERE, before RenderFrame — the cursor position
    // is where the image is about to be drawn, and the HUD is painted during
    // that render, so reading it afterwards would be a frame late.
    //
    // `inside` is gated on the panel being hovered so the UI does not react to
    // a pointer that is over some other panel entirely. ImGui::IsWindowHovered
    // is used rather than IsItemHovered because the image item does not exist
    // yet at this point in the frame.
    {
        // Offset by the letterbox pad, and bounded by the SURFACE: a click in a
        // bar is outside the game, exactly as it is outside a fullscreen game's
        // letterbox.
        const ImVec2 origin(panelMin.x + pad.x, panelMin.y + pad.y);
        const ImVec2 m = ImGui::GetMousePos();
        MyCoreEngine::ui::UIPointerState p;
        p.position = { m.x - origin.x, m.y - origin.y };
        p.inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                   p.position.x >= 0.f && p.position.y >= 0.f &&
                   p.position.x < surface.x && p.position.y < surface.y;
        // Only route clicks to the game UI when ImGui is not using the mouse
        // for its own dragging, so resizing the panel never presses a button.
        p.buttonDown = p.inside && ImGui::IsMouseDown(ImGuiMouseButton_Left);

        // Click-to-own-the-keyboard, resolved here because this is where the
        // image rect is known. Pressing the game surface hands the keyboard to
        // the game; pressing anything else in the panel — the camera picker, the
        // Blend field — hands it back to the editor. It is the same bargain as
        // clicking into a text field, and it is what makes Tab unambiguous.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            gameSurfaceFocused_ = p.inside;
        }

        // ImGui is the right source here for the same reason it is for the
        // keyboard below: the Game view is an ImGui window, so GLFW's own wheel
        // callback belongs to the Scene camera. Gated on exactly the expression
        // that computed p.inside, so hover, clicks and the wheel can never
        // disagree — and so this cannot fight the Scene view's zoom, since two
        // panels cannot both be hovered.
        //
        // io.MouseWheelH is documented ">0 scrolls Left", i.e. the CONTENT moves
        // right — already the engine's convention, so no sign flip.
        // io.MouseWheelRequestAxisSwap is deliberately ignored: GLFW performs no
        // such swap, and honouring it would make Shift+wheel behave differently
        // here and in the shipped player, which is what this panel exists to
        // rule out.
        if (p.inside) {
            const ImGuiIO& wio = ImGui::GetIO();
            p.wheel = { wio.MouseWheelH, wio.MouseWheel };
            p.shift = wio.KeyShift;
        }

        // Keyboard, but ONLY while the Game panel is focused. Otherwise typing
        // a name in the Inspector would also be typed into the game's UI, and
        // Tab would move focus in two places at once.
        //
        // ImGui is the right source here rather than GLFW: it has already
        // decoded text into codepoints (layouts, dead keys, IMEs) and it
        // resolves who owns the keyboard this frame, which is the whole
        // question. The player, which has no ImGui, reads GLFW directly.
        MyCoreEngine::ui::UIKeyboardState kb;
        // gameSurfaceFocused_, not gameViewFocused_: the panel also contains the
        // editor's own widgets, and while one of those has the keyboard the game
        // must not be reading it too. WantTextInput stays as a second guard for
        // the frame an ImGui field activates.
        if (gameSurfaceFocused_ && !ui_.WantTextInput()) {
            struct KeyMap { ImGuiKey imgui; MyCoreEngine::ui::UIKey ui; };
            static const KeyMap kKeys[] = {
                { ImGuiKey_Tab,        MyCoreEngine::ui::UIKey::Tab },
                { ImGuiKey_Enter,      MyCoreEngine::ui::UIKey::Enter },
                { ImGuiKey_KeypadEnter,MyCoreEngine::ui::UIKey::Enter },
                { ImGuiKey_Escape,     MyCoreEngine::ui::UIKey::Escape },
                { ImGuiKey_Backspace,  MyCoreEngine::ui::UIKey::Backspace },
                { ImGuiKey_Delete,     MyCoreEngine::ui::UIKey::Delete },
                { ImGuiKey_LeftArrow,  MyCoreEngine::ui::UIKey::Left },
                { ImGuiKey_RightArrow, MyCoreEngine::ui::UIKey::Right },
                { ImGuiKey_UpArrow,    MyCoreEngine::ui::UIKey::Up },
                { ImGuiKey_DownArrow,  MyCoreEngine::ui::UIKey::Down },
                { ImGuiKey_Home,       MyCoreEngine::ui::UIKey::Home },
                { ImGuiKey_End,        MyCoreEngine::ui::UIKey::End },
                { ImGuiKey_PageUp,     MyCoreEngine::ui::UIKey::PageUp },
                { ImGuiKey_PageDown,   MyCoreEngine::ui::UIKey::PageDown },
                { ImGuiKey_A,          MyCoreEngine::ui::UIKey::A },
                { ImGuiKey_C,          MyCoreEngine::ui::UIKey::C },
                { ImGuiKey_V,          MyCoreEngine::ui::UIKey::V },
                { ImGuiKey_X,          MyCoreEngine::ui::UIKey::X },
                { ImGuiKey_Z,          MyCoreEngine::ui::UIKey::Z },
                { ImGuiKey_Y,          MyCoreEngine::ui::UIKey::Y },
            };
            const ImGuiIO& io = ImGui::GetIO();
            for (const KeyMap& k : kKeys) {
                // `repeat` on, so held Backspace and arrows behave the way a
                // text field needs them to.
                if (!ImGui::IsKeyPressed(k.imgui, /*repeat=*/true)) continue;
                MyCoreEngine::ui::UIKeyEvent e;
                e.key = k.ui;
                e.shift = io.KeyShift;
                e.ctrl = io.KeyCtrl;
                e.alt = io.KeyAlt;
                kb.keys.push_back(e);
            }
            for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                MyCoreEngine::Font::AppendUTF8(kb.text,
                                               std::uint32_t(io.InputQueueCharacters[i]));
            }
        }
        uiWorld_.SetPointer(p);
        uiWorld_.SetKeyboard(kb);
    }

    // Keep the look coherent with the Scene view: scene-level state (lights,
    // materials, toggles) is shared via the Scene itself; these few live on
    // the renderer and must be mirrored. Force direct-dir mode first —
    // otherwise the game renderer's own yaw/pitch default overwrites the
    // mirrored direction and sun edits never reach the Game view.
    gameRenderer_.setUseSunYawPitch(false);
    gameRenderer_.setSunDir(renderer().sunDir());
    gameRenderer_.setExposure(renderer().exposure());
    // Every shadow/CSM setting, not just the enable flag. Only setCSMEnabled was
    // mirrored, so the Game view kept its own construction defaults (4 cascades
    // @2048) while the quality tier and every Sun & Shadows slider moved the
    // Scene view's renderer alone — the panel meant to preview the shipped build
    // showed better, longer-range shadows than the build itself.
    gameRenderer_.CopyShadowSettingsFrom(renderer());

    if (gameTarget_.fbo() && gameTarget_.width() > 0) {
        gameRenderer_.RenderFrame(scene, shader, gameCamera_,
                                  gameTarget_.width(), gameTarget_.height(), dt,
                                  gameTarget_.fbo());
        // mid-UI-frame offscreen render: restore the backbuffer binding —
        // ImGui's backend renders into whatever framebuffer is bound
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    const ImVec2 imageSize(surface.x > 1.f ? surface.x : 1.f,
                           surface.y > 1.f ? surface.y : 1.f);
    if (gameTarget_.colorTexture()) {
        // The bars are painted rather than left as panel background, so the
        // locked surface reads as a screen with a shape instead of as a game
        // that failed to fill its window.
        if (pad.x > 0.f || pad.y > 0.f) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                panelMin, ImVec2(panelMin.x + avail.x, panelMin.y + avail.y),
                IM_COL32(10, 10, 12, 255));
        }
        const ImVec2 imgMin(panelMin.x + pad.x, panelMin.y + pad.y);
        ImGui::SetCursorScreenPos(imgMin);
        // GL textures are bottom-up: flip V
        ImGui::Image((ImTextureID)(intptr_t)gameTarget_.colorTexture(),
                     imageSize, ImVec2(0, 1), ImVec2(1, 0));
        // Who owns the keyboard has to be VISIBLE, or "why does Tab not reach my
        // HUD" is unanswerable. Same idea as the caret in a focused text field:
        // the border says the game is reading your keystrokes, and clicking the
        // toolbar above takes them back.
        if (gameSurfaceFocused_) {
            ImGui::GetWindowDrawList()->AddRect(
                imgMin, ImVec2(imgMin.x + imageSize.x, imgMin.y + imageSize.y),
                ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 2.0f);
        }
    }
    ImGui::End();
}

void EditorApplication::pickEntity_(MyCoreEngine::Scene& scene, float u, float v,
                                    const glm::mat4& view, const glm::mat4& proj)
{
    // viewport uv -> NDC (screen top = +1) -> world-space ray
    const glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 pn = invVP * glm::vec4(2.f * u - 1.f, 1.f - 2.f * v, -1.f, 1.f);
    glm::vec4 pf = invVP * glm::vec4(2.f * u - 1.f, 1.f - 2.f * v, 1.f, 1.f);
    pn /= pn.w;
    pf /= pf.w;
    const glm::vec3 origin(pn);
    const glm::vec3 dir = glm::normalize(glm::vec3(pf) - origin);

    float bestT = FLT_MAX;
    entt::entity best = entt::null;
    auto entities = scene.registry.view<Transform, AABB>();
    for (auto e : entities) {
        const auto& t = entities.get<Transform>(e);
        const auto& b = entities.get<AABB>(e);

        // conservative world-space AABB of the transformed local box
        glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
        for (int c = 0; c < 8; ++c) {
            const glm::vec3 corner(
                (c & 1) ? b.max.x : b.min.x,
                (c & 2) ? b.max.y : b.min.y,
                (c & 4) ? b.max.z : b.min.z);
            const glm::vec3 w = glm::vec3(t.modelMatrix * glm::vec4(corner, 1.f));
            mn = glm::min(mn, w);
            mx = glm::max(mx, w);
        }

        // ray/AABB slab test
        float t0 = 0.f, t1 = FLT_MAX;
        bool hit = true;
        for (int a = 0; a < 3 && hit; ++a) {
            if (std::abs(dir[a]) < 1e-8f) {
                if (origin[a] < mn[a] || origin[a] > mx[a]) hit = false;
            }
            else {
                float tA = (mn[a] - origin[a]) / dir[a];
                float tB = (mx[a] - origin[a]) / dir[a];
                if (tA > tB) std::swap(tA, tB);
                t0 = std::max(t0, tA);
                t1 = std::min(t1, tB);
                if (t0 > t1) hit = false;
            }
        }
        if (hit && t0 < bestT) {
            bestT = t0;
            best = e;
        }
    }
    selected_ = best; // entt::null on miss = deselect
}

void EditorApplication::DrawLayoutControls()
{
    if (!ImGui::CollapsingHeader("Layouts", ImGuiTreeNodeFlags_None)) return;
    namespace fs = std::filesystem;

    static char layoutName[64] = "MyLayout";
    ImGui::InputText("##layoutname", layoutName, sizeof(layoutName));
    ImGui::SameLine();
    if (ImGui::Button("Save Layout")) {
        // keep the filename filesystem-safe
        std::string safe;
        for (char c : std::string(layoutName)) {
            if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ')
                safe += c;
        }
        if (!safe.empty()) {
            std::error_code ec;
            fs::create_directories("Layouts", ec);
            ImGui::SaveIniSettingsToDisk(("Layouts/" + safe + ".ini").c_str());
        }
    }
    ImGui::TextDisabled("(docking + window positions; the session layout");
    ImGui::TextDisabled(" auto-saves to imgui.ini on top of named ones)");
    ImGui::Separator();

    std::vector<fs::path> layouts;
    std::error_code ec;
    if (fs::exists("Layouts", ec)) {
        for (const auto& entry : fs::directory_iterator("Layouts", ec)) {
            if (entry.path().extension() == ".ini") layouts.push_back(entry.path());
        }
    }
    if (layouts.empty()) ImGui::TextDisabled("(no saved layouts)");
    for (const auto& p : layouts) {
        const std::string stem = p.stem().string();
        ImGui::PushID(stem.c_str());
        if (ImGui::SmallButton("Load")) {
            pendingLayoutLoad_ = p.string(); // applied before the next frame
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            fs::remove(p, ec);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(stem.c_str());
        ImGui::PopID();
    }
}

void EditorApplication::DrawInputPanel()
{
    if (!ImGui::CollapsingHeader("Input", ImGuiTreeNodeFlags_None)) return;

    auto& in = input();
    ImGui::Text("Gamepad: %s", in.gamepadConnected() ? "connected" : "not connected");
    ImGui::Text("MoveForward: %+.2f", in.axis("MoveForward"));
    ImGui::Text("MoveRight:   %+.2f", in.axis("MoveRight"));
    ImGui::Text("Look X/Y:    %+.2f / %+.2f", in.axis("LookX"), in.axis("LookY"));
    ImGui::TextDisabled("Defaults: WASD/arrows + left stick move, right stick looks,");
    ImGui::TextDisabled("ESC / Back quits. Rebind via Application::input().");
}

void EditorApplication::DrawTimeControls()
{
    if (!ImGui::CollapsingHeader("Time", ImGuiTreeNodeFlags_None)) return;

    bool isPaused = paused();
    if (ImGui::Checkbox("Paused", &isPaused)) setPaused(isPaused);

    float scale = timeScale();
    if (ImGui::SliderFloat("Time Scale", &scale, 0.f, 4.f)) setTimeScale(scale);

    float hz = fixedTimestepHz();
    if (ImGui::SliderFloat("Fixed Tick (Hz)", &hz, 15.f, 240.f, "%.0f")) setFixedTimestepHz(hz);
}

bool EditorApplication::saveScene_(MyCoreEngine::Scene& scene)
{
    // No target: an untitled scene. Ask where, rather than writing to "" (which
    // just fails) or to whatever file happened to be open before.
    if (currentScenePath_[0] == '\0') {
        pendingSaveAs_ = true;
        setSceneStatus_("This scene has no file yet - choose one");
        return false;
    }
    MyCoreEngine::SceneSerializer serializer(scene, *assets_);
    const bool ok = serializer.Save(currentScenePath_);
    setSceneStatus_(ok ? (std::string("Saved ") + currentScenePath_)
                       : "Save FAILED (see console)");
    return ok;
}

void EditorApplication::saveAll_(MyCoreEngine::Scene& scene)
{
    // "Everything currently saveable": the scene, plus the editor layout, which
    // ImGui otherwise only persists on a clean shutdown. Startup-scene is a
    // deliberate build setting, not dirty state, so it stays its own action.
    const bool sceneOk = saveScene_(scene);
    if (const char* ini = ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ini);
    setSceneStatus_(sceneOk ? "Saved all (scene + layout)"
                            : "Scene save FAILED (layout saved)");
}

void EditorApplication::newScene_(MyCoreEngine::Scene& scene)
{
    scene.ResetToDefaults();
    // Seed the same minimal content the editor boots with when no scene file
    // exists. A truly EMPTY scene has no camera, and a camera-less scene makes
    // the Game view and the shipped player fall back to a debug fly-cam — the
    // exact trap that made a built game look like it ignored the scene's camera.
    createDefaultScene_(scene);
    selected_ = entt::null;   // every entity handle is gone
    undo_.clear();            // ...including all of the history's
    gameDirector_.reset();    // ...and the Game view's camera handles
    pendingModelOps_.clear(); // in-flight ops were aimed at the old scene
    physics_.Clear();         // bodies referred to the old entities
    scripts_.Clear();         // ...and so did every script instance
    audio_.Clear();           // ...and so did any voices
    // ...and a mode was entered from a menu in the scene that just went away.
    // This function replaces the scene BY HAND rather than through SceneLoader,
    // so it does not get the observer that handles every other swap -- which is
    // why it already repeats the four lines above it. Unreachable today (New
    // Scene is disabled while playing, and a mode only exists during play) and
    // written anyway, because the list this line belongs to is right here and a
    // second path that forgot one of them is how the observer came to exist.
    modes_.Leave();
    // wholesale caster removal bypasses the departure-sphere flow: the old
    // scene's shadows would stay baked otherwise
    forceAllCSMUpdate_();
    // UNTITLED. The new scene has never been saved anywhere, so it has no save
    // target -- keeping the old one made the next Ctrl+S overwrite the file the
    // author had just closed, with no prompt and nothing in the status bar to
    // suggest it had happened. Save and Ctrl+S now route to Save As instead.
    currentScenePath_[0] = '\0';
    setSceneStatus_("New scene (unsaved - use Save Scene As)");
}

void EditorApplication::DrawMainMenuBar(MyCoreEngine::Scene& scene)
{
    // Let the status line decay back to the scene name. Ticked here, ahead of
    // the side bar rather than inside it, so a collapsed or clipped title bar
    // cannot freeze a stale message on screen. ImGui's own frame time is the
    // clock: this function runs exactly once per frame and nothing else needs
    // to know about the timeout.
    if (sceneStatusTtl_ > 0.f) {
        sceneStatusTtl_ -= ImGui::GetIO().DeltaTime;
        if (sceneStatusTtl_ <= 0.f) sceneStatus_.clear();
    }

    // Scene file ops are disabled during Play: saving would persist transient
    // play state, and loading/newing gets overwritten by Stop's restore anyway.
    const bool canEdit = !playing_;
    bool openNew = false, openOpen = false, openSaveAs = false;

    GLFWwindow* win = GetNativeWindow();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float rowH = ImGui::GetFrameHeight();

    // ======================= Row 1: custom title bar ========================
    // The window is borderless (EditorTitleBar stripped the OS caption), so we
    // draw our own top strip: the engine mark (left), the scene/document name
    // (centre, like a real title bar), and the window buttons (right). Two Up
    // side bars stack here -- this one on top, the menu row below -- and each
    // reserves work-area height so the dockspace sits under both. The empty
    // remainder is reported as the draggable caption, keeping native move /
    // Aero-snap / double-click-maximise.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::BeginViewportSideBar("##CatSplatTitleBar", vp, ImGuiDir_Up, rowH,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings)) {
        const ImVec2 barPos = ImGui::GetWindowPos();   // screen-space origin
        const float  barW   = ImGui::GetWindowWidth();
        const float  barH   = ImGui::GetWindowHeight();
        EditorTitleBar::SetBarHeight(barH);
        const float textY = (barH - ImGui::GetTextLineHeight()) * 0.5f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Paint the strip in the menu-bar tone so both rows read as one slab
        // of chrome (the side bar's own bg is the lighter window colour).
        dl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                          ImGui::GetColorU32(ImGuiCol_MenuBarBg));

        // Engine mark, far left, in the accent amber.
        ImGui::SetCursorPos(ImVec2(12.f, textY));
        ImGui::TextColored(ImVec4(0.945f, 0.631f, 0.251f, 1.f), "Cat Splat Engine");

        // Window buttons (minimise / maximise-restore / close), hard right,
        // drawn on the draw list so they don't depend on font glyphs.
        const float btnW = 46.f;
        const float btnsX = barW - btnW * 3.f;          // local X of first button
        const float btnsScreenX = barPos.x + btnsX;

        auto winBtn = [&](const char* id, int kind, bool danger, float lx) -> bool {
            ImGui::SetCursorPos(ImVec2(lx, 0.f));
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(id, ImVec2(btnW, barH));
            const bool clk = ImGui::IsItemClicked();
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 p1(p0.x + btnW, p0.y + barH);
            if (ImGui::IsItemHovered())
                d->AddRectFilled(p0, p1, danger ? IM_COL32(200, 60, 55, 255)
                                                : ImGui::GetColorU32(ImGuiCol_ButtonHovered));
            const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
            const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const float r = 5.f;
            if (kind == 0) {                    // minimise: a low bar
                d->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y + r), fg, 1.5f);
            } else if (kind == 1) {             // maximise / restore
                if (glfwGetWindowAttrib(win, GLFW_MAXIMIZED)) {
                    // two offset squares == "restore"
                    d->AddRect(ImVec2(c.x - r + 2, c.y - r), ImVec2(c.x + r, c.y + r - 2), fg, 0.f, 0, 1.4f);
                    d->AddRectFilled(ImVec2(c.x - r, c.y - r + 2), ImVec2(c.x + r - 2, c.y + r),
                                     ImGui::GetColorU32(ImGuiCol_MenuBarBg));
                    d->AddRect(ImVec2(c.x - r, c.y - r + 2), ImVec2(c.x + r - 2, c.y + r), fg, 0.f, 0, 1.4f);
                } else {
                    d->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fg, 0.f, 0, 1.4f);
                }
            } else {                            // close: an X
                d->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fg, 1.5f);
                d->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), fg, 1.5f);
            }
            return clk;
        };

        // Scene/document status, CENTRED like a real title bar: the last
        // save/load result, else the scene file name, with a play flag.
        const char* base = currentScenePath_;
        for (const char* p = currentScenePath_; *p; ++p)
            if (*p == '/' || *p == '\\') base = p + 1;
        char titleText[384];
        std::snprintf(titleText, sizeof(titleText), "%s%s",
                      playing_ ? "[PLAYING]  " : "",
                      sceneStatus_.empty() ? base : sceneStatus_.c_str());
        // Confine the title to the free span between the engine mark and the
        // window buttons, centred within it, and clip so a long scene path
        // elides at the edges instead of drawing over the mark or the buttons.
        const float titleW = ImGui::CalcTextSize(titleText).x;
        const float markW  = ImGui::CalcTextSize("Cat Splat Engine").x;
        const float freeL  = 12.f + markW + 16.f;   // just past the mark
        const float freeR  = btnsX - 16.f;          // just before the buttons
        if (freeR - freeL > 24.f) {                 // enough room to bother
            float sx = freeL + ((freeR - freeL) - titleW) * 0.5f;
            if (sx < freeL) sx = freeL;             // never start before the zone
            ImGui::PushClipRect(ImVec2(barPos.x + freeL, barPos.y),
                                ImVec2(barPos.x + freeR, barPos.y + barH), true);
            ImGui::SetCursorPos(ImVec2(sx, textY));
            ImGui::TextDisabled("%s", titleText);
            ImGui::PopClipRect();
        }

        if (winBtn("##min", 0, false, btnsX))         glfwIconifyWindow(win);
        if (winBtn("##max", 1, false, btnsX + btnW)) {
            if (glfwGetWindowAttrib(win, GLFW_MAXIMIZED)) glfwRestoreWindow(win);
            else                                          glfwMaximizeWindow(win);
        }
        if (winBtn("##close", 2, true, btnsX + btnW * 2)) glfwSetWindowShouldClose(win, 1);

        // The whole strip except the buttons is the drag caption -- but NOT
        // while a menu/modal popup is open, or clicking the strip would drag the
        // window out from under it instead of dismissing it. The buttons get
        // their own exclusion so their top/right edges click, not resize.
        const bool popupOpen = ImGui::IsPopupOpen(
            "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (popupOpen)
            EditorTitleBar::SetDragRegion(0, 0, 0, 0);
        else
            EditorTitleBar::SetDragRegion(barPos.x, barPos.y, btnsScreenX, barPos.y + barH);
        EditorTitleBar::SetButtonsRegion(btnsScreenX, barPos.y,
                                         barPos.x + barW, barPos.y + barH);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    // ========================= Row 2: menu bar ==============================
    // File / Edit / Window on their own row directly under the title bar
    // (Unity-style). A second Up side bar whose window carries a real menu bar.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::BeginViewportSideBar("##CatSplatMenuBar", vp, ImGuiDir_Up, rowH,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene", nullptr, false, canEdit))  openNew = true;
                if (ImGui::MenuItem("Open Scene...", nullptr, false, canEdit)) openOpen = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canEdit)) saveScene_(scene);
                if (ImGui::MenuItem("Save Scene As...", nullptr, false, canEdit)) openSaveAs = true;
                if (ImGui::MenuItem("Save All", "Ctrl+Shift+S", false, canEdit)) saveAll_(scene);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Save the scene AND the editor layout.");
                ImGui::Separator();
                if (ImGui::MenuItem("Set Current Scene as Player Startup", nullptr, false,
                                    canEdit && currentScenePath_[0] != '\0')) {
                    setStartupScene_(currentScenePath_);
                    setSceneStatus_(buildSettingsStatus_); // surface it in the bar
                }
                // UNDER FILE, next to the item above, because that is where a
                // Unity refugee looks and because the two are the same subject:
                // one of them sets index 0 of the list the other one edits.
                // It also has a Window checkbox, like every other panel -- the
                // bool is the same, so the two can never disagree about whether
                // the window is up. Choosing it while the window is already open
                // behind three others brings it forward rather than doing
                // nothing visible.
                if (ImGui::MenuItem("Build Settings...")) {
                    panels_.build = true;
                    buildFocus_ = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                    glfwSetWindowShouldClose(GetNativeWindow(), 1);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                // Play-gated for the same reason the Ctrl+Z handler is, and the
                // File menu above: recording is OFF during a session, so every
                // entry in the history describes the PRE-PLAY registry.
                // Rewinding one from here applied a pre-play snapshot into the
                // LIVE registry -- destroying and create(hint)-resurrecting
                // entities under handles that PhysicsWorld::entityToBody_ and
                // ScriptWorld::instances_ still map to native bodies and script
                // instances. That is precisely why stopPlay_ Clear()s both
                // before its own restore.
                const bool canU = canEdit && undo_.canUndo();
                const bool canR = canEdit && undo_.canRedo();
                const auto& entries = undo_.entries();
                const size_t cur = undo_.cursor();
                // Show WHAT would be undone/redone, like a real editor -- the
                // history deque is [0, cursor) applied, so cursor-1 is the next
                // undo and cursor the next redo.
                std::string uL = "Undo", rL = "Redo";
                if (canU && cur >= 1 && cur - 1 < entries.size()) uL += "  " + entries[cur - 1].label;
                if (canR && cur < entries.size())                 rL += "  " + entries[cur].label;
                if (ImGui::MenuItem(uL.c_str(), "Ctrl+Z", false, canU)) doUndo_(scene);
                if (ImGui::MenuItem(rL.c_str(), "Ctrl+Y", false, canR)) doRedo_(scene);
                ImGui::Separator();
                if (ImGui::MenuItem("Clear History", nullptr, false,
                                    canEdit && (undo_.canUndo() || undo_.canRedo())))
                    undo_.clear();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window")) {
                // Checkbox items bound straight to the visibility bools the UI
                // loop gates each panel on.
                ImGui::MenuItem("Scene View",  nullptr, &panels_.scene);
                ImGui::MenuItem("Game View",   nullptr, &panels_.game);
                ImGui::MenuItem("Hierarchy",   nullptr, &panels_.hierarchy);
                ImGui::MenuItem("Inspector",   nullptr, &panels_.inspector);
                ImGui::MenuItem("Assets",      nullptr, &panels_.assets);
                ImGui::MenuItem("Information", nullptr, &panels_.information);
                ImGui::MenuItem("Edit History",nullptr, &panels_.edit);
                ImGui::MenuItem("Settings",    nullptr, &panels_.settings);
                ImGui::MenuItem("Build Settings", nullptr, &panels_.build);
                // Whatever the title registered, in its own section. Draws
                // nothing at all -- not even the separator -- when no title is
                // linked, so a general editor has no empty gap suggesting
                // something failed to load.
                titlePanels_.DrawMenuItems();
                ImGui::Separator();
                // Each half restores its OWN defaults. PanelVis{} turns the
                // built-ins back on; a registered panel that asked to start
                // hidden stays hidden, which is what its VisibleByDefault
                // means and is exactly the behaviour the Combo Prover had when
                // its bool lived in PanelVis and this reset left it false.
                if (ImGui::MenuItem("Show All Panels")) {
                    panels_ = PanelVis{};
                    titlePanels_.ResetToDefaults();
                }
                ImGui::TextDisabled("Layouts: Settings > Editor tab");
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    // Keyboard shortcuts. Gated on not-typing so Ctrl+S in a text field is a
    // normal keystroke, and on canEdit for the same reason as the menu items.
    ImGuiIO& io = ImGui::GetIO();
    if (canEdit && io.KeyCtrl && !io.WantTextInput && !gameIsTyping_() &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift) saveAll_(scene); else saveScene_(scene);
    }

    // A save with no target asked for this modal (see saveScene_).
    if (pendingSaveAs_) { openSaveAs = true; pendingSaveAs_ = false; }

    if (openNew)    ImGui::OpenPopup("New Scene?");
    // Seed the scratch buffer from the current target, so both modals open
    // showing where you are rather than an empty box. An untitled scene gets a
    // suggestion instead of nothing.
    if (openOpen || openSaveAs)
        std::snprintf(scenePathEdit_, sizeof(scenePathEdit_), "%s",
                      currentScenePath_[0] ? currentScenePath_ : "Exported/scene.json");
    if (openOpen)   ImGui::OpenPopup("Open Scene");
    if (openSaveAs) ImGui::OpenPopup("Save Scene As");

    // --- New Scene confirmation ---
    if (ImGui::BeginPopupModal("New Scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Replace the current scene with a new one?");
        ImGui::TextUnformatted("You get a Main Camera and a ground plane.");
        ImGui::TextUnformatted("Unsaved changes will be lost.");
        ImGui::Separator();
        if (ImGui::Button("New Scene", ImVec2(120, 0))) {
            newScene_(scene);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Open Scene ---
    if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Scene file to open:");
        ImGui::SetNextItemWidth(360.f);
        ImGui::InputText("##openpath", scenePathEdit_, sizeof(scenePathEdit_));
        ImGui::TextDisabled("Tip: double-clicking a .json in the Asset browser also loads it.");
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            // loadSceneFromFile_ moves currentScenePath_ itself once the swap
            // completes, so the save target follows a load that WORKED.
            loadSceneFromFile_(scene, scenePathEdit_); // reports via
                                                       // SetOnSwapComplete
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Save Scene As ---
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save the scene to:");
        ImGui::SetNextItemWidth(360.f);
        ImGui::InputText("##savepath", scenePathEdit_, sizeof(scenePathEdit_));
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            // Commit HERE: Save As is exactly the gesture that retargets.
            std::snprintf(currentScenePath_, sizeof(currentScenePath_), "%s",
                          scenePathEdit_);
            saveScene_(scene);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Body only -- the caller (the Rendering tab's "Lighting" header) provides the
// section. The scene's directional light and the shadow-casting sun both live
// under Lighting now, since editing one usually means editing the other.
void EditorApplication::DrawLightControls(MyCoreEngine::Scene& scene)
{
    auto& Ld = scene.LightDir();
    auto& Lc = scene.LightColor();
    auto& Li = scene.LightIntensity();
    ImGui::DragFloat3("Dir", &Ld.x, 0.01f);
    ImGui::ColorEdit3("Color", &Lc.x);
    ImGui::SliderFloat("Intensity", &Li, 0.0f, 10.0f);
}

void EditorApplication::DrawIBLHDRControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_None)) return;

    bool ibl = scene.GetIBLEnabled();
    if (ImGui::Checkbox("Enable IBL", &ibl)) scene.SetIBLEnabled(ibl);

    float iblInt = scene.GetIBLIntensity();
    if (ImGui::SliderFloat("IBL Intensity", &iblInt, 0.0f, 4.0f)) scene.SetIBLIntensity(iblInt);

    float exposure = renderer().exposure();
    if (ImGui::SliderFloat("Exposure", &exposure, 0.2f, 5.0f)) renderer().setExposure(exposure);

    // (Anti-aliasing moved to Post & Toggles.)

    ImGui::Separator();
    ImGui::TextUnformatted("Sky / IBL source");

    // Edited in place; the renderer re-bakes when the value actually changes,
    // so dragging a colour is fine but every committed change costs a bake.
    MyCoreEngine::EnvironmentSettings& env = scene.Environment();

    const char* kSources[] = { "Procedural sky", "HDRi file" };
    int src = static_cast<int>(env.source);
    if (ImGui::Combo("Source", &src, kSources, IM_ARRAYSIZE(kSources))) {
        env.source = static_cast<MyCoreEngine::EnvironmentSettings::Source>(src);
    }

    if (env.source == MyCoreEngine::EnvironmentSettings::Source::HDRi) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", env.hdriPath.c_str());
        if (ImGui::InputText("HDRi", buf, sizeof(buf))) env.hdriPath = buf;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Equirectangular .hdr, relative to the working directory\n"
                              "e.g. Exported/Env/studio.hdr");
        }
        // A bad path falls back to the procedural sky rather than going black,
        // so say so here or the scene silently looks 'wrong but lit'.
        if (!renderer().EnvironmentError().empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.55f, 0.25f, 1.f), "%s",
                               renderer().EnvironmentError().c_str());
            ImGui::TextDisabled("Using the procedural sky instead.");
        }
    } else {
        ImGui::ColorEdit3("Zenith", &env.zenith.x);
        ImGui::ColorEdit3("Horizon", &env.horizon.x);
        ImGui::ColorEdit3("Ground", &env.ground.x);
        ImGui::SliderFloat("Sun brightness", &env.sunIntensity, 0.f, 20.f);
        ImGui::TextDisabled("Sun position follows the Lighting section.");
    }

    ImGui::Checkbox("Draw skybox", &env.drawSkybox);
    if (env.drawSkybox) {
        ImGui::SliderFloat("Sky brightness", &env.skyIntensity, 0.f, 4.f);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Dims the DRAWN sky only.\n"
                              "Use IBL Intensity to change how much it lights the scene.");
        }
    }
}


// Body only -- drawn under the Rendering tab's "Lighting" header.
void EditorApplication::DrawSunShadowControls(MyCoreEngine::Scene& scene)
{
    // --- Directional light (Unity-style) ---
    ImGui::SeparatorText("Directional Light");

    bool useYawPitch = renderer().getUseSunYawPitch();
    if (ImGui::Checkbox("Rotate Sun (Yaw/Pitch)", &useYawPitch)) renderer().setUseSunYawPitch(useYawPitch);

    if (useYawPitch) {
        float yaw, pitch; renderer().getSunYawPitchDegrees(yaw, pitch);
        if (ImGui::SliderFloat("Yaw", &yaw, -180.f, 180.f) ||
            ImGui::SliderFloat("Pitch", &pitch, -89.f, 89.f)) {
            renderer().setSunYawPitchDegrees(yaw, pitch);
        }
    }
    else {
        glm::vec3 dir = renderer().sunDir();
        if (ImGui::DragFloat3("Sun dir", &dir.x, 0.01f, -1.0f, 1.0f)) {
            if (glm::length(dir) > 1e-6f) dir = glm::normalize(dir);
            renderer().setSunDir(dir);
        }
    }

    // Link the scene's shading light to the sun. This was a FRAME-LOCAL bool
    // initialised to true, which made it permanently dead: ImGui::Checkbox
    // reports the frame the value CHANGES, and by then the local had already
    // been flipped to false, so `if (useSunForShading)` never ran. It rendered
    // as checked while doing nothing, so shadows could swing round while the
    // diffuse shading stayed put. It is now real persisted state, and it
    // defaults OFF because that is what it has actually been doing.
    if (ImGui::Checkbox("Use Sun Dir for Shading Light", &syncShadingLightToSun_)) {
        if (syncShadingLightToSun_) scene.LightDir() = renderer().sunDir();
    }
    // Keep following while enabled, so dragging the sun carries the shading
    // direction with it rather than only syncing on the click.
    if (syncShadingLightToSun_) {
        scene.LightDir() = renderer().sunDir();
        ImGui::TextDisabled("Direct Light > Dir is driven by the sun.");
    }
    // --- CSM Controls ---
    ImGui::SeparatorText("Cascaded Shadows");

    bool on = renderer().getCSMEnabled();
    if (ImGui::Checkbox("CSM Enabled", &on)) renderer().setCSMEnabled(on);

    int casc = renderer().getCSMNumCascades();
    if (ImGui::SliderInt("Cascades", &casc, 1, 4)) {
        renderer().setCSMNumCascades(casc);
        demoteQualityToCustom_(scene);
    }

    int res = renderer().getCSMBaseResolution();
    if (ImGui::SliderInt("Base Resolution", &res, 512, 4096)) {
        renderer().setCSMBaseResolution(res);
        demoteQualityToCustom_(scene);
    }

    float lambda = renderer().getCSMLambda();
    if (ImGui::SliderFloat("Split Lambda", &lambda, 0.f, 1.f)) renderer().setCSMLambda(lambda);

    float maxDist = renderer().getCSMMaxShadowDistance();
    if (ImGui::SliderFloat("Max Shadow Distance", &maxDist, 10.f, 2000.f)) renderer().setCSMMaxShadowDistance(maxDist);

    float pad = renderer().getCSMCascadePadding();
    if (ImGui::SliderFloat("Cascade Padding (m)", &pad, 0.f, 50.f)) renderer().setCSMCascadePadding(pad);

    float margin = renderer().getCSMDepthMargin();
    if (ImGui::SliderFloat("Depth Margin (m)", &margin, 0.f, 50.f)) renderer().setCSMDepthMargin(margin);

    float posEps, angEps; renderer().getCSMEpsilons(posEps, angEps);
    if (ImGui::SliderFloat("Stability Pos Epsilon (m)", &posEps, 0.f, 0.5f) ||
        ImGui::SliderFloat("Stability Ang Epsilon (deg)", &angEps, 0.f, 5.f)) {
        renderer().setCSMEpsilons(posEps, angEps);
    }

    int budget = renderer().getCSMCascadeBudget();
    if (ImGui::SliderInt("Update Budget (cascades/frame)", &budget, 0, casc)) renderer().setCSMCascadeBudget(budget);
    ImGui::SameLine(); ImGui::TextDisabled("(0 = all)");

    ImGui::SeparatorText("Dynamic Caster Cost");
    int dynCap = renderer().getCSMDynamicIntervalCap();
    if (ImGui::SliderInt("Far Re-render Interval (frames)", &dynCap, 1, 4)) renderer().setCSMDynamicIntervalCap(dynCap);
    ImGui::SameLine(); ImGui::TextDisabled("(1 = every frame)");

    // Bias / culling
    ImGui::SeparatorText("Shadow Acne Controls");
    float slope = renderer().getCSMSlopeDepthBias();
    float cbias = renderer().getCSMConstantDepthBias();
    bool cullFront = renderer().getCSMCullFrontFaces();

    if (ImGui::SliderFloat("Slope Depth Bias", &slope, 0.f, 8.f)) renderer().setCSMSlopeDepthBias(slope);
    if (ImGui::SliderFloat("Constant Depth Bias", &cbias, 0.f, 16.f)) renderer().setCSMConstantDepthBias(cbias);
    if (ImGui::Checkbox("Cull Front Faces", &cullFront)) renderer().setCSMCullFrontFaces(cullFront);

    ImGui::SeparatorText("Shadow Filtering (PCF)");
    float rbc = renderer().getShadowBiasConst();
    if (ImGui::SliderFloat("Receiver Bias Const (texels)", &rbc, 0.f, 8.f)) renderer().setShadowBiasConst(rbc);
    float rbs = renderer().getShadowBiasSlope();
    if (ImGui::SliderFloat("Receiver Bias Slope (texels)", &rbs, 0.f, 8.f)) renderer().setShadowBiasSlope(rbs);
    static const char* kKernelLabels[4] = {
        "PCF Radius (cascade 0)", "PCF Radius (cascade 1)",
        "PCF Radius (cascade 2)", "PCF Radius (cascade 3)"
    };
    for (int i = 0; i < casc && i < 4; ++i) {
        int r = renderer().getCascadeKernel(i);
        if (ImGui::SliderInt(kKernelLabels[i], &r, 0, 4)) renderer().setCascadeKernel(i, r);
    }

    if (ImGui::Button("Force Rebuild CSM")) forceAllCSMUpdate_();
    // Debug
    if (ImGui::CollapsingHeader("CSM Debug", ImGuiTreeNodeFlags_None)) {
        static const char* kModes[] = {
            "Off", "Cascade index", "Shadow factor", "Light depth", "Sampled depth", "Projected UV"
        };
        int dbg = renderer().csmDebugMode();
        if (ImGui::Combo("Mode", &dbg, kModes, IM_ARRAYSIZE(kModes))) renderer().setCSMDebugMode(dbg);

        ImGui::SameLine();
        ImGui::TextDisabled("(?)");

        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Off: normal shading\n"
            "Cascade index: color per cascade\n"
            "Shadow factor: PCF result (white=lit)\n"
            "Light depth: light-space depth 0..1\n"
            "Sampled depth / Projected UV: debug sampling");
    }
}

void EditorApplication::DrawRenderingToggles(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Post & Toggles", ImGuiTreeNodeFlags_None)) return;

    bool aa = scene.GetAAEnabled();
    if (ImGui::Checkbox("Anti-aliasing (FXAA)", &aa)) { scene.SetAAEnabled(aa); demoteQualityToCustom_(scene); }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Post-process edge antialiasing, applied after tonemapping.\n"
                          "Costs ~0.2ms and one full-resolution LDR target.\n"
                          "Smooths staircased edges; perfectly axis-aligned ones\n"
                          "have no sub-pixel coverage to recover and are left as-is.");
    }

    // Post-process stack (tonemap -> effects -> FXAA). Effects chain through a
    // ping-pong pair allocated only while at least one is enabled.
    if (ImGui::TreeNodeEx("Post-process", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& pfx = scene.PostFX();

        // Bloom (HDR glow, composited before tonemap). The signature AAA
        // effect and the one real fill cost here, so it's tier-gated.
        if (ImGui::Checkbox("Bloom", &pfx.bloom.enabled)) demoteQualityToCustom_(scene);
        if (pfx.bloom.enabled) {
            ImGui::SliderFloat("Threshold##bloom", &pfx.bloom.threshold, 0.f, 4.f);
            ImGui::SliderFloat("Intensity##bloom", &pfx.bloom.intensity, 0.f, 2.f);
        }

        // Ink outline (depth-edge) -- pairs with cel shading.
        ImGui::Checkbox("Ink outline", &pfx.outline.enabled);
        if (pfx.outline.enabled) {
            ImGui::SliderFloat("Thickness##out", &pfx.outline.thickness, 0.5f, 4.f, "%.1f px");
            ImGui::SliderFloat("Sensitivity##out", &pfx.outline.threshold, 0.02f, 0.6f);
            ImGui::SliderFloat("Strength##out", &pfx.outline.strength, 0.f, 1.f);
            ImGui::ColorEdit3("Ink colour##out", &pfx.outline.color.x);
        }

        // Procedural colour grade (LUT-style look-dev without an asset).
        ImGui::Checkbox("Colour grade", &pfx.colorGrade.enabled);
        if (pfx.colorGrade.enabled) {
            ImGui::SliderFloat("Contrast##cg",    &pfx.colorGrade.contrast,    0.5f, 2.f);
            ImGui::SliderFloat("Saturation##cg",  &pfx.colorGrade.saturation,  0.f, 2.f);
            ImGui::SliderFloat("Temperature##cg", &pfx.colorGrade.temperature, -1.f, 1.f);
            ImGui::SliderFloat("Tint##cg",        &pfx.colorGrade.tint,        -1.f, 1.f);
            ImGui::SliderFloat("Lift##cg",        &pfx.colorGrade.lift,        -0.5f, 0.5f);
            ImGui::SliderFloat("Gain##cg",        &pfx.colorGrade.gain,        0.5f, 2.f);
        }

        // Vignette (radial framing).
        auto& v = pfx.vignette;
        ImGui::Checkbox("Vignette", &v.enabled);
        if (v.enabled) {
            ImGui::SliderFloat("Intensity##vig",  &v.intensity,  0.f, 1.f);
            ImGui::SliderFloat("Roundness##vig",  &v.roundness,  0.f, 1.f);
            ImGui::SliderFloat("Smoothness##vig", &v.smoothness, 0.f, 1.f);
        }
        ImGui::TreePop();
    }

    bool vsync = vsyncEnabled();
    if (ImGui::Checkbox("VSync", &vsync)) setVSync(vsync);
    ImGui::SameLine(); ImGui::TextDisabled("(off = uncapped, for benchmarking)");

    bool inst = scene.GetInstancingEnabled();
    if (ImGui::Checkbox("Enable instancing", &inst)) scene.SetInstancingEnabled(inst);

    bool prepass = scene.GetDepthPrepassEnabled();
    if (ImGui::Checkbox("Depth prepass", &prepass)) { scene.SetDepthPrepassEnabled(prepass); demoteQualityToCustom_(scene); }
    ImGui::SameLine(); ImGui::TextDisabled("(shade each pixel once)");

    bool lod = scene.GetLODEnabled();
    if (ImGui::Checkbox("Enable mesh LOD", &lod)) { scene.SetLODEnabled(lod); demoteQualityToCustom_(scene); }
    float lodScale = scene.GetLODDistanceScale();
    if (ImGui::SliderFloat("LOD distance scale", &lodScale, 0.25f, 4.f)) { scene.SetLODDistanceScale(lodScale); demoteQualityToCustom_(scene); }
    ImGui::SameLine(); ImGui::TextDisabled("(higher = detail farther)");

    // Projected-size cull: the lever that actually speeds up wide/bird's-eye
    // views (they're vertex/instance-bound; shadows/fill are effectively free).
    // Higher pixel floor = more culled + more distant popping.
    bool smallCull = scene.GetSmallCullEnabled();
    if (ImGui::Checkbox("Cull tiny objects", &smallCull)) { scene.SetSmallCullEnabled(smallCull); demoteQualityToCustom_(scene); }
    float smallPx = scene.GetSmallCullPixels();
    if (ImGui::SliderFloat("Min on-screen px", &smallPx, 0.f, 48.f, "%.1f px")) { scene.SetSmallCullPixels(smallPx); demoteQualityToCustom_(scene); }
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Drops objects whose bounding sphere projects smaller than N pixels\n"
            "tall. Speeds up vertex/instance-bound wide & bird's-eye views.\n"
            "Low values (2-4px) are sub-visible. Higher values cull more but can\n"
            "pop distant objects and, with a low sun, leave their (still-cast)\n"
            "shadows briefly visible.");
    }

    bool nm = scene.GetNormalMapEnabled();
    if (ImGui::Checkbox("Enable normal mapping", &nm)) scene.SetNormalMapEnabled(nm);

    bool pbr = scene.GetPBREnabled();
    if (ImGui::Checkbox("Enable PBR (Cook-Torrance)", &pbr)) scene.SetPBREnabled(pbr);

    // ---- physics ----
    ImGui::SeparatorText("Physics");
    {
        // Backend list is whatever this build registered, so the picker is
        // honest: a build without an SDK simply doesn't offer it.
        const auto backends = MyCoreEngine::PhysicsBackendRegistry::Available();
        const std::string current = physics_.BackendName();
        if (ImGui::BeginCombo("Backend", current.empty() ? "(none)" : current.c_str())) {
            for (const auto& n : backends) {
                if (ImGui::Selectable(n.c_str(), n == current) && n != current) {
                    // Switching rebuilds the world under the new engine.
                    // Refused mid-play: bodies would vanish and every
                    // simulated pose would snap back.
                    if (playing_) {
                        physicsStatus_ = "Stop play before switching backend.";
                    }
                    else if (physics_.SetBackend(n)) {
                        physicsStatus_ = "Backend: " + n;
                    }
                    else {
                        physicsStatus_ = "FAILED to initialize " + n;
                    }
                }
            }
            ImGui::EndCombo();
        }
        glm::vec3 g = physics_.Gravity();
        if (ImGui::DragFloat3("Gravity", &g.x, 0.05f)) physics_.SetGravity(g);
        ImGui::Text("Bodies: %zu", physics_.BodyCount());
        if (!physics_.SkippedEntities().empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
                               "%zu body(s) skipped - no collider",
                               physics_.SkippedEntities().size());
        }
        if (!physicsStatus_.empty()) ImGui::TextDisabled("%s", physicsStatus_.c_str());
        ImGui::TextDisabled(playing_ ? "Simulating." : "Bodies build on Play.");
    }
}

void EditorApplication::DrawInformationPanel(const MyCoreEngine::Scene& scene, float dt)
{
    const auto& rs = scene.GetRenderStats();
    ImGui::Begin("Information", &panels_.information, ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::CollapsingHeader("Rendering Stats", ImGuiTreeNodeFlags_None)) {
        ImGui::Text("dt: %.3f ms (%.1f FPS)", dt * 1000.f, dt > 0.f ? 1.f / dt : 0.f);
        // GPU string: a hybrid laptop silently on the Intel iGPU is ~4-5x
        // slower than the dGPU — the fastest way to spot that here. Queried
        // once (the string is static for the context's lifetime).
        static const char* sGpu = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        ImGui::TextDisabled("GPU: %s", sGpu ? sGpu : "(unknown)");
        // Frame breakdown: which THIRD of the frame is slow. GL is async, so
        // 3D submission is usually small and the GPU wait (plus any vsync
        // block) lands in swap. A big "ui" means the editor panels, not the
        // renderer, own the frame.
        ImGui::Text("  3D submit: %6.2f ms", frameSceneRenderMs());
        ImGui::Text("  editor UI: %6.2f ms", frameUiMs());
        ImGui::Text("  swap/wait: %6.2f ms  (vsync %s)",
                    frameSwapMs(), vsyncEnabled() ? "ON" : "off");
        ImGui::Text("Cascades: %d, res: %d", renderer().getCSMNumCascades(), renderer().getCSMBaseResolution());
        ImGui::Text("Draws:            %u", rs.draws);
        ImGui::Text("Instanced draws:  %u", rs.instancedDraws);
        ImGui::Text("Instances:        %u", rs.instances);
        ImGui::Separator();
        ImGui::Text("Texture binds:    %u", rs.textureBinds);
        ImGui::Text("VAO binds:        %u", rs.vaoBinds);
        ImGui::Separator();
        ImGui::Text("Built items:      %u", rs.itemsBuilt);
        ImGui::Text("Culled (frustum): %u", rs.culled);
        ImGui::Text("Culled (size):    %u", rs.culledSmall);
        ImGui::Text("Submitted:        %u", rs.submitted);
        ImGui::Text("Lights (act/cull):%u / %u", rs.lightsActive, rs.lightsCulled);
        ImGui::Text("LOD 0/1/2:        %u / %u / %u",
            rs.lodInstances[0], rs.lodInstances[1], rs.lodInstances[2]);
        unsigned totalCalls = rs.draws + rs.instancedDraws;
        ImGui::Text("GPU draw calls:   %u", totalCalls);
    }
    ImGui::End();
}

void EditorApplication::createDefaultScene_(MyCoreEngine::Scene& scene)
{
    using namespace MyCoreEngine;

    // Minimal, but deliberately NEVER camera-less: the Game view and the
    // shipped player both render through a CameraComponent, and a scene
    // without one silently falls back to a debug fly-cam — which reads as
    // "the build ignored my camera".
    {
        Entity cam = scene.createEntity();
        cam.addComponent<Name>(Name{ "Main Camera" });
        Transform t{};
        t.position = glm::vec3(0.f, 6.f, 30.f);
        t.rotation = glm::vec3(-11.f, 0.f, 0.f); // pitch down toward the origin
        cam.addComponent<Transform>(t);
        scene.registry.emplace<CameraComponent>(cam, CameraComponent{});
    }

    // A ground you can actually land on: visual plane + a static physics
    // plane, so dropping a RigidBody into a fresh scene just works.
    if (auto groundHandle = assets_->GetModel("Exported/Model/plane.obj")) {
        Entity ground = scene.createEntity();
        ground.addComponent<Name>(Name{ "Ground" });
        Transform t{};
        t.position = glm::vec3(0.f, -3.f, 0.f);
        t.scale = glm::vec3(300.f, 1.f, 300.f);
        ground.addComponent<Transform>(t);
        ground.addComponent<ModelComponent>(ModelComponent{ groundHandle });
        ground.addComponent<AABB>(generateAABB(*groundHandle));
        // registry directly: Entity::addComponent can't return a reference for
        // empty flag components (EnTT emplace returns void for them)
        scene.registry.emplace<NoShadow>(ground);
        scene.registry.emplace<RigidBody>(ground, RigidBody{ BodyType::Static });
        scene.registry.emplace<PlaneCollider>(ground, PlaneCollider{});
    }
}

bool EditorApplication::loadSceneFromFile_(MyCoreEngine::Scene& /*scene*/,
                                          const std::string& path)
{
    // Everything this method used to do by hand — the selection/undo/director
    // resets, the CSM rebuild, the physics/script/audio clears — is now an
    // observer on the loader, subscribed where each of those things is created.
    // What is left is the one decision only the host makes: WHEN.
    //
    // Editor loads are drained IMMEDIATELY rather than at the frame boundary.
    // They come from a menu handler, not from game code holding a view into the
    // registry, and the caller wants the yes/no now for the status line. The
    // deferred path is for GAME-originated swaps, which the editor drains in
    // Application::RunLoop like any other host.
    if (!sceneLoader_) return false;
    if (!sceneLoader_->RequestSwap(path, MyCoreEngine::SceneSwapOrigin::Host))
        return false; // the file did not validate; nothing was touched
    sceneLoader_->DrainPendingSwap();
    return sceneLoader_->lastResult().status == MyCoreEngine::SceneSwapStatus::Ok;
}

bool EditorApplication::setStartupScene_(const std::string& path)
{
    // TWO FILES MOVE, AND BOTH HAVE TO, or the editor's two ways of saying "this
    // is where the game starts" disagree with each other.
    //
    // build.json is the one that decides. BuildSettings.h names this menu item
    // as the verb `SetStartupScene` becomes -- move the scene to index 0, adding
    // it first if it is not in the list -- and a Build writes `scenes[0]` over
    // project.json's startupScene on its way into the bundle. So a version of
    // this that wrote only project.json would let somebody press this item, see
    // it confirmed, and get a bundle that boots something else, because the
    // build never read the field they set.
    //
    // project.json is written TOO, and not just for symmetry: it is the file
    // that already exists in every project, the one a player run out of the
    // build tree reads (PlayerMain resolves it after a linked title's front
    // end), and the one whose masterVolume the load-modify-save preserves.
    // Nothing here sets `startupSceneFromBuild` -- that flag means "a build
    // wrote this", and this is a preference.
    //
    // NOTE THE VISIBLE SIDE EFFECT: choosing this for a scene already in the
    // build REORDERS the list, because index 0 is what boots. That is what the
    // item means now, and the status line below says how long the list is so it
    // is not silent.
    std::string listNote;
    if (buildLoad_.safeToSave()) {
        std::string why;
        if (buildSettings_.SetStartupScene(path, &why)) {
            buildPreflightStale_ = true;
            if (saveBuildSettings_()) {
                listNote = "; first of " +
                           std::to_string(buildSettings_.scenes.size()) +
                           " scene(s) in the build";
            }
            else {
                listNote = "; build.json write FAILED (see console)";
            }
        }
        else {
            // AddScene's own sentence, which names the reason (over the scene
            // limit, or a path the sandbox refuses). Surfaced rather than
            // swallowed: the build list is what actually decides.
            listNote = "; NOT added to the build (" + why + ")";
        }
    }
    else {
        listNote = "; build.json is unreadable, so the build list was left alone";
    }

    MyCoreEngine::ProjectSettings s;
    s.Load(); // preserves the fields the struct knows; Save rewrites the file
    s.startupScene = path;
    const bool ok = s.Save();
    if (ok) {
        startupSceneDisplay_ = path;
        startupSceneLoaded_ = true;
        buildSettingsStatus_ = "Saved to Exported/project.json" + listNote;
    }
    else {
        buildSettingsStatus_ = "Save FAILED (see console)" + listNote;
    }
    return ok;
}

void EditorApplication::demoteQualityToCustom_(MyCoreEngine::Scene& scene)
{
    // Only the label changes -- every individual setting keeps the value the
    // author just chose. Custom is precisely "don't fan a preset over these".
    scene.SetQualityLevel(MyCoreEngine::Scene::QualityLevel::Custom);
}

void EditorApplication::saveMasterVolume_()
{
    MyCoreEngine::ProjectSettings s;
    s.Load(); // preserve startupScene (and any future fields) before rewriting
    s.masterVolume = masterVolume_;
    s.Save();  // best-effort; a failed write just means it won't persist
}

void EditorApplication::spawnModelEntity_(MyCoreEngine::Scene& scene,
                                          const std::string& path,
                                          const glm::vec3& pos)
{
    if (!assets_) return;
    auto req = assets_->RequestModel(jobs(), path);
    if (req->state == MyCoreEngine::AssetManager::LoadState::Live) {
        finishSpawn_(scene, req->model, pos); // cache hit: same frame as before
        return;
    }
    if (req->state == MyCoreEngine::AssetManager::LoadState::Failed) return;
    PendingModelOp op;
    op.req = std::move(req);
    op.spawnPos = pos;
    op.requestedDuringPlay = playing_;
    pendingModelOps_.push_back(std::move(op));
}

void EditorApplication::assignModelToEntity_(MyCoreEngine::Scene& scene,
                                             const std::string& path,
                                             entt::entity target)
{
    if (!assets_ || !scene.registry.valid(target)) return;
    auto req = assets_->RequestModel(jobs(), path);
    if (req->state == MyCoreEngine::AssetManager::LoadState::Live) {
        finishAssign_(scene, req->model, target);
        return;
    }
    if (req->state == MyCoreEngine::AssetManager::LoadState::Failed) return;
    PendingModelOp op;
    op.req = std::move(req);
    op.assignTo = target;
    op.requestedDuringPlay = playing_;
    pendingModelOps_.push_back(std::move(op));
}

void EditorApplication::pollPendingModelOps_(MyCoreEngine::Scene& scene)
{
    using LoadState = MyCoreEngine::AssetManager::LoadState;
    for (size_t i = 0; i < pendingModelOps_.size(); ) {
        PendingModelOp& op = pendingModelOps_[i];
        if (op.req->state == LoadState::Queued || op.req->state == LoadState::Decoding) {
            ++i;
            continue;
        }
        // terminal: run it (or drop it) and remove from the list
        PendingModelOp done = std::move(op);
        pendingModelOps_.erase(pendingModelOps_.begin() + i);
        if (done.req->state == LoadState::Failed) {
            fprintf(stderr, "[Editor] model load failed: %s\n", done.req->path.c_str());
            continue;
        }
        if (done.assignTo == entt::null) {
            finishSpawn_(scene, done.req->model, done.spawnPos);
        }
        else if (scene.registry.valid(done.assignTo)) {
            // target may have died while decoding: only assign to the living
            finishAssign_(scene, done.req->model, done.assignTo);
        }
    }
}

void EditorApplication::finishSpawn_(MyCoreEngine::Scene& scene,
                                     const std::shared_ptr<MyCoreEngine::Model>& model,
                                     const glm::vec3& pos)
{
    if (!model || model->Meshes().empty()) return;
    MyCoreEngine::Entity e = scene.createEntity();
    const std::string stem = std::filesystem::path(model->SourcePath()).stem().string();
    e.addComponent<Name>(Name{ stem.empty() ? std::string("Entity") : stem });
    Transform t{};
    t.position = pos;
    e.addComponent<Transform>(t);
    e.addComponent<ModelComponent>(ModelComponent{ model });
    e.addComponent<AABB>(generateAABB(*model));
    undo_.recordCreate(scene.registry, e, "Spawn '" + stem + "'");
    selected_ = e;
    // no CSM force needed: the fresh Transform is dirty, so the normal
    // dirty-caster arrival flow picks the new caster up next frame
}

void EditorApplication::finishAssign_(MyCoreEngine::Scene& scene,
                                      const std::shared_ptr<MyCoreEngine::Model>& model,
                                      entt::entity target)
{
    if (!model || model->Meshes().empty()) return;
    auto& reg = scene.registry;
    undo_.record(reg, target, "Assign model", [&] {
        reg.emplace_or_replace<ModelComponent>(target, ModelComponent{ model });
        reg.emplace_or_replace<AABB>(target, generateAABB(*model));
        if (!reg.any_of<Transform>(target)) reg.emplace<Transform>(target);
    });
    // swapped caster without a transform dirty: rebuild both renderers' CSM
    forceAllCSMUpdate_();
}

void EditorApplication::startValidate_()
{
    if (validateRun_) return; // one run at a time (button is disabled anyway)
    validateRunning_ = true;
    validateOpen_ = true;
    validateReport_.clear();

    // Spawn the cooker as a child (Subprocess seam): we keep its handle/pid so
    // shutdown can KILL a hung cooker — crash isolation AND hang isolation.
    // Only stdout is captured (the WARN/ERR/DONE protocol); stderr (engine
    // [Model] logs) stays on the editor console.
    editor::Subprocess sub = editor::Subprocess::Spawn(
        { "AssetCooker", "validate", "Exported" });
    if (!sub.ok()) {
        validateReport_ = "failed to launch AssetCooker (" + sub.error() + ")";
        validateRunning_ = false;
        return;
    }

    auto run = std::make_unique<ValidateRun>();
    run->proc = std::move(sub);
    ValidateRun* raw = run.get();
    // The reader ONLY drains stdout. Reaping (proc.wait) is left to the main
    // thread after it joins, so proc.kill() (cancel, main thread) and the reap
    // never run concurrently -- otherwise a kill could target a pid the reader
    // had already reaped, and the kernel could have recycled it.
    run->reader = std::thread([raw] {
        while (raw->proc.readChunk(raw->output)) { /* drain until EOF */ }
        raw->done = true;
    });
    validateRun_ = std::move(run);
}

void EditorApplication::cancelValidate_()
{
    if (!validateRun_) return;
    // a hung child would block the reader forever: kill it, which EOFs the
    // pipe and lets the reader thread finish
    if (!validateRun_->done) {
        validateRun_->proc.kill();
    }
    if (validateRun_->reader.joinable()) validateRun_->reader.join();
    validateRun_->proc.wait(); // reap on the main thread (no zombie left behind)
    validateRun_.reset();      // Subprocess dtor closes the child handles
    validateRunning_ = false;
}

// ===========================================================================
// Build Settings: the editor's half of the build pipeline
// ===========================================================================
namespace {

// The process seam BuildPipeline.h declares, over the Subprocess this editor
// already owns. A FORWARDING SHIM AND NOTHING MORE, which is the property the
// interface was shaped for: the four operations are deliberately the four
// editor::Subprocess exposes.
//
// IT LIVES HERE RATHER THAN IN THE ENGINE, and BuildPipeline.h gives the reason
// that matters: Engine.dll is linked by the PLAYER too, so process spawning
// inside the engine would put "launch an arbitrary program" inside every shipped
// game. Behind a seam the host fills, the shipped player passes nothing and the
// capability is simply absent.
//
// THE CALLING DISCIPLINE IS THE PIPELINE'S, not this class's, and it is stated
// in IBuildProcess: ReadChunk and Wait only on the job thread, Wait only after
// ReadChunk returned false, Kill from any thread while a ReadChunk is blocked.
// Subprocess already satisfies all three -- its own comments record the
// use-after-reap this repository has already met once.
class SubprocessBuildProcess final : public MyCoreEngine::IBuildProcess {
public:
    explicit SubprocessBuildProcess(editor::Subprocess p) : proc_(std::move(p)) {}
    bool ReadChunk(std::string& out) override { return proc_.readChunk(out); }
    int  Wait() override { return proc_.wait(); }
    void Kill() override { proc_.kill(); }
private:
    editor::Subprocess proc_;
};

} // namespace

void EditorApplication::loadBuildSettings_()
{
    buildSettings_ = MyCoreEngine::BuildSettings{};
    buildLoad_ = buildSettings_.Load();
    buildDirty_ = false;
    buildPreflightStale_ = true;

    // MISSING IS THE MIGRATION CASE, NOT AN ERROR. Every project that predates
    // the build list has a startupScene in project.json and no build.json, and
    // BuildSettings::SeedFromProjectSettings exists to turn that one field into
    // a one-element list. It only ever ADDS and only to an empty list, so it
    // cannot reorder an authored list if this is ever called twice.
    //
    // NOT SAVED HERE. Seeding writes nothing: an editor that created a file on
    // every launch would put a build.json into every checkout that ever opened
    // the editor, including ones where nobody is shipping anything. The panel
    // shows "No Exported/build.json yet" and the first Save is the author's.
    if (buildLoad_.status == MyCoreEngine::BuildSettingsStatus::Missing) {
        MyCoreEngine::ProjectSettings legacy;
        legacy.Load();
        buildSettings_.SeedFromProjectSettings(legacy);
        buildDirty_ = !buildSettings_.scenes.empty();
    }
}

bool EditorApplication::saveBuildSettings_()
{
    // THE REFUSAL THAT PROTECTS A TWELVE-SCENE LIST FROM A MISPLACED COMMA. A
    // Malformed load left buildSettings_ holding this session's DEFAULTS, not
    // the file's contents, so writing now replaces somebody's list with two
    // lines. BuildSettings.h states the rule; this is the one place in the
    // editor that could break it.
    if (!buildLoad_.safeToSave()) {
        buildSettingsStatus_ = "build.json is unreadable; refusing to overwrite it";
        return false;
    }
    if (!buildSettings_.Save()) {
        buildSettingsStatus_ = "Saving Exported/build.json FAILED (see console)";
        return false;
    }
    buildDirty_ = false;
    // A successful write makes the file readable again by definition, so a
    // Discard that just overwrote a malformed file leaves a truthful standing
    // rather than one that keeps the page read-only until the next launch.
    buildLoad_ = MyCoreEngine::BuildSettingsLoadResult{};
    buildLoad_.status = MyCoreEngine::BuildSettingsStatus::Ok;
    return true;
}

std::string EditorApplication::buildEnvironmentProblem_() const
{
    // Reads the macros directly rather than building a BuildEnvironment and
    // inspecting it: this is asked once per frame while the panel is open, and
    // constructing the environment allocates a std::function for the launcher
    // that is then thrown away.
    static const char* const kMissing =
        "This editor was compiled without its build-tree locations, so it cannot "
        "invoke a build. Re-run CMake configure and rebuild the editor.";
#if defined(CSE_BUILD_SOURCE_DIR) && defined(CSE_BUILD_BINARY_DIR) && \
    defined(CSE_BUILD_EXE_DIR)
    if (CSE_BUILD_SOURCE_DIR[0] == '\0' || CSE_BUILD_BINARY_DIR[0] == '\0' ||
        CSE_BUILD_EXE_DIR[0] == '\0') {
        return kMissing;
    }
    return std::string();
#else
    return kMissing;
#endif
}

MyCoreEngine::BuildEnvironment EditorApplication::buildEnvironment_() const
{
    MyCoreEngine::BuildEnvironment env;

    // WHERE THESE COME FROM: compile definitions set on the Editor target
    // (Editor/CMakeLists.txt). BuildPipeline.h proposes a configure-time JSON
    // instead, and its stated reason is that "baking absolute build-machine
    // paths into Engine.dll puts them in every shipped game" -- which is an
    // argument about Engine.dll, and this is Editor.exe. Nothing installs the
    // editor (the install rules are Engine and, per configuration, exactly one
    // player -- PlayerShipping in Release, PlayerDebug in Debug and
    // RelWithDebInfo),
    // so nothing here can leak into a bundle. A file would additionally need a
    // JSON parser in a target that does not link one, and could not carry
    // $<CONFIG>; definitions can, which is what makes exeDir correct on a
    // multi-config generator without a second mechanism.
    //
    // A tree configured before these existed leaves them undefined, the strings
    // stay empty, and buildEnvironmentProblem_ says so on the panel rather than
    // the pipeline failing somewhere further in.
#ifdef CSE_BUILD_SOURCE_DIR
    env.sourceDir = CSE_BUILD_SOURCE_DIR;
#endif
#ifdef CSE_BUILD_BINARY_DIR
    env.binaryDir = CSE_BUILD_BINARY_DIR;
#endif
#ifdef CSE_BUILD_EXE_DIR
    env.exeDir = CSE_BUILD_EXE_DIR;
#endif
#ifdef CSE_BUILD_CMAKE_COMMAND
    env.cmakeCommand = CSE_BUILD_CMAKE_COMMAND;
#endif
#ifdef CSE_BUILD_CONFIG_TYPE
    env.configuredBuildType = CSE_BUILD_CONFIG_TYPE;
#endif
#ifdef CSE_BUILD_MULTI_CONFIG
    env.multiConfigGenerator = (CSE_BUILD_MULTI_CONFIG != 0);
#endif

    // Empty means sourceDir, which is what BuildSettings::outputDirectory being
    // "Builds" is written against. Left empty rather than restated, so there is
    // one definition of the default.
    env.outputRoot.clear();

    // LEFT EMPTY, AND THE EDITOR CANNOT DO OTHERWISE. Telling the pipeline which
    // scene a linked title boots would need TitleFrontEndScene(), and this
    // executable is not linked against the title's front end -- deliberately, by
    // the rule the root CMakeLists' boundary assertion enforces. The consequence
    // (a build whose index 0 is not the title's front end can be overruled in
    // the bundle) is stated by the panel as help text instead.
    env.titleFrontEndScene.clear();

    // RESOLVING argv[0] IS THE ADAPTER'S JOB, and BuildPipeline.h says so: it
    // passes "a program, not a target name", which in practice is TWO KINDS OF
    // THING and neither of the editor's existing rules covers both.
    //
    //   `cmake`             on PATH, or an absolute path from configure time.
    //   `AssetCooker`       a SIBLING of the editor, bare-named by the validate
    //                       phase. It is not on PATH and never will be.
    //
    // Subprocess::Spawn resolves everything as a sibling, which turns cmake into
    // ".\cmake.exe" and fails with "is cmake.exe built?". SpawnProgram resolves
    // everything the way the OS does, which finds a sibling on WINDOWS (its
    // search starts in the calling process's directory) and NOT ON LINUX, where
    // posix_spawnp searches PATH only and the current directory is not on it.
    // Left alone, that is a Linux editor whose builds silently skip the asset
    // check with a warning, forever, while Windows passes.
    //
    // So: a bare name that names a file NEXT TO US becomes that file's path, and
    // everything else goes through untouched. One rule, both platforms.
    env.launchProcess = [](const std::vector<std::string>& argv,
                           std::string& error)
        -> std::unique_ptr<MyCoreEngine::IBuildProcess> {
        namespace fs = std::filesystem;
        std::vector<std::string> resolved = argv;
        if (!resolved.empty()) {
            const bool bare = resolved[0].find('/')  == std::string::npos &&
                              resolved[0].find('\\') == std::string::npos;
            if (bare) {
                // "." is the working directory, which is the editor's own
                // directory -- the same assumption Spawn makes and the same one
                // that lets ProjectSettings::DefaultPath() find Exported/.
                fs::path sibling = fs::path(".") / resolved[0];
#if defined(_WIN32)
                sibling += ".exe";
#endif
                std::error_code ec;
                if (fs::is_regular_file(sibling, ec)) {
                    resolved[0] = sibling.make_preferred().string();
                }
            }
            else {
                // Native separators. Forward slashes are what CMake hands out
                // and what CreateProcess is least sure about when it is parsing
                // an application path out of a command line.
                resolved[0] = fs::path(resolved[0]).make_preferred().string();
            }
        }
        editor::Subprocess p = editor::Subprocess::SpawnProgram(resolved);
        if (!p.ok()) {
            error = p.error();
            return nullptr;
        }
        return std::unique_ptr<MyCoreEngine::IBuildProcess>(
            new SubprocessBuildProcess(std::move(p)));
    };
    return env;
}

void EditorApplication::runBuildPreflight_()
{
    buildPreflight_ = MyCoreEngine::PreflightBuild(buildSettings_, buildEnvironment_());
    buildPreflightValid_ = true;
    buildPreflightStale_ = false;
}

void EditorApplication::startBuild_()
{
    if (buildJob_) return; // one at a time; the button is disabled anyway

    // The previous attempt's output goes NOW, before anything can fail: a
    // "nothing was built" report sitting above the last successful compile's log
    // reads as if that log belonged to it.
    buildLog_.clear();
    buildHaveReport_ = false;
    buildProgress_ = MyCoreEngine::BuildProgress{};

    // SAVE FIRST, so the bundle and build.json agree about what shipped. The job
    // builds from the IN-MEMORY settings (BuildJob::Start takes them by value,
    // precisely so a build cannot change under its own feet), and a bundle whose
    // scene list was never written to disk is one nobody can reproduce.
    // A refused save (Malformed) also blocks the build -- the panel greys the
    // button for the same reason, so this is the second of two agreeing gates
    // rather than a surprise.
    //
    // AND IT REPORTS THROUGH THE SAME CHANNEL AS EVERY OTHER FAILURE. Returning
    // quietly here would be the one way to press Build and get nothing at all,
    // with the explanation sitting in a status string the panel does not read.
    if (!saveBuildSettings_()) {
        buildReport_ = MyCoreEngine::BuildReport{};
        buildReport_.result = MyCoreEngine::BuildResult::FailedPreflight;
        buildReport_.message = "Nothing was built: the build settings could not "
                               "be written, so the bundle and build.json would "
                               "have disagreed about what shipped.";
        buildReport_.errors.push_back(buildSettingsStatus_);
        buildHaveReport_ = true;
        return;
    }

    // Open the log as the build starts, not at the end: a compile that fails in
    // the first ten seconds should not need a second click to explain itself.
    panels_.build = true;

    buildJob_ = MyCoreEngine::BuildJob::Start(buildSettings_, buildEnvironment_());
    // Start ALWAYS returns a job -- a preflight failure comes back as an
    // already-finished one carrying the reasons -- so there is one code path for
    // "how did the build go" and pollBuild_ below is it. A null here would mean
    // the allocation failed, which is not something to paper over.
    if (!buildJob_) {
        buildReport_ = MyCoreEngine::BuildReport{};
        buildReport_.result = MyCoreEngine::BuildResult::FailedPreflight;
        buildReport_.message = "could not start the build job";
        buildHaveReport_ = true;
    }
}

void EditorApplication::pollBuild_()
{
    if (!buildJob_) return;

    // APPENDS, so the editor owns the accumulated buffer and Poll never hands
    // back a growing string -- the same shape as Subprocess::readChunk, and for
    // the same reason: the log of a long compile is large and must be moved
    // once, not copied per frame.
    buildProgress_ = buildJob_->Poll(&buildLog_);
    if (buildLog_.size() > kMaxBuildLogBytes) {
        // Drop the head at a LINE boundary, so the first visible line is a whole
        // one rather than the tail of a compiler diagnostic. The panel renders a
        // smaller tail still; this bound is about the buffer, which would
        // otherwise grow for the life of the editor session.
        const std::size_t cut = buildLog_.size() - kMaxBuildLogBytes;
        const std::size_t nl = buildLog_.find('\n', cut);
        buildLog_.erase(0, nl == std::string::npos ? cut : nl + 1);
    }

    if (!buildProgress_.finished) return;

    // Finished: take the report BEFORE the job is destroyed, because it lives
    // inside the job and the panel keeps it long after.
    buildReport_ = buildJob_->report();
    buildHaveReport_ = true;
    buildJob_.reset();   // joins the job's thread

    // The bundle that now exists (or the refusal that stopped it) changes what a
    // preflight would say -- an output directory that did not exist before does
    // now, and it carries this build's marker. Re-running once here is cheaper
    // than leaving the panel showing a check from before the build.
    runBuildPreflight_();

    setSceneStatus_(buildReport_.ok()
        ? ("Build succeeded: " + buildReport_.outputDirectory)
        : ("Build " + std::string(MyCoreEngine::BuildResultName(buildReport_.result)) +
           " - see Build Settings"));
}

void EditorApplication::cancelBuild_()
{
    if (!buildJob_) return;
    // Ask first, THEN destroy -- even though ~BuildJob does the same thing, for
    // the same reason it does: the job's thread is parked in a blocking pipe read
    // inside a compile that can run for minutes, and killing the child EOFs the
    // pipe so that read returns. Written out here rather than left to the
    // destructor because the SHUTDOWN PATH is the one that has to be obviously
    // right at a glance, and this is the same kill-then-join shape
    // cancelValidate_ above already establishes for the cooker.
    buildJob_->RequestCancel();
    buildJob_.reset();
}

void EditorApplication::startPlay_(MyCoreEngine::Scene& scene)
{
    if (playing_) return;
    undo_.cancelEdit(); // a half-open drag must not commit against play state
    playSnapshot_ = UndoHistory::captureScene(scene.registry);
    // Where Stop has to get back to, captured BEFORE the session can move it.
    playReturnScene_ = currentScenePath_;
    playSwapped_ = false;
    undo_.setRecordingEnabled(false);
    resetGameClock(); // deterministic first tick for every session
    // Build native bodies from the CURRENT (edit-mode) poses: play starts
    // from exactly what the author sees. Bodies live only for the session —
    // Stop destroys them and restores the pre-play scene.
    physics_.Rebuild(scene.registry);

    // Scripts load and start from the same edit-mode state. Build compiles
    // (surfacing syntax errors immediately) and Start runs OnStart, in that
    // order, so a broken file is reported before anything is executed.
    scripts_.SetInput(&input());
    scripts_.Rebuild(scene.registry);
    scripts_.Start(scene.registry);
    audio_.Start(scene.registry); // play-on-start sources begin now

    setGameplayEnabled(true);
    playing_ = true;
}

void EditorApplication::stopPlay_(MyCoreEngine::Scene& scene)
{
    if (!playing_) return;
    // THE MODE GOES FIRST, and unconditionally. A mode lives inside a play
    // session (see the game-modes block in Run()), so Play is the only door in
    // and Stop is therefore always the right time to leave -- there is no such
    // thing as a mode this Stop did not start, and remembering which ones it did
    // is how a mode ends up still ticking over a stopped scene.
    //
    // BEFORE the gameplay gates below, so the mode's last frame is a frame it was
    // still being ticked in rather than one where FixedTick had already stopped
    // arriving. Exit() gives back what Enter took -- for the fighting game that
    // is its Fight.* action names, which live in the host's SHARED InputMap and
    // would otherwise sit on the J and A/D/W keys of an editor that has never
    // heard of them. Safe with nothing active, which is the common case.
    modes_.Leave();
    setGameplayEnabled(false);
    setGameplayInputEnabled(false); // no game to receive input once stopped
    // BEFORE anything below: the swap observers read playing_ (to arm
    // playSwapped_) and gameplayEnabled() (to decide whether to rebuild). Both
    // must already say "stopped" or the reload underneath would re-arm the flag
    // it is answering and build bodies edit mode does not want.
    playing_ = false;
    undo_.setRecordingEnabled(true);

    if (playSwapped_) {
        // The session changed scene. The snapshot describes a DIFFERENT file,
        // so restoring it here would put the pre-Play entities into whatever
        // scene the game ended on -- and currentScenePath_ points at that one,
        // which makes the next Ctrl+S an unrecoverable overwrite.
        //
        // Go back to the file Play started from instead. Host origin, so the
        // edit-mode gate lets it through.
        playSwapped_ = false;
        const bool back = loadSceneFromFile_(scene, playReturnScene_);
        if (back) {
            // The load's own observers did the selection/undo/CSM work.
            playSnapshot_.clear();
            pendingModelOps_.erase(
                std::remove_if(pendingModelOps_.begin(), pendingModelOps_.end(),
                               [](const PendingModelOp& op) { return op.requestedDuringPlay; }),
                pendingModelOps_.end());
            return;
        }
        // Renamed, deleted or corrupted while the game was running. Fall
        // through to the snapshot restore: those entities are the only copy of
        // the pre-Play scene that still exists, and dropping them because the
        // FILE went missing would turn a recoverable problem into data loss.
        // currentScenePath_ is corrected below so Save still cannot go astray.
        setSceneStatus_("Stop: could not reload '" + playReturnScene_ +
                        "' - restored the pre-Play scene in memory instead");
        std::cerr << "[editor] " << sceneStatus_ << "\n";
    }

    // Drop every native body BEFORE the restore: restoreScene() clears the
    // registry and resurrects entities via create(hint), so the entity->body
    // map would survive looking valid while pointing at freed bodies.
    physics_.Clear();
    // Same hazard as bodies: restoreScene() clears the registry and
    // resurrects entities via create(hint), so every entity->instance pair
    // would survive looking valid while pointing at a destroyed instance.
    // Clearing here also fires OnDestroy while the entities still exist.
    scripts_.Clear();
    audio_.Clear();   // stop all voices when leaving Play
    UndoHistory::restoreScene(scene.registry, assets_.get(), playSnapshot_);
    playSnapshot_.clear();
    // The restored entities came from playReturnScene_, so that is what Save
    // must target -- not whatever the game swapped to.
    if (!playReturnScene_.empty()) {
        std::snprintf(currentScenePath_, sizeof(currentScenePath_), "%s",
                      playReturnScene_.c_str());
    }
    // handles survive the restore, but the Game view must CUT back to the
    // edit-mode camera — blending from the play session's last pose would
    // look like gameplay continuing after Stop
    gameDirector_.cut();
    // ops REQUESTED during play die with the session (their entities would
    // have been discarded by the restore anyway); edit-requested ops that
    // deferred across play stay and land in the restored edit scene
    pendingModelOps_.erase(
        std::remove_if(pendingModelOps_.begin(), pendingModelOps_.end(),
                       [](const PendingModelOp& op) { return op.requestedDuringPlay; }),
        pendingModelOps_.end());
    // selection normally survives (same handles); it only drops if a
    // play-created entity was selected at Stop
    if (!scene.registry.valid(selected_)) selected_ = entt::null;
    // The restore rewrites transforms wholesale, so the dirty-caster flow
    // never sees the play-end poses as "departures" — far cascades would
    // keep shadows baked where things stood when Stop was pressed. A full
    // rebuild on a user action is imperceptible.
    forceAllCSMUpdate_();
}

void EditorApplication::doUndo_(MyCoreEngine::Scene& scene)
{
    // Belt and braces behind the menu/shortcut gates: every history entry
    // describes the pre-play registry, so applying one into a live play session
    // resurrects entities under handles physics and scripts still hold.
    if (playing_) return;
    undo_.undo(scene.registry, assets_.get());
    if (!scene.registry.valid(selected_)) selected_ = entt::null;
    // snapshot restores overwrite the live matrix, so the departure pose
    // never reaches the dirty-caster flow — rebuild shadows outright
    forceAllCSMUpdate_();
}

void EditorApplication::doRedo_(MyCoreEngine::Scene& scene)
{
    if (playing_) return; // see doUndo_
    undo_.redo(scene.registry, assets_.get());
    if (!scene.registry.valid(selected_)) selected_ = entt::null;
    forceAllCSMUpdate_();
}

void EditorApplication::DrawEditHistory(MyCoreEngine::Scene& scene)
{
    ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Edit", &panels_.edit)) {
        if (playing_) {
            // play-mode changes are discarded by Stop, not undone; rewinding
            // history against play state would corrupt both
            ImGui::TextDisabled("(undo/redo disabled during play)");
            ImGui::End();
            return;
        }
        const bool canU = undo_.canUndo();
        const bool canR = undo_.canRedo();
        if (!canU) ImGui::BeginDisabled();
        if (ImGui::Button("Undo")) doUndo_(scene);
        if (!canU) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!canR) ImGui::BeginDisabled();
        if (ImGui::Button("Redo")) doRedo_(scene);
        if (!canR) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Ctrl+Z / Ctrl+Y");
        ImGui::Separator();

        const auto& entries = undo_.entries();
        if (entries.empty()) {
            ImGui::TextDisabled("(no edits yet)");
        }
        else {
            // clicking a row rewinds/replays history to just after that entry;
            // rows past the cursor are undone (dimmed)
            size_t target = entries.size() + 1; // sentinel: no click
            {
                const bool current = undo_.cursor() == 0;
                if (ImGui::Selectable("(initial state)", current)) target = 0;
            }
            for (size_t i = 0; i < entries.size(); ++i) {
                ImGui::PushID((int)i);
                const bool applied = i < undo_.cursor();
                if (!applied) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                                                  ImGui::GetStyle().Alpha * 0.45f);
                const auto& ops = entries[i].ops;
                const uint32_t primary = ops.empty() ? 0u : (uint32_t)ops[0].entity;
                char row[192];
                if (ops.size() > 1) {
                    snprintf(row, sizeof(row), "%d. %s [e%u +%d]", (int)i + 1,
                             entries[i].label.c_str(), primary, (int)ops.size() - 1);
                }
                else {
                    snprintf(row, sizeof(row), "%d. %s [e%u]", (int)i + 1,
                             entries[i].label.c_str(), primary);
                }
                if (ImGui::Selectable(row, applied && i + 1 == undo_.cursor())) {
                    target = i + 1;
                }
                if (!applied) ImGui::PopStyleVar();
                ImGui::PopID();
            }
            if (target <= entries.size()) {
                undo_.jumpTo(scene.registry, assets_.get(), target);
                if (!scene.registry.valid(selected_)) selected_ = entt::null;
                forceAllCSMUpdate_(); // see doUndo_
            }
        }
    }
    ImGui::End();
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

