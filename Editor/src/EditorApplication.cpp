#include "EditorApplication.h"

#include "Engine.h"                 // Renderer/Scene/Shader headers aggregated or include individually
#include "EditorImGuiLayer.h"
#include "ImGuiInputMap.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

#include "imgui.h"
#include "ImGuizmo.h"

#include <glm/gtc/type_ptr.hpp>
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

        // Multi-viewport input routing: keyboard/mouse polls go through
        // ImGui (aggregated across detached OS windows), and the editor
        // drives camera look/zoom itself in DrawViewport — the engine's
        // raw main-window mouse-look can't see panels on other monitors.
        installInput(std::make_unique<ImGuiInputMap>());
        setInternalCameraInput(false);

        // scene renders offscreen; the Viewport panel displays (and resizes) it
        sceneTarget_.Create(1280, 720);
        SetSceneRenderTarget(&sceneTarget_);

        // SAFE: capture 'this' (EditorApplication) whose lifetime spans the run loop
        SetUICaptureProvider([this] {
            // Camera keys act only when the user is actually IN the viewport:
            // focused, hovered, or mid-RMB-look. The old rule ("fly unless
            // typing") predates multi-viewports — with input aggregated
            // across detached OS windows, it moved the camera while
            // scrolling/keyboarding in panels on other monitors. Text editing
            // still blocks regardless (WantTextInput).
            const bool inViewport = viewportFocused_ || viewportHovered_ || camLooking_;
            return std::pair<bool, bool>{
                ui_.WantTextInput() || !inViewport,
                    // the viewport is an ImGui window too — camera controls
                    // must keep working while the mouse is over it
                    ui_.WantCaptureMouse() && !viewportHovered_
            };
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

        DrawViewport(scene);

        //Information Panel
        DrawInformationPanel(scene, dt);

        // Engine/render controls. These used to be bare CollapsingHeaders,
        // which ImGui collects into its implicit "Debug##Default" fallback
        // window — and the fallback window can never dock. A real named
        // window makes them a first-class dockable panel.
        ImGui::SetNextWindowSize(ImVec2(360, 540), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings")) {
            DrawScenePersistence(scene);
            DrawTimeControls();
            DrawInputPanel();
            DrawRenderingToggles(scene);
            DrawLightControls(scene);
            DrawSunShadowControls(scene);
            DrawMaterialControls(scene);
            DrawIBLHDRControls(scene);
            DrawLayoutControls();
        }
        ImGui::End();

		//scene hierarchy
        hierarchy_.Draw(scene.registry, selected_, undo_);

		//inspector
        if (inspector_.Draw(scene.registry, selected_, undo_, assets_.get())) {
            // caster set changed without a transform dirtying (model swap /
            // remove / shadow toggle): stale shadows stay baked otherwise
            renderer().forceCSMUpdate();
        }

        // commit any edit whose widget stopped being submitted this frame
        // (deselect while a text field was focused, tab switch, collapse)
        undo_.tickFrame(scene.registry);

        //asset browser (P2-5)
        {
            const AssetBrowserActions aba = assetBrowser_.Draw(
                scene.registry, selected_, undo_, assets_.get(), playing_);
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
            if (aba.shadowsDirty) renderer().forceCSMUpdate();
        }

        //undo/redo history (P2-7)
        DrawEditHistory(scene);

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

    assert(glfwGetCurrentContext() != nullptr);
    // --- Load or reuse a model by path ---
    auto modelHandle = assets_->GetModel("Exported/Model/backpack.obj");  // shared
    AABB localBV = generateAABB(*modelHandle); // if you still use local-space AABB
    
    // 20x20 grid; the center entity (at the origin) is named "Hero" and is
    // spun by the FixedUpdate demo below. (There used to be a separate hero
    // entity on top of the grid one — two backpacks clipping at the origin.)
    for (unsigned int x = 0; x < 20; ++x) {
        for (unsigned int z = 0; z < 20; ++z) {
            Entity e = scene.createEntity();
            Transform t{};
            t.position = glm::vec3(x * 10.f - 100.f, 0.f, z * 10.f - 100.f);
            e.addComponent<Transform>(t);
            e.addComponent<ModelComponent>(ModelComponent{ modelHandle });
			e.addComponent<AABB>(localBV); // if you still use local-space AABB
            if (x == 10 && z == 10) {
                e.addComponent<Name>(Name{ "Hero" }); // sits at (0, 0, 0)
            }
        }
    }

    // Ground plane: receives the sun shadows (NoShadow = doesn't cast its own)
    {
        auto groundHandle = assets_->GetModel("Exported/Model/plane.obj");
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
    }

    // Demo gameplay (shared with the standalone player): spin entities named
    // "Hero". Only ticks between Play and Stop here — the gameplay gate is
    // off in edit mode.
    MyCoreEngine::InstallDemoGameplay(*this, scene);

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
    const bool open = ImGui::Begin("Viewport", nullptr,
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
    const glm::mat4 proj = glm::perspective(glm::radians(cam.Zoom), aspect, 0.1f, 1000.0f);

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

void EditorApplication::DrawScenePersistence(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_None)) return;

    static char scenePath[260] = "Exported/scene.json";
    static std::string lastStatus;
    ImGui::InputText("Scene file", scenePath, sizeof(scenePath));

    MyCoreEngine::SceneSerializer serializer(scene, *assets_);
    // Saving mid-play would persist transient play state; loading/newing
    // would be overwritten by Stop's snapshot restore anyway.
    if (playing_) ImGui::BeginDisabled();
    if (ImGui::Button("New Scene")) {
        ImGui::OpenPopup("New Scene?");
    }
    if (ImGui::BeginPopupModal("New Scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Replace the current scene with an empty one?");
        ImGui::TextUnformatted("Unsaved changes will be lost.");
        ImGui::Separator();
        if (ImGui::Button("New Scene", ImVec2(120, 0))) {
            scene.ResetToDefaults();
            selected_ = entt::null; // every entity handle is gone
            undo_.clear();          // ...including all of the history's
            // wholesale caster removal bypasses the departure-sphere flow:
            // the old scene's shadows would stay baked otherwise
            renderer().forceCSMUpdate();
            lastStatus = "New scene";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Scene")) {
        lastStatus = serializer.Save(scenePath)
            ? std::string("Saved to ") + scenePath
            : std::string("Save FAILED (see console)");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scene")) {
        lastStatus = loadSceneFromFile_(scene, scenePath)
            ? std::string("Loaded ") + scenePath
            : std::string("Load FAILED (see console)");
    }
    if (playing_) ImGui::EndDisabled();
    if (!lastStatus.empty()) ImGui::TextDisabled("%s", lastStatus.c_str());

    // --- Build Settings: which scene the standalone player boots into ---
    ImGui::SeparatorText("Build Settings");
    if (!startupSceneLoaded_) {
        MyCoreEngine::ProjectSettings s;
        s.Load();
        startupSceneDisplay_ = s.startupScene;
        startupSceneLoaded_ = true;
    }
    ImGui::Text("Player startup scene: %s", startupSceneDisplay_.c_str());
    if (ImGui::Button("Set Current File as Startup Scene")) {
        setStartupScene_(scenePath); // outcome lands in buildSettingsStatus_
    }
    if (!buildSettingsStatus_.empty()) ImGui::TextDisabled("%s", buildSettingsStatus_.c_str());
}

void EditorApplication::DrawLightControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Lights (Shading)", ImGuiTreeNodeFlags_None)) return;
    auto& Ld = scene.LightDir();
    auto& Lc = scene.LightColor();
    auto& Li = scene.LightIntensity();
    ImGui::SeparatorText("Direct Light");
    ImGui::DragFloat3("Dir", &Ld.x, 0.01f);
    ImGui::ColorEdit3("Color", &Lc.x);
    ImGui::SliderFloat("Intensity", &Li, 0.0f, 10.0f);
}

void EditorApplication::DrawIBLHDRControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("IBL/HDR", ImGuiTreeNodeFlags_None)) return;

    bool ibl = scene.GetIBLEnabled();
    if (ImGui::Checkbox("Enable IBL", &ibl)) scene.SetIBLEnabled(ibl);

    float iblInt = scene.GetIBLIntensity();
    if (ImGui::SliderFloat("IBL Intensity", &iblInt, 0.0f, 4.0f)) scene.SetIBLIntensity(iblInt);

    float exposure = renderer().exposure();
    if (ImGui::SliderFloat("Exposure", &exposure, 0.2f, 5.0f)) renderer().setExposure(exposure);
}

