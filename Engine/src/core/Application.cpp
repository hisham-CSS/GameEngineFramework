#include <glad/glad.h>

#include "Application.h"
#include "GLInit.h"
#include "Scene.h"
#include "Shader.h"
#include "WindowIcon.h"

#include <chrono>
#include <stdexcept>

namespace MyCoreEngine
{
	Application::Application(int width, int height, const char* title)
		: window_(width, height, title)
		, input_(std::make_unique<InputMap>())
	{
		bindDefaultInput_();
	}

	Application::~Application() = default;

	void Application::bindDefaultInput_()
	{
		// Delegated so the default set is testable without a window; apps can
		// rebind or clear any of it via input().
		BindDefaultActions(*input_);
	}

	void Application::installInput(std::unique_ptr<InputMap> map)
	{
		if (!map) return;
		input_ = std::move(map);
		bindDefaultInput_();
	}

	void Application::InitGL()
	{
		if (!EnsureGLADLoaded()) {
			throw std::runtime_error("Failed to initialize GLAD");
		}
		glfwSwapInterval(vsync_ ? 1 : 0);

		int w = 0, h = 0;
		window_.getFramebufferSize(w, h);
		renderer_.Setup(w, h);

		glfwSetWindowUserPointer(window_.getGLFWwindow(), this);
		glfwSetScrollCallback(window_.getGLFWwindow(), &Application::ScrollThunk_);

		// Window-manager icon, staged with the other runtime assets. On
		// Windows the exe's icon resource already covers this; on Linux this
		// call is the only mechanism. Best-effort: missing file = no icon.
		TrySetWindowIconFromFile(window_.getGLFWwindow(), "Exported/Icon/icon.png");
		// (no framebuffer-size callback: RenderFrame tracks its output size
		// every frame and resizes the HDR pipeline itself)

		if (!readyFired_ && onReady_) {
			onReady_(); // apps init ImGui / create GL objects here
			readyFired_ = true;
		}
	}

	void Application::setVSync(bool on)
	{
		vsync_ = on;
		// context is current on the main thread for the app's lifetime
		glfwSwapInterval(on ? 1 : 0);
	}

	void Application::updateDeltaTime_()
	{
		float currentFrame = static_cast<float>(glfwGetTime());
		// Clamp so a stall (debugger break, window drag, load hitch) doesn't
		// produce a giant step that teleports the camera / future physics.
		deltaTime_ = glm::clamp(currentFrame - lastFrame_, 0.0f, 0.1f);
		lastFrame_ = currentFrame;
	}

