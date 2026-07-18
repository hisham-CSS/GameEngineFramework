#pragma once
#include "Engine.h"
#include "EditorImGuiLayer.h"
#include "UndoHistory.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/AssetBrowserPanel.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() : Application(1280, 720, "Cat Splat Engine") {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

    // Scene viewport (god camera): renders the offscreen scene texture,
    // hosts the transform gizmo, and click-picks entities via ray-AABB.
    void DrawViewport(MyCoreEngine::Scene& scene);

    // Game viewport: what the game actually shows — rendered from the
    // scene's camera ENTITIES via a CameraDirector (priority selection +
    // blends, with a toolbar override picker) through a dedicated Renderer
    // (sharing the Scene view's renderer would thrash CSM cascades between
    // two frusta every frame). Skipped entirely while the panel is hidden.
    void DrawGameViewport(MyCoreEngine::Scene& scene, MyCoreEngine::Shader& shader,
                          float dt);

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

    // shared scene-load path (Scene panel button + asset browser): clears
    // undo/selection (stale handles) and forces a CSM rebuild (wholesale
    // scene replacement bypasses the departure-sphere dirty-caster flow —
    // old-scene shadows would stay baked in far cascades otherwise)
    bool loadSceneFromFile_(MyCoreEngine::Scene& scene, const std::string& path);
    bool setStartupScene_(const std::string& path); // updates buildSettingsStatus_
    // Shadow rebuilds must hit BOTH renderers — the Game view keeps its own
    // CSM state and would otherwise hold stale baked shadows.
    void forceAllCSMUpdate_() {
        renderer().forceCSMUpdate();
        gameRenderer_.forceCSMUpdate();
    }
    // spawn a model entity (undo-recorded, selects it); pos = world position
    void spawnModelEntity_(MyCoreEngine::Scene& scene, const std::string& path,
                           const glm::vec3& pos);

    EditorImGuiLayer ui_;                 // <-- persistent member
    SceneHierarchyPanel hierarchy_;
    InspectorPanel      inspector_;
    AssetBrowserPanel   assetBrowser_;
    entt::entity        selected_ = entt::null;

    // startup-scene display cache + status line (Scene panel); set from
    // either the panel button or the asset browser context menu
    std::string startupSceneDisplay_;
    std::string buildSettingsStatus_;
    bool startupSceneLoaded_ = false;

    MyCoreEngine::RenderTarget sceneTarget_;
    MyCoreEngine::RenderTarget gameTarget_;   // Game panel's offscreen target
    MyCoreEngine::Renderer gameRenderer_;     // own CSM state for the game frustum
    Camera gameCamera_;                       // scratch, written by gameDirector_
    // The Game panel's own director (the Application one only runs when
    // renderFromSceneCamera is on — the player). Reset whenever entity
    // handles go stale (scene load / new scene) or after play-stop's bulk
    // restore (cut back to the edit-mode camera, don't blend).
    MyCoreEngine::CameraDirector gameDirector_;
    MyCoreEngine::Shader* sceneShader_ = nullptr; // Run()'s shader (outlives the loop)
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