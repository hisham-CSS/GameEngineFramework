#pragma once
#include "Core.h"

#include <functional>
#include <memory>
#include <utility>

#include "Window.h"
#include "InputMap.h"
#include "Camera.h"
#include "FixedTimestep.h"
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

		// --- hooks (same contract as the old Renderer API) ---
		using UIDrawFn = std::function<void(float /*deltaTime*/)>;
		void SetUIDraw(UIDrawFn fn) { uiDraw_ = std::move(fn); }

		using UICaptureFn = std::function<std::pair<bool, bool>()>;
		void SetUICaptureProvider(UICaptureFn fn) { captureFn_ = std::move(fn); }

		using OnContextReadyFn = std::function<void()>;
		void SetOnContextReady(OnContextReadyFn fn) { onReady_ = std::move(fn); }

		using UpdateFn = std::function<void(float /*dt*/)>;
		void SetUpdate(UpdateFn fn) { update_ = std::move(fn); }
		void SetFixedUpdate(UpdateFn fn) { fixedUpdate_ = std::move(fn); }

		// --- gameplay gating (play-in-editor) ---
		// When disabled, the fixed/variable game-update hooks don't tick.
		// The Player leaves this on (default); the editor enables it only
		// while in play mode, so gameplay never mutates the edit-mode scene.
		void setGameplayEnabled(bool on) { gameplayEnabled_ = on; }
		bool gameplayEnabled() const { return gameplayEnabled_; }

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
		Renderer renderer_;
		bool     internalCameraInput_ = true;

		// timing
		float    deltaTime_ = 0.0f;
		float    lastFrame_ = 0.0f;
		FixedTimestep fixedStep_{ 1.f / 60.f };
		float    timeScale_ = 1.0f;
		bool     paused_ = false;
		bool     gameplayEnabled_ = true;
		bool     vsync_ = true;

		// hooks
		UIDrawFn       uiDraw_{};
		UICaptureFn    captureFn_{};
		OnContextReadyFn onReady_{};
		bool           readyFired_ = false;
		UpdateFn       update_{};
		UpdateFn       fixedUpdate_{};

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
