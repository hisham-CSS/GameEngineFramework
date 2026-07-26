#pragma once

//FOR USE BY ENGINE APPLICATIONS
#include "../src/core/Core.h"
#include "../src/core/Application.h"
//#include "../src/core/Main.h"
#include "../src/core/Camera.h"
#include "../src/core/CameraDirector.h"
#include "../src/core/Model.h"
#include "../src/core/Shader.h"
//#include "../src/core/Mesh.h"
#include "../src/core/Entity.h"
#include "../src/core/Renderer.h"
#include "../src/core/Scene.h"
#include "../src/core/Event.h"
#include "../src/core/EventBus.h"
#include "../src/core/ImageIO.h"
#include "../src/core/AssetManager.h"
#include "../src/core/Material.h"
#include "../src/core/SceneSerializer.h"
#include "../src/core/ProjectSettings.h"
#include "../src/core/FixedTimestep.h"
#include "../src/core/InputMap.h"
#include "../src/core/JobSystem.h"
#include "../src/core/RenderTarget.h"
#include "../src/core/GLInit.h"
#include "../src/assets/AssetIndex.h"
#include "../src/assets/AssetValidator.h"
#include "../src/assets/ImportSettings.h"
#include "../src/render/CSMSplits.h"
// physics: backend-agnostic core. The concrete backends (Jolt/PhysX) are
// deliberately NOT exported — callers select one by name through the
// registry, so no consumer ever includes an SDK header.
#include "../src/physics/PhysicsTypes.h"
#include "../src/physics/PhysicsComponents.h"
#include "../src/physics/IPhysicsBackend.h"
#include "../src/physics/PhysicsBackendRegistry.h"
#include "../src/physics/PhysicsWorld.h"
#include "../src/physics/PhysicsInstall.h"
// scripting: same shape as physics. The concrete language backends (Lua, and
// whatever follows) sit behind IScriptBackend and are reached through the
// registry, so no consumer ever includes sol2 or a Lua header.
#include "../src/script/ScriptTypes.h"
#include "../src/script/ScriptComponent.h"
#include "../src/script/IScriptHost.h"
#include "../src/script/IScriptBackend.h"
#include "../src/script/ScriptBackendRegistry.h"
#include "../src/script/ScriptWorld.h"
#include "../src/script/ScriptInstall.h"

// Audio: same seam pattern. miniaudio stays behind the backend .cpp, so no
// consumer includes <miniaudio.h>.
#include "../src/audio/AudioTypes.h"
#include "../src/audio/IAudioBackend.h"
#include "../src/audio/AudioBackendRegistry.h"
#include "../src/audio/AudioComponents.h"
#include "../src/audio/AudioWorld.h"
#include "../src/audio/AudioInstall.h"

// 2D layer + in-game UI. Renderer2D is general-purpose (a 2D game can use it
// directly); the UI tree is one consumer of it.
#include "../src/render2d/Renderer2D.h"
#include "../src/render2d/Font.h"
#include "../src/ui/UIStyle.h"
#include "../src/ui/UIValue.h"
#include "../src/ui/UIDataSource.h"
#include "../src/ui/UIBinding.h"
#include "../src/ui/UITextField.h"
#include "../src/ui/UIElement.h"
#include "../src/ui/UIStyleSheet.h"
#include "../src/ui/UIMarkup.h"
#include "../src/ui/UIInteractionStyler.h"
#include "../src/ui/UIAssetDocument.h"
#include "../src/ui/UIComponent.h"
#include "../src/ui/UIWorld.h"
#include "../src/ui/DemoUIContent.h"