void EditorApplication::DrawMaterialControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_None)) return;
    float metallic = scene.GetMetallic();
    float roughness = scene.GetRoughness();
    float ao = scene.GetAO();
    if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) scene.SetMetallic(metallic);
    if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) scene.SetRoughness(roughness);
    if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f)) scene.SetAO(ao);
    bool enMetal = scene.GetMetallicMapEnabled();
    bool enRough = scene.GetRoughnessMapEnabled();
    bool enAO = scene.GetAOMapEnabled();
    if (ImGui::Checkbox("Use Metallic Map", &enMetal))  scene.SetMetallicMapEnabled(enMetal);
    if (ImGui::Checkbox("Use Roughness Map", &enRough)) scene.SetRoughnessMapEnabled(enRough);
    if (ImGui::Checkbox("Use AO Map", &enAO))           scene.SetAOMapEnabled(enAO);
}

void EditorApplication::DrawSunShadowControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Sun / Shadows Controls", ImGuiTreeNodeFlags_None)) return;
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

    if (ImGui::Button("Force Rebuild CSM")) renderer().forceCSMUpdate();
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
    if (!ImGui::CollapsingHeader("Rendering Toggles", ImGuiTreeNodeFlags_None)) return;

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

    bool nm = scene.GetNormalMapEnabled();
    if (ImGui::Checkbox("Enable normal mapping", &nm)) scene.SetNormalMapEnabled(nm);

    bool pbr = scene.GetPBREnabled();
    if (ImGui::Checkbox("Enable PBR (Cook-Torrance)", &pbr)) scene.SetPBREnabled(pbr);
}

