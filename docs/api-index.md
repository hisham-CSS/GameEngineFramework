# Cat Splat Engine — API Reference

This is the generated reference for the engine's public C++ API. It is
produced from the headers themselves, so it always matches the code you are
building against.

Looking for concepts and how-to guides instead? Start with the
[Manual](manual/index.md).

## Where to start

Everything a game or tool needs is reachable through the single façade header:

```cpp
#include "Engine.h"   // pulls in the whole public API
using namespace MyCoreEngine;
```

The API divides into a few areas:

| Area | Start here | What it covers |
|---|---|---|
| Application & main loop | `MyCoreEngine::Application` | Window, input, camera, timing, the frame loop, update hooks |
| Scene & entities | `MyCoreEngine::Scene`, `Entity` | The EnTT registry, draw submission, culling, LOD |
| Components | `Transform`, `ModelComponent`, `CameraComponent`, `LightComponent` (global); `MyCoreEngine::{RigidBody, ScriptComponent, AudioSourceComponent}` | The data you attach to entities |
| Rendering | `MyCoreEngine::Renderer` | Render passes, shadows, PBR/IBL, post-processing, quality tiers, render targets |
| Physics | `MyCoreEngine::PhysicsWorld`, `IPhysicsBackend` | Bodies, colliders, collision events, backend selection |
| Scripting | `MyCoreEngine::ScriptWorld`, `IScriptBackend`, `ScriptComponent` | Sandboxed Lua scripts on entities, backend selection |
| Audio | `MyCoreEngine::AudioWorld`, `IAudioBackend`, `AudioSourceComponent` | 3D/2D sounds, listener, backend selection |
| In-game UI & the 2D layer | `MyCoreEngine::UIWorld`, `ui::UIDocument`, `ui::UIElement`, `UIDocumentComponent`, `Renderer2D` | `.cxml` markup, `.cstyle` stylesheets, flexbox layout, events, two-way data binding, hot reload, and the general-purpose 2D renderer |
| Assets | `MyCoreEngine::AssetManager`, `Model` | Loading models and textures, async requests |
| Serialization | `MyCoreEngine::SceneSerializer` | Saving and loading scenes |

## Conventions

- Everything public lives in namespace `MyCoreEngine`. The **core** ECS
  components are the exception — they sit at global scope so ECS code reads
  cleanly: `Name`, `Parent`, `Transform`, `ModelComponent`, `MaterialOverrides`,
  `CameraComponent` and `LightComponent` in `Components.h`, plus the `NoShadow`
  tag in `Scene.h`. Newer subsystem components (`RigidBody`, the four colliders,
  `ScriptComponent`, `AudioSourceComponent`, `AudioListenerComponent`,
  `UIDocumentComponent`) stay in `MyCoreEngine`.
- `ENGINE_API` marks the DLL boundary. It is stripped from these pages.
- Types whose names end in `Desc` are plain parameter structs, filled in and
  passed by value.
- Physics `BodyId` and similar handles are **opaque**: meaningful only to the
  system that issued them, and never safe to persist to disk.

## A note on the backends

The concrete physics backends (Jolt, PhysX, Simple) each have a header under
`Engine/src/physics/backends/`, so Doxygen documents them — but **never program
against one**. Nothing outside a backend's own `.cpp` may see an SDK type: every
Jolt and PhysX type stays behind a pimpl, and that is what lets a backend be
swapped at runtime and a new physics library be added without touching a single
call site. Program against `IPhysicsBackend` and select an implementation by
name through `PhysicsBackendRegistry`.
