#pragma once
#include "Engine.h"
#include "EditorImGuiLayer.h"
#include "UndoHistory.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/AssetBrowserPanel.h"

#include <atomic>
#include <thread>

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() : Application(1280, 720, "Cat Splat Engine") {}
    ~EditorApplication() { cancelValidate_(); } // kill a hung cooker child

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

    // Main menu bar (File menu) + its New/Open/Save-As modals. Replaces the
    // old "Scene" settings tab; scene file operations belong under the title
    // bar, Unity-style, not buried in a tab.
    void DrawMainMenuBar(MyCoreEngine::Scene& scene);
    bool saveScene_(MyCoreEngine::Scene& scene);      // -> currentScenePath_
    void saveAll_(MyCoreEngine::Scene& scene);        // scene + editor layout
    void newScene_(MyCoreEngine::Scene& scene);       // reset + default content

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
    // Minimal starting content (Main Camera + ground) used when no startup
    // scene exists and by New Scene. Never leaves the scene camera-less.
    void createDefaultScene_(MyCoreEngine::Scene& scene);
    bool setStartupScene_(const std::string& path); // updates buildSettingsStatus_
    // Shadow rebuilds must hit BOTH renderers — the Game view keeps its own
    // CSM state and would otherwise hold stale baked shadows.
    void forceAllCSMUpdate_() {
        renderer().forceCSMUpdate();
        gameRenderer_.forceCSMUpdate();
    }
    // Async model ops (P4-3 phase 3): spawn/assign REQUEST the model and
    // return immediately; the entity appears (undo-recorded) when the
    // decode lands, at the position/target captured at request time.
    // Cache hits complete inline, same frame as before.
    void spawnModelEntity_(MyCoreEngine::Scene& scene, const std::string& path,
                           const glm::vec3& pos);
    void assignModelToEntity_(MyCoreEngine::Scene& scene, const std::string& path,
                              entt::entity target);
    void pollPendingModelOps_(MyCoreEngine::Scene& scene);
    // spawn `AssetCooker validate` as a child process on a worker thread
    // (crash isolation: hostile assets take down the cooker, not us)
    void startValidate_();
    void finishSpawn_(MyCoreEngine::Scene& scene,
                      const std::shared_ptr<MyCoreEngine::Model>& model,
                      const glm::vec3& pos);
    void finishAssign_(MyCoreEngine::Scene& scene,
                       const std::shared_ptr<MyCoreEngine::Model>& model,
                       entt::entity target);

    EditorImGuiLayer ui_;                 // <-- persistent member
    SceneHierarchyPanel hierarchy_;
    InspectorPanel      inspector_;
    AssetBrowserPanel   assetBrowser_;
    // Engine-side asset filesystem domain: cached tree of Exported/, all
    // disk walking + rescan throttling live here; the panel is a view.
    MyCoreEngine::AssetIndex assetIndex_;
    entt::entity        selected_ = entt::null;

    // async model requests awaiting their decode (spawn ops carry the
    // drop position captured at request time; assign ops carry the target
    // entity, revalidated at completion — it can die in the meantime).
    // Edit/play boundary: the poll is GATED off during play, so ops
    // requested in edit mode defer across a play session and land in the
    // restored edit scene (undo-recorded); ops requested DURING play are
    // tagged and dropped at Stop — their intent died with the session,
    // matching the old sync semantics where play spawns were discarded.
    struct PendingModelOp {
        MyCoreEngine::AssetManager::ModelRequestHandle req;
        glm::vec3 spawnPos{ 0.f };
        entt::entity assignTo = entt::null; // null = spawn op
        bool requestedDuringPlay = false;
    };
    std::vector<PendingModelOp> pendingModelOps_;

    // Which view the Inspector shows: an asset click hands it to the
    // asset view; a NEWLY selected entity (hierarchy, viewport pick,
    // spawn) reclaims it. The entity selection itself is never cleared by
    // asset clicks — "assign to selected" flows depend on it surviving.
    bool inspectorShowsAsset_ = false;

    // AssetCooker validation run: child process via CreateProcess (we hold
    // the process handle so shutdown can kill a hung cooker) with its
    // stdout piped; a dedicated reader thread drains the pipe (NOT a pool
    // worker — a blocked pipe read must never stall asset decodes or the
    // RunLoop exit drain). stderr stays on the editor console.
    struct ValidateRun {
        std::thread reader;
        void* process = nullptr;      // HANDLE
        std::atomic<bool> done{ false };
        std::string output;           // reader-owned until done
        unsigned long exitCode = 0;
    };
    std::unique_ptr<ValidateRun> validateRun_;
    void cancelValidate_();           // kill + join (shutdown path)
    bool validateRunning_ = false;
    bool validateOpen_ = false;       // report window visibility
    std::string validateReport_;

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

    // Physics: one active backend for the whole world. Bodies exist only for
    // the duration of a play session (built in startPlay_, cleared in
    // stopPlay_), so edit-mode poses are never disturbed by the solver.
    MyCoreEngine::PhysicsWorld physics_;
    std::string physicsStatus_; // last backend switch result, shown in Settings

    // Scripting: same lifecycle as physics. Instances are created on Play and
    // destroyed on Stop, so a script can never mutate the edit-mode scene the
    // author is looking at.
    MyCoreEngine::ScriptWorld scripts_;

    // Game panel focus. Gameplay receives input only while this is true, so
    // the Scene view stays navigable with the same keys while playing.
    bool gameViewFocused_ = false;
    std::string bootStatus_;    // what happened at startup (loaded / defaulted)
    char        currentScenePath_[260] = "Exported/scene.json"; // File menu target
    std::string sceneStatus_;   // last save/load result, shown in the menu bar

    // layout .ini to load before the next frame (empty = none). Deferred
    // because settings must be (re)applied outside NewFrame/Render.
    std::string pendingLayoutLoad_;

    std::unique_ptr<MyCoreEngine::AssetManager> assets_;
};