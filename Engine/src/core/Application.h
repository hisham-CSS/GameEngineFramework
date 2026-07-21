#pragma once
#include "Core.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "Window.h"
#include "InputMap.h"
#include "Camera.h"
#include "CameraDirector.h"
#include "FixedTimestep.h"
#include "JobSystem.h"
#include "Renderer.h"
#include "RenderTarget.h"

namespace MyCoreEngine
{
	// Owns the application side of the engine: window, input, camera, timing,
	// and the main loop. The Renderer it owns only renders. Apps (Editor,
	// Player) subclass this, do their setup in Run(), then call RunLoop().
	class ENGINE_API Application
	{
	public:
		Application(int width, int height, const char* title);
		virtual ~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		virtual void Run() = 0;

		// Loads GLAD, sets up the renderer + window callbacks, applies vsync,
		// and fires the OnContextReady hook. Call once before RunLoop().
		void InitGL();

		// The main loop: input -> fixed/variable game updates -> transforms ->
		// render -> UI -> present. Returns when the window closes.
		void RunLoop(Scene& scene, Shader& shader);

		// --- subsystem access ---
		Renderer& renderer() { return renderer_; }
		InputMap& input() { return *input_; }
		Camera&   camera() { return camera_; }
		// The Cinemachine-style director that picks (and blends between)
		// scene camera entities when renderFromSceneCamera is on. Gameplay
		// code tunes it here: setDefaultBlendSeconds for smooth camera
		// takes, setOverride for scripted hard control. Camera switching
		// itself is data-driven — raise a CameraComponent's priority.
		CameraDirector& cameraDirector() { return director_; }
		// The engine thread pool (P4-3). Submit CPU work from anywhere;
		// completions run on the main thread each frame (RunLoop pumps
		// them with the GL context current — that's where uploads go).
		JobSystem& jobs() { return jobs_; }
		Window&   window() { return window_; }
		GLFWwindow* GetNativeWindow() { return window_.getGLFWwindow(); }

		// Swap the input backend (e.g. the editor's ImGui-routed InputMap,
		// which aggregates input across detached multi-viewport windows).
		// Default fly-camera bindings are re-applied to the new map.
		void installInput(std::unique_ptr<InputMap> map);

		// When off, the engine's raw main-window mouse-look and scroll-zoom
		// are skipped — the app drives the camera itself (the editor does
		// this via ImGui input so it works across detached panels).
		void setInternalCameraInput(bool on) { internalCameraInput_ = on; }
		bool internalCameraInput() const { return internalCameraInput_; }

		// When on, each frame renders through the CameraDirector — the
		// highest-priority enabled camera ENTITY (CameraComponent +
		// Transform), with blending on switches — instead of the free-fly
		// camera. The player enables this so the game is seen through its
		// own cameras. Scenes without a camera fall back to the fly cam
		// automatically. The editor leaves this off: its Scene view is the
		// god camera and the Game panel runs its own director separately.
		void setRenderFromSceneCamera(bool on) { renderFromSceneCamera_ = on; }
		bool renderFromSceneCamera() const { return renderFromSceneCamera_; }

		// --- hooks (same contract as the old Renderer API) ---
		using UIDrawFn = std::function<void(float /*deltaTime*/)>;
		void SetUIDraw(UIDrawFn fn) { uiDraw_ = std::move(fn); }

		// What the UI is currently taking away from the engine.
		//
		// `keyboard` and `mouse` gate the built-in fly camera, and a host may
		// set them for reasons that have NOTHING to do with typing (the editor
		// sets keyboard whenever the Scene viewport is not focused). They are
		// therefore useless as a "the user is typing" signal -- reusing
		// `keyboard` for that dropped every gameplay keypress the moment the
		// Game panel took focus, i.e. exactly when the game should have had
		// input. `textInput` is the narrow signal: a text widget has focus, so
		// keystrokes are characters, not commands.
		struct UICapture {
			bool keyboard = false;
			bool mouse = false;
			bool textInput = false;
		};
		using UICaptureFn = std::function<UICapture()>;
		void SetUICaptureProvider(UICaptureFn fn) { captureFn_ = std::move(fn); }

		using OnContextReadyFn = std::function<void()>;
		void SetOnContextReady(OnContextReadyFn fn) { onReady_ = std::move(fn); }

		using UpdateFn = std::function<void(float /*dt*/)>;
		void SetUpdate(UpdateFn fn) { update_ = std::move(fn); }
		// The single "primary" fixed-update slot (gameplay). Overwrites.
		void SetFixedUpdate(UpdateFn fn) { fixedUpdate_ = std::move(fn); }

		// Additional fixed-tick subscribers, run AFTER the primary slot in
		// registration order. Physics needs this: SetFixedUpdate is already
		// owned by the game's gameplay hook, and silently replacing it would
		// delete the game. Ordering matches Unity's: gameplay applies forces
		// on the tick, then the simulation integrates them.
		using TickHandle = uint32_t;
		TickHandle AddFixedUpdate(UpdateFn fn) {
			const TickHandle h = ++nextTickHandle_;
			fixedSubscribers_.push_back({ h, std::move(fn) });
			return h;
		}
		void RemoveFixedUpdate(TickHandle h) {
			fixedSubscribers_.erase(
				std::remove_if(fixedSubscribers_.begin(), fixedSubscribers_.end(),
					[h](const FixedSubscriber& s) { return s.handle == h; }),
				fixedSubscribers_.end());
		}

