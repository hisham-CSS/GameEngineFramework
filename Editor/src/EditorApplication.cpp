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
#include <cstdio>
#include <filesystem>
#include <vector>


//for the future for any initalization things that are required
void EditorApplication::Initialize()
{
    // Load resources once during initialization:
    MyCoreEngine::SetImageFlipVerticallyOnLoad(true); // or false
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
            caps.keyboard = ui_.WantTextInput() || !inViewport;
            // the viewport is an ImGui window too — camera controls
            // must keep working while the mouse is over it
            caps.mouse = ui_.WantCaptureMouse() && !viewportHovered_;
            // Reported separately because `keyboard` above is mostly about
            // WHERE the pointer is, not about typing. Gameplay input keys off
            // this narrow flag alone.
            caps.textInput = ui_.WantTextInput();
            return caps;
        });
    });

    SetUIDraw([this, &scene](float dt) {

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

        //Information Panel (reads the SCENE view's render stats — draw it
        //before the Game view renders and overwrites them)
        if (panels_.information) DrawInformationPanel(scene, dt);

        //Game view: what the primary camera entity sees
        if (panels_.game) DrawGameViewport(scene, *sceneShader_, dt);

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
                    DrawTimeControls();
                    DrawInputPanel();
                    DrawLayoutControls();
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

            assetIndex_.tick(dt, &jobs()); // throttled; the walk runs on a worker
            // gated off during play: an edit-mode drop landing mid-play
            // would spawn into the play scene, record no undo (recording
            // disabled), and be silently destroyed by Stop's restore —
            // deferring applies it to the restored edit scene instead
            if (!playing_) pollPendingModelOps_(scene);
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
        if (panels_.hierarchy) hierarchy_.Draw(scene.registry, selected_, undo_, &panels_.hierarchy);

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
                                 &panels_.inspector)) {
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
        if (io.KeyCtrl && !io.WantTextInput &&
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
        const std::string startup = settings.startupScene.empty()
            ? std::string("Exported/scene.json") : settings.startupScene;

        MyCoreEngine::SceneSerializer bootSerializer(scene, *assets_);
        if (bootSerializer.Load(startup)) {
            bootStatus_ = "Loaded startup scene: " + startup;
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

        // Audio: per-frame listener/source update installed for the app's life;
        // voices are populated by audio_.Start() on Play.
        MyCoreEngine::InstallAudio(*this, scene, audio_);
    }

    RunLoop(scene, *shader);
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

void EditorApplication::DrawGameViewport(MyCoreEngine::Scene& scene,
                                         MyCoreEngine::Shader& shader, float dt)
{
    using namespace MyCoreEngine;

    ImGui::SetNextWindowSize(ImVec2(640, 400), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
    const bool open = ImGui::Begin("Game", &panels_.game,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) {
        // hidden/collapsed: skip the whole second scene render
        gameViewFocused_ = false;
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
    setGameplayInputEnabled(playing_ && gameViewFocused_);

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

        // Say WHERE input is going. Silence here is what made a working jump
        // look broken: the key was fine, the panel just did not have focus.
        if (playing_) {
            ImGui::SameLine();
            if (gameViewFocused_) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.f), "| Input: game");
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f), "| Click to give input to the game");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Gameplay reads input only while this panel is focused,\n"
                                  "so the Scene view stays navigable while playing.");
            }
        }
    }

    if (!gameDirector_.Update(scene.registry, dt, gameCamera_)) {
        ImGui::TextDisabled("No camera in the scene.");
        ImGui::TextDisabled("Select an entity and use Inspector > Add Component > Camera.");
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 8.f && avail.y >= 8.f) {
        gameTarget_.Resize((int)avail.x, (int)avail.y);
    }

    // Keep the look coherent with the Scene view: scene-level state (lights,
    // materials, toggles) is shared via the Scene itself; these few live on
    // the renderer and must be mirrored. Force direct-dir mode first —
    // otherwise the game renderer's own yaw/pitch default overwrites the
    // mirrored direction and sun edits never reach the Game view.
    gameRenderer_.setUseSunYawPitch(false);
    gameRenderer_.setSunDir(renderer().sunDir());
    gameRenderer_.setExposure(renderer().exposure());
    gameRenderer_.setCSMEnabled(renderer().getCSMEnabled());

    if (gameTarget_.fbo() && gameTarget_.width() > 0) {
        gameRenderer_.RenderFrame(scene, shader, gameCamera_,
                                  gameTarget_.width(), gameTarget_.height(), dt,
                                  gameTarget_.fbo());
        // mid-UI-frame offscreen render: restore the backbuffer binding —
        // ImGui's backend renders into whatever framebuffer is bound
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    const ImVec2 imageSize(avail.x > 1.f ? avail.x : 1.f, avail.y > 1.f ? avail.y : 1.f);
    if (gameTarget_.colorTexture()) {
        // GL textures are bottom-up: flip V
        ImGui::Image((ImTextureID)(intptr_t)gameTarget_.colorTexture(),
                     imageSize, ImVec2(0, 1), ImVec2(1, 0));
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
    MyCoreEngine::SceneSerializer serializer(scene, *assets_);
    const bool ok = serializer.Save(currentScenePath_);
    sceneStatus_ = ok ? (std::string("Saved ") + currentScenePath_)
                      : "Save FAILED (see console)";
    return ok;
}

void EditorApplication::saveAll_(MyCoreEngine::Scene& scene)
{
    // "Everything currently saveable": the scene, plus the editor layout, which
    // ImGui otherwise only persists on a clean shutdown. Startup-scene is a
    // deliberate build setting, not dirty state, so it stays its own action.
    const bool sceneOk = saveScene_(scene);
    if (const char* ini = ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ini);
    sceneStatus_ = sceneOk ? "Saved all (scene + layout)"
                           : "Scene save FAILED (layout saved)";
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
    // wholesale caster removal bypasses the departure-sphere flow: the old
    // scene's shadows would stay baked otherwise
    forceAllCSMUpdate_();
    sceneStatus_ = "New scene";
}

void EditorApplication::DrawMainMenuBar(MyCoreEngine::Scene& scene)
{
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
                if (ImGui::MenuItem("Set Current Scene as Player Startup", nullptr, false, canEdit)) {
                    setStartupScene_(currentScenePath_);
                    sceneStatus_ = buildSettingsStatus_; // surface the result in the bar
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                    glfwSetWindowShouldClose(GetNativeWindow(), 1);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                const bool canU = undo_.canUndo();
                const bool canR = undo_.canRedo();
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
                if (ImGui::MenuItem("Clear History", nullptr, false, canU || canR)) undo_.clear();
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
                ImGui::Separator();
                if (ImGui::MenuItem("Show All Panels")) panels_ = PanelVis{};
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
    if (canEdit && io.KeyCtrl && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift) saveAll_(scene); else saveScene_(scene);
    }

    if (openNew)    ImGui::OpenPopup("New Scene?");
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
        ImGui::InputText("##openpath", currentScenePath_, sizeof(currentScenePath_));
        ImGui::TextDisabled("Tip: double-clicking a .json in the Asset browser also loads it.");
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            sceneStatus_ = loadSceneFromFile_(scene, currentScenePath_)
                ? (std::string("Loaded ") + currentScenePath_)
                : "Load FAILED (see console)";
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
        ImGui::InputText("##savepath", currentScenePath_, sizeof(currentScenePath_));
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
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

    // Optionally sync scene shading light with sun
    bool useSunForShading = true;
    if (ImGui::Checkbox("Use Sun Dir for Shading Light", &useSunForShading)) {
        if (useSunForShading) scene.LightDir() = renderer().sunDir();
    }
    //if (useSunForShading) scene.LightDir() = renderer().sunDir();
    // --- CSM Controls ---
    ImGui::SeparatorText("Cascaded Shadows");

    bool on = renderer().getCSMEnabled();
    if (ImGui::Checkbox("CSM Enabled", &on)) renderer().setCSMEnabled(on);

    int casc = renderer().getCSMNumCascades();
    if (ImGui::SliderInt("Cascades", &casc, 1, 4)) renderer().setCSMNumCascades(casc);

    int res = renderer().getCSMBaseResolution();
    if (ImGui::SliderInt("Base Resolution", &res, 512, 4096)) renderer().setCSMBaseResolution(res);

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
    if (ImGui::Checkbox("Anti-aliasing (FXAA)", &aa)) scene.SetAAEnabled(aa);
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
        ImGui::Checkbox("Bloom", &pfx.bloom.enabled);
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
    if (ImGui::Checkbox("Depth prepass", &prepass)) scene.SetDepthPrepassEnabled(prepass);
    ImGui::SameLine(); ImGui::TextDisabled("(shade each pixel once)");

    bool lod = scene.GetLODEnabled();
    if (ImGui::Checkbox("Enable mesh LOD", &lod)) scene.SetLODEnabled(lod);
    float lodScale = scene.GetLODDistanceScale();
    if (ImGui::SliderFloat("LOD distance scale", &lodScale, 0.25f, 4.f)) scene.SetLODDistanceScale(lodScale);
    ImGui::SameLine(); ImGui::TextDisabled("(higher = detail farther)");

    // Projected-size cull: the lever that actually speeds up wide/bird's-eye
    // views (they're vertex/instance-bound; shadows/fill are effectively free).
    // Higher pixel floor = more culled + more distant popping.
    bool smallCull = scene.GetSmallCullEnabled();
    if (ImGui::Checkbox("Cull tiny objects", &smallCull)) scene.SetSmallCullEnabled(smallCull);
    float smallPx = scene.GetSmallCullPixels();
    if (ImGui::SliderFloat("Min on-screen px", &smallPx, 0.f, 48.f, "%.1f px")) scene.SetSmallCullPixels(smallPx);
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

bool EditorApplication::loadSceneFromFile_(MyCoreEngine::Scene& scene, const std::string& path)
{
    MyCoreEngine::SceneSerializer serializer(scene, *assets_);
    if (!serializer.Load(path)) return false;
    // Re-apply the quality tier: the perf toggles serialize, but the CSM
    // cascade/resolution part of a tier lives in the Renderer (not serialized),
    // so a reloaded High scene would otherwise get default shadows.
    if (scene.GetQualityLevel() != MyCoreEngine::Scene::QualityLevel::Custom)
        renderer().ApplyQualityTier(scene.GetQualityLevel(), scene);
    selected_ = entt::null; // old entity handles are invalid after a load
    // ...and so is every handle in the undo history: undoing a pre-load
    // entry would resurrect ghosts into the loaded scene
    undo_.clear();
    gameDirector_.reset(); // Game view camera/override handles are stale too
    pendingModelOps_.clear(); // in-flight spawns/assigns were aimed at the old scene
    // wholesale scene replacement bypasses the departure-sphere flow: the
    // old scene's shadows would stay baked in cascades the new content
    // doesn't touch (same class of bug as play-mode stop-restore)
    forceAllCSMUpdate_();
    // ...and every entity->body pair now refers to the OLD scene's entities
    physics_.Clear();
    scripts_.Clear(); // ...as does every entity->script-instance pair
    audio_.Clear();   // stop any voices from the old scene
    return true;
}

bool EditorApplication::setStartupScene_(const std::string& path)
{
    MyCoreEngine::ProjectSettings s;
    s.Load(); // preserves the fields the struct knows; Save rewrites the file
    s.startupScene = path;
    const bool ok = s.Save();
    if (ok) {
        startupSceneDisplay_ = path;
        startupSceneLoaded_ = true;
        buildSettingsStatus_ = "Saved to Exported/project.json (ships with the game)";
    }
    else {
        buildSettingsStatus_ = "Save FAILED (see console)";
    }
    return ok;
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

void EditorApplication::startPlay_(MyCoreEngine::Scene& scene)
{
    if (playing_) return;
    undo_.cancelEdit(); // a half-open drag must not commit against play state
    playSnapshot_ = UndoHistory::captureScene(scene.registry);
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
    setGameplayEnabled(false);
    // Drop every native body BEFORE the restore: restoreScene() clears the
    // registry and resurrects entities via create(hint), so the entity->body
    // map would survive looking valid while pointing at freed bodies.
    setGameplayInputEnabled(false); // no game to receive input once stopped
    physics_.Clear();
    // Same hazard as bodies: restoreScene() clears the registry and
    // resurrects entities via create(hint), so every entity->instance pair
    // would survive looking valid while pointing at a destroyed instance.
    // Clearing here also fires OnDestroy while the entities still exist.
    scripts_.Clear();
    audio_.Clear();   // stop all voices when leaving Play
    UndoHistory::restoreScene(scene.registry, assets_.get(), playSnapshot_);
    playSnapshot_.clear();
    undo_.setRecordingEnabled(true);
    playing_ = false;
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
    undo_.undo(scene.registry, assets_.get());
    if (!scene.registry.valid(selected_)) selected_ = entt::null;
    // snapshot restores overwrite the live matrix, so the departure pose
    // never reaches the dirty-caster flow — rebuild shadows outright
    forceAllCSMUpdate_();
}

void EditorApplication::doRedo_(MyCoreEngine::Scene& scene)
{
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