	void Application::RunLoop(Scene& scene, Shader& shader)
	{
		while (!window_.shouldClose()) {
			updateDeltaTime_();

			bool capK = false, capM = false;
			bool typing = false;
			if (captureFn_) {
				const UICapture caps = captureFn_();
				capK = caps.keyboard; capM = caps.mouse; typing = caps.textInput;
			}

			// Poll every frame so edge states stay coherent; only APPLY the
			// default camera/quit behavior when the UI isn't capturing keys.
			input_->update(window_.getGLFWwindow());
			// Polling stays unconditional so edge state remains coherent, but
			// a press made while a TEXT WIDGET has focus must not reach
			// gameplay: "Jump" is bound to Space, so typing a space into the
			// entity-name or script-path field during Play would otherwise
			// launch the player.
			//
			// Deliberately NOT capK -- see UICapture. capK is also set when
			// the host simply is not pointing at its 3D viewport, which is
			// true whenever the Game panel has focus, so clearing on it threw
			// away every press at exactly the moment the game deserved one.
			if (typing) input_->clearPressLatches();
			if (!capK) {
				if (input_->wasPressed("Quit")) {
					glfwSetWindowShouldClose(window_.getGLFWwindow(), true);
				}
				const float move = camera_.MovementSpeed * deltaTime_;
				camera_.Position += camera_.Front * (input_->axis("MoveForward") * move);
				camera_.Position += camera_.Right * (input_->axis("MoveRight") * move);

				const float lookX = input_->axis("LookX");
				const float lookY = input_->axis("LookY");
				if (lookX != 0.f || lookY != 0.f) {
					// ~120 deg/s at full stick deflection (sensitivity 0.1 applies inside)
					const float padLook = 1200.f * deltaTime_;
					camera_.ProcessMouseMovement(lookX * padLook, lookY * padLook);
				}
			}
			if (!capM && internalCameraInput_) handleMouseLook_();

			// Finalize completed background work (asset decodes and the
			// like) on the main thread with the GL context current. The
			// budget keeps a burst of finished jobs from hitching a frame.
			jobs_.pumpCompletions(2.0f);

			// Game update: fixed steps (simulation) then per-frame variable step.
			// Camera/editor input above deliberately ignores pause/time scale.
			// Skipped entirely while gameplay is gated off (editor edit mode).
			int   fixedSteps = 0;
			bool  hasFixedConsumers = false;
			float gameDt = 0.f;
			// Scoped to the gameplay hooks ONLY: the editor's fly-camera block
			// above has already run off the same map, so the Scene view keeps
			// working while the game receives nothing.
			input_->setSuppressed(!gameplayInput_);
			if (gameplayEnabled_) {
				gameDt = paused_ ? 0.f : deltaTime_ * timeScale_;
				hasFixedConsumers = fixedUpdate_ || !fixedSubscribers_.empty();
				if (hasFixedConsumers) {
					// One accumulator drives BOTH the primary gameplay slot
					// and every subscriber (physics), so they always see the
					// same step count and never drift apart.
					fixedSteps = fixedStep_.advance(gameDt, [this](float fixedDt) {
						// Each tick is its own consumption phase: every
						// consumer within it observes the same input edges.
						input_->beginInputPhase();
						if (fixedUpdate_) fixedUpdate_(fixedDt);
						for (auto& s : fixedSubscribers_) {
							if (s.fn) s.fn(fixedDt);
						}
					});
				}
				if (gameDt > 0.f) {
					input_->beginInputPhase(); // variable-rate phase
					if (update_) update_(gameDt);
					for (auto& s : updateSubscribers_) {
						if (s.fn) s.fn(gameDt);
					}
				}
			}

			// A press latch exists for exactly one situation: a frame that
			// SHOULD have run a fixed tick but ran none, so a fixed-tick
			// consumer has not had its chance to see the press yet. In every
			// other case the latch is dropped, which keeps a press from being
			// replayed later.
			//
			// gameDt > 0 is part of "should have": while paused (or at
			// timeScale 0) NO tick is ever owed, so a latch would sit
			// indefinitely and fire the moment the user resumes -- a jump
			// from a keypress made minutes earlier, during the pause.
			// Edit mode is the same case via gameplayEnabled_.
			input_->setSuppressed(false); // editor/UI reads are never suppressed

			const bool awaitingTick = gameplayEnabled_ && gameplayInput_
			                       && hasFixedConsumers
			                       && gameDt > 0.f && fixedSteps == 0;
			// gameplayInput_ is part of "should have" for the same reason as
			// pause: with input off, nothing will ever consume the latch, so
			// holding it would fire a jump the moment the Game view regains
			// focus -- from a key pressed while the user was in the Scene view.
			if (!awaitingTick) input_->clearPressLatches();

			scene.UpdateTransforms();

			// After UpdateTransforms so the camera entities' world matrices
			// are current — the view tracks gameplay with no frame lag. The
			// director picks the highest-priority enabled camera and blends
			// on switches. When no camera is usable the fly cam takes over —
			// restore its default lens first: the director wrote the LAST
			// scene camera's clip planes into camera_, and e.g. near=2/
			// far=60 would corrupt the fallback view for the whole session.
			if (renderFromSceneCamera_) {
				if (!director_.Update(scene.registry, deltaTime_, camera_)) {
					camera_.NearClip = Camera::NEAR_DEFAULT;
					camera_.FarClip = Camera::FAR_DEFAULT;
				}
			}

			// per-frame CPU breakdown: attribute a slow frame to 3D
			// submission vs editor UI vs present/vsync without guessing
			using perfClock_ = std::chrono::steady_clock;
			const auto tRender0_ = perfClock_::now();

			int fbw = 0, fbh = 0;
			window_.getFramebufferSize(fbw, fbh);
			if (sceneTarget_ && sceneTarget_->fbo() && sceneTarget_->width() > 0) {
				// scene -> offscreen target (editor viewport)...
				renderer_.RenderFrame(scene, shader, camera_,
					sceneTarget_->width(), sceneTarget_->height(), deltaTime_,
					sceneTarget_->fbo());
				// ...UI -> window backbuffer
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glViewport(0, 0, fbw, fbh);
				glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			}
			else if (fbw > 0 && fbh > 0) {
				renderer_.RenderFrame(scene, shader, camera_, fbw, fbh, deltaTime_);
			}

			const auto tRender1_ = perfClock_::now();

			// Editor UI (after 3D draw)
			if (uiDraw_) uiDraw_(deltaTime_);

			const auto tUi1_ = perfClock_::now();
			window_.swapBuffers();
			const auto tSwap1_ = perfClock_::now();

			using ms_ = std::chrono::duration<float, std::milli>;
			sceneRenderMs_ = ms_(tRender1_ - tRender0_).count();
			uiMs_ = ms_(tUi1_ - tRender1_).count();
			swapMs_ = ms_(tSwap1_ - tUi1_).count();

			window_.pollEvents();
		}

		// Drain the pool BEFORE RunLoop returns: callers destroy their
		// locals (Scene, AssetManager, Shader) right after this, and the
		// derived app's members go next — a worker still decoding against
		// them would be a quit-during-load use-after-free. Finishing here
		// also runs every completion while the GL context and app state
		// are fully alive, so shutdown is deterministic.
		//
		// LOOPED because completions may chain-submit (the AssetManager's
		// decode queue launches the next load from a completion): after
		// waitIdle every finished job's completion is visible, so a pump
		// that runs ZERO completions proves nothing was submitted since —
		// the pool is quiescent. A single pass would return with workers
		// still running chained decodes.
		do {
			jobs_.waitIdle();
		} while (jobs_.pumpCompletions(1e6f) > 0);
	}