void EditorApplication::DrawInformationPanel(const MyCoreEngine::Scene& scene, float dt)
{
    const auto& rs = scene.GetRenderStats();
    ImGui::Begin("Information", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::CollapsingHeader("Rendering Stats", ImGuiTreeNodeFlags_None)) {
        ImGui::Text("dt: %.3f ms (%.1f FPS)", dt * 1000.f, dt > 0.f ? 1.f / dt : 0.f);
        ImGui::Text("Cascades: %d, res: %d", renderer().getCSMNumCascades(), renderer().getCSMBaseResolution());
        ImGui::Text("Draws:            %u", rs.draws);
        ImGui::Text("Instanced draws:  %u", rs.instancedDraws);
        ImGui::Text("Instances:        %u", rs.instances);
        ImGui::Separator();
        ImGui::Text("Texture binds:    %u", rs.textureBinds);
        ImGui::Text("VAO binds:        %u", rs.vaoBinds);
        ImGui::Separator();
        ImGui::Text("Built items:      %u", rs.itemsBuilt);
        ImGui::Text("Culled:           %u", rs.culled);
        ImGui::Text("Submitted:        %u", rs.submitted);
        ImGui::Text("LOD 0/1/2:        %u / %u / %u",
            rs.lodInstances[0], rs.lodInstances[1], rs.lodInstances[2]);
        unsigned totalCalls = rs.draws + rs.instancedDraws;
        ImGui::Text("GPU draw calls:   %u", totalCalls);
    }
    ImGui::End();
}

bool EditorApplication::loadSceneFromFile_(MyCoreEngine::Scene& scene, const std::string& path)
{
    MyCoreEngine::SceneSerializer serializer(scene, *assets_);
    if (!serializer.Load(path)) return false;
    selected_ = entt::null; // old entity handles are invalid after a load
    // ...and so is every handle in the undo history: undoing a pre-load
    // entry would resurrect ghosts into the loaded scene
    undo_.clear();
    // wholesale scene replacement bypasses the departure-sphere flow: the
    // old scene's shadows would stay baked in cascades the new content
    // doesn't touch (same class of bug as play-mode stop-restore)
    renderer().forceCSMUpdate();
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
    auto model = assets_->GetModel(path);
    if (!model || model->Meshes().empty()) return;

    MyCoreEngine::Entity e = scene.createEntity();
    const std::string stem = std::filesystem::path(path).stem().string();
    e.addComponent<Name>(Name{ stem.empty() ? std::string("Entity") : stem });
    Transform t{};
    t.position = pos;
    e.addComponent<Transform>(t);
    e.addComponent<ModelComponent>(ModelComponent{ model });
    e.addComponent<AABB>(generateAABB(*model));
    undo_.recordCreate(scene.registry, e, "Spawn '" + stem + "'");
    selected_ = e;
}

void EditorApplication::startPlay_(MyCoreEngine::Scene& scene)
{
    if (playing_) return;
    undo_.cancelEdit(); // a half-open drag must not commit against play state
    playSnapshot_ = UndoHistory::captureScene(scene.registry);
    undo_.setRecordingEnabled(false);
    resetGameClock(); // deterministic first tick for every session
    setGameplayEnabled(true);
    playing_ = true;
}

void EditorApplication::stopPlay_(MyCoreEngine::Scene& scene)
{
    if (!playing_) return;
    setGameplayEnabled(false);
    UndoHistory::restoreScene(scene.registry, assets_.get(), playSnapshot_);
    playSnapshot_.clear();
    undo_.setRecordingEnabled(true);
    playing_ = false;
    // selection normally survives (same handles); it only drops if a
    // play-created entity was selected at Stop
    if (!scene.registry.valid(selected_)) selected_ = entt::null;
    // The restore rewrites transforms wholesale, so the dirty-caster flow
    // never sees the play-end poses as "departures" — far cascades would
    // keep shadows baked where things stood when Stop was pressed. A full
    // rebuild on a user action is imperceptible.
    renderer().forceCSMUpdate();
}

void EditorApplication::doUndo_(MyCoreEngine::Scene& scene)
{
    undo_.undo(scene.registry, assets_.get());
    if (!scene.registry.valid(selected_)) selected_ = entt::null;
    // snapshot restores overwrite the live matrix, so the departure pose
    // never reaches the dirty-caster flow — rebuild shadows outright
    renderer().forceCSMUpdate();
}

void EditorApplication::doRedo_(MyCoreEngine::Scene& scene)
{
    undo_.redo(scene.registry, assets_.get());
    if (!scene.registry.valid(selected_)) selected_ = entt::null;
    renderer().forceCSMUpdate();
}

void EditorApplication::DrawEditHistory(MyCoreEngine::Scene& scene)
{
    ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Edit")) {
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
                renderer().forceCSMUpdate(); // see doUndo_
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