		// Variable-rate equivalent of AddFixedUpdate, for the same reason:
		// SetUpdate is the game's single primary slot, and scripting taking it
		// would silently delete a game's own per-frame hook.
		//
		// It also exists so both hosts drive per-frame scripts at the SAME
		// point in the loop. The editor previously called ScriptWorld::Update
		// from its UI callback (which runs after the frame's input bookkeeping)
		// while the player called it from SetUpdate (which runs before) -- so
		// a script reading an input edge in OnUpdate behaved differently in
		// Play than in the shipped game. That divergence is precisely what
		// this engine tries hardest to avoid.
		TickHandle AddUpdate(UpdateFn fn) {
			const TickHandle h = ++nextTickHandle_;
			updateSubscribers_.push_back({ h, std::move(fn) });
			return h;
		}
		void RemoveUpdate(TickHandle h) {
			updateSubscribers_.erase(
				std::remove_if(updateSubscribers_.begin(), updateSubscribers_.end(),
					[h](const FixedSubscriber& s) { return s.handle == h; }),
				updateSubscribers_.end());
		}

		// --- gameplay gating (play-in-editor) ---
		// When disabled, the fixed/variable game-update hooks don't tick.
		// The Player leaves this on (default); the editor enables it only
		// while in play mode, so gameplay never mutates the edit-mode scene.
		void setGameplayEnabled(bool on) { gameplayEnabled_ = on; }
		bool gameplayEnabled() const { return gameplayEnabled_; }

		// Whether gameplay hooks currently RECEIVE input. Distinct from
		// gameplayEnabled: the game still simulates, it just reads nothing.
		//
		// The Player leaves this on. The editor follows Game-view focus, so
		// keys typed while the Scene view is focused drive the editor's fly
		// camera instead of the game -- without this, one Space press both
		// jumped the player and did whatever the editor wanted, and there was
		// no way to look around a running scene without playing it.
		void setGameplayInputEnabled(bool on) { gameplayInput_ = on; }
		bool gameplayInputEnabled() const { return gameplayInput_; }

		// --- time control ---
		// Drop any accumulated partial fixed step (play-in-editor calls this
		// on Play so every session's first tick lands at the same time).
		void  resetGameClock() { fixedStep_.reset(); }
		void  setFixedTimestepHz(float hz) { fixedStep_.setStep(1.f / std::max(1.f, hz)); }
		float fixedTimestepHz() const { return 1.f / fixedStep_.step(); }
		float fixedAlpha() const { return fixedStep_.alpha(); }
		void  setTimeScale(float s) { timeScale_ = std::max(0.f, s); }
		float timeScale() const { return timeScale_; }
		void  setPaused(bool p) { paused_ = p; }
		bool  paused() const { return paused_; }

		// --- presentation ---
		void setVSync(bool on);
		bool vsyncEnabled() const { return vsync_; }

		// --- per-frame CPU breakdown (last frame, milliseconds) ---
		// Decomposes the frame so a slow editor frame can be attributed
		// without guessing: sceneRender = 3D submission (GL calls are async,
		// so GPU work usually lands in swap), ui = the whole editor UI
		// callback (panels + ImGui render), swap = SwapBuffers, which absorbs
		// the GPU wait AND any vsync block.
		float frameSceneRenderMs() const { return sceneRenderMs_; }
		float frameUiMs()          const { return uiMs_; }
		float frameSwapMs()        const { return swapMs_; }

		// Route the 3D scene into an offscreen target (the editor's Viewport
		// panel). Null = render straight to the window backbuffer (player).
		// The UI callback always draws to the window backbuffer.
		void SetSceneRenderTarget(RenderTarget* target) { sceneTarget_ = target; }

	private:
		void updateDeltaTime_();
		void handleMouseLook_();
		void bindDefaultInput_();

		static void ScrollThunk_(GLFWwindow* w, double xoff, double yoff);

		// Destruction order matters: window_ is declared first so the GL
		// context outlives renderer_'s resource teardown.
		Window   window_;
		std::unique_ptr<InputMap> input_;
		Camera   camera_{ glm::vec3(0.0f, 0.0f, 3.0f) };
		CameraDirector director_;
		JobSystem jobs_; // constructed on the main thread (captures its id)
		Renderer renderer_;
		bool     internalCameraInput_ = true;

		// timing
		float    deltaTime_ = 0.0f;
		float    lastFrame_ = 0.0f;
		// per-frame CPU breakdown (see frameSceneRenderMs/frameUiMs/frameSwapMs)
		float    sceneRenderMs_ = 0.0f;
		float    uiMs_ = 0.0f;
		float    swapMs_ = 0.0f;
		FixedTimestep fixedStep_{ 1.f / 60.f };
		float    timeScale_ = 1.0f;
		bool     paused_ = false;
		bool     gameplayEnabled_ = true;
		bool     gameplayInput_ = true; // Player default; editor follows Game-view focus
		bool     renderFromSceneCamera_ = false;
		bool     vsync_ = true;

		// hooks
		UIDrawFn       uiDraw_{};
		UICaptureFn    captureFn_{};
		OnContextReadyFn onReady_{};
		bool           readyFired_ = false;
		UpdateFn       update_{};
		UpdateFn       fixedUpdate_{};
		struct FixedSubscriber { TickHandle handle; UpdateFn fn; };
		std::vector<FixedSubscriber> fixedSubscribers_;
		std::vector<FixedSubscriber> updateSubscribers_; // variable rate
		TickHandle     nextTickHandle_ = 0;

		// mouse-look state
		bool   rotating_ = false;
		bool   firstMouse_ = true;
		double lastX_ = 0.0;
		double lastY_ = 0.0;

		RenderTarget* sceneTarget_ = nullptr; // non-owning
	};

	//defined in other projects
	Application* CreateApplication();
}