	void Application::handleMouseLook_()
	{
		GLFWwindow* win = window_.getGLFWwindow();
		if (!win) return;

		const int rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);

		if (rmb == GLFW_PRESS) {
			if (!rotating_) {
				rotating_ = true;
				firstMouse_ = true;
				glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			}

			double xpos, ypos;
			glfwGetCursorPos(win, &xpos, &ypos);

			if (firstMouse_) {
				lastX_ = xpos; lastY_ = ypos;
				firstMouse_ = false;
				return;
			}

			// Note: yaw increases with +x, pitch increases with -y
			float xoffset = static_cast<float>(xpos - lastX_);
			float yoffset = static_cast<float>(lastY_ - ypos);

			lastX_ = xpos; lastY_ = ypos;

			camera_.ProcessMouseMovement(xoffset, yoffset, true);
		}
		else {
			if (rotating_) {
				rotating_ = false;
				glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			}
			firstMouse_ = true; // reset when not rotating
		}
	}

	void Application::ScrollThunk_(GLFWwindow* w, double /*xoff*/, double yoff)
	{
		if (auto* self = static_cast<Application*>(glfwGetWindowUserPointer(w))) {
			// the editor zooms via ImGui wheel input instead (viewport-aware)
			if (self->internalCameraInput_) {
				self->camera_.ProcessMouseScroll(static_cast<float>(yoff));
			}
		}
	}
}
