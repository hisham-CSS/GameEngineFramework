#pragma once
#include "Engine.h"
#include "EditorImGuiLayer.h"
#include "UndoHistory.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() : Application(1280, 720, "Cat Splat Engine") {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

    // Scene viewport: renders the offscreen scene texture, hosts the
    // transform gizmo, and click-picks entities via ray-AABB.
    void DrawViewport(MyCoreEngine::Scene& scene);

    void DrawInputPanel();

    // named layout save/load (Settings window; files in Layouts/*.ini)
    void DrawLayoutControls();

    void DrawTimeControls();

    void DrawScenePersistence(MyCoreEngine::Scene& scene);

    void DrawLightControls(MyCoreEngine::Scene& scene);

    void DrawIBLHDRControls(MyCoreEngine::Scene& scene);

    void DrawMaterialControls(MyCoreEngine::Scene& scene);

    void DrawSunShadowControls(MyCoreEngine::Scene& scene);

    void DrawRenderingToggles(MyCoreEngine::Scene& scene);

    void DrawInformationPanel(const MyCoreEngine::Scene& scene, float dt);

    // Undo/redo buttons + clickable command history (P2-7)
    void DrawEditHistory(MyCoreEngine::Scene& scene);

private:
    void pickEntity_(MyCoreEngine::Scene& scene, float mouseU, float mouseV,
                     const glm::mat4& view, const glm::mat4& proj);
    void doUndo_(MyCoreEngine::Scene& scene);
    void doRedo_(MyCoreEngine::Scene& scene);

    // play-in-editor (P2-6): Play snapshots the registry and enables the
    // gameplay hooks; Stop disables them and restores the snapshot under
    // the original entity handles (selection + undo history stay valid).
    void startPlay_(MyCoreEngine::Scene& scene);
    void stopPlay_(MyCoreEngine::Scene& scene);

    EditorImGuiLayer ui_;                 // <-- persistent member
    SceneHierarchyPanel hierarchy_;
    InspectorPanel      inspector_;
    entt::entity        selected_ = entt::null;

    MyCoreEngine::RenderTarget sceneTarget_;
    bool viewportHovered_ = false;
    bool viewportFocused_ = false; // Viewport is the focused ImGui window
    bool camLooking_ = false; // RMB look drag started over the viewport
    GLFWwindow* lookWindow_ = nullptr; // platform window holding the disabled cursor
    int  gizmoOp_ = 0; // 0 translate, 1 rotate, 2 scale

    UndoHistory undo_;
    bool gizmoWasUsing_ = false;   // edge-detects gizmo drag end for coalescing
    Transform gizmoBefore_{};      // selected entity's transform before the drag

    bool playing_ = false;                      // play-in-editor state
    UndoHistory::SceneSnapshot playSnapshot_;   // edit-mode scene, restored on Stop

    // layout .ini to load before the next frame (empty = none). Deferred
    // because settings must be (re)applied outside NewFrame/Render.
    std::string pendingLayoutLoad_;

    std::unique_ptr<MyCoreEngine::AssetManager> assets_;
};