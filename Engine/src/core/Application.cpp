#include <glad/glad.h>

#include "Application.h"
#include "GLInit.h"
#include "Scene.h"
#include "Shader.h"

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
		// Default fly-camera bindings; apps can rebind via input().
		input_->bindAxisKeys("MoveForward", GLFW_KEY_W, GLFW_KEY_S);
		input_->bindAxisKeys("MoveForward", GLFW_KEY_UP, GLFW_KEY_DOWN);
		input_->bindAxisKeys("MoveRight", GLFW_KEY_D, GLFW_KEY_A);
		input_->bindAxisKeys("MoveRight", GLFW_KEY_RIGHT, GLFW_KEY_LEFT);
		input_->bindGamepadAxis("MoveForward", GLFW_GAMEPAD_AXIS_LEFT_Y, /*inverted=*/true);
		input_->bindGamepadAxis("MoveRight", GLFW_GAMEPAD_AXIS_LEFT_X);
		input_->bindGamepadAxis("LookX", GLFW_GAMEPAD_AXIS_RIGHT_X);
		input_->bindGamepadAxis("LookY", GLFW_GAMEPAD_AXIS_RIGHT_Y, /*inverted=*/true);
		input_->bindKey("Quit", GLFW_KEY_ESCAPE);
		input_->bindGamepadButton("Quit", GLFW_GAMEPAD_BUTTON_BACK);
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
			if (captureFn_) {
				auto caps = captureFn_();
				capK = caps.first; capM = caps.second;
			}

			// Poll every frame so edge states stay coherent; only APPLY the
			// default camera/quit behavior when the UI isn't capturing keys.
			input_->update(window_.getGLFWwindow());
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
			if (gameplayEnabled_) {
				const float gameDt = paused_ ? 0.f : deltaTime_ * timeScale_;
				if (fixedUpdate_) {
					fixedStep_.advance(gameDt, fixedUpdate_);
				}
				if (update_ && gameDt > 0.f) {
					update_(gameDt);
				}
			}

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

			// Editor UI (after 3D draw)
			if (uiDraw_) uiDraw_(deltaTime_);

			window_.swapBuffers();
			window_.pollEvents();
		}

		// Drain the pool BEFORE RunLoop returns: callers destroy their
		// locals (Scene, AssetManager, Shader) right after this, and the
		// derived app's members go next — a worker still decoding against
		// them would be a quit-during-load use-after-free. Finishing here
		// also runs every completion while the GL context and app state
		// are fully alive, so shutdown is deterministic.
		jobs_.waitIdle();
		jobs_.pumpCompletions(1e6f);
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
