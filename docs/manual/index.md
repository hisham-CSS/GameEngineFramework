# Cat Splat Engine — Manual

A C++17 / OpenGL 3.3 game engine with an editor, a standalone player, and a
headless asset cooker.

This manual explains how the engine works and how to build things with it.
For exhaustive per-class detail — every signature, field and overload — see the
generated [API Reference](../api-index.md).

> **Which do I want?**
> The manual answers *"how do I do X?"* and *"why does it work this way?"*.
> The API reference answers *"what exactly does this function take?"*.

## Start here

| Page | Read it when |
|---|---|
| [Getting Started](getting-started.md) | Building the engine, choosing a configuration, running the editor and player |
| [Engine Architecture](architecture.md) | Understanding how the pieces fit and what happens each frame |
| [Using the Editor](editor.md) | Learning the panels, viewport controls, and Play mode |

## Building a game

| Page | Covers |
|---|---|
| [Entities and Components](entities-and-components.md) | The ECS, the transform hierarchy, and every component the engine ships |
| [Writing Gameplay](gameplay-scripting.md) | Update hooks, the fixed tick, input, and reacting to collisions |
| [Lua Scripting](lua-scripting.md) | Attaching scripts to entities, the script API, and adding another language |
| [Physics](physics.md) | Rigid bodies, colliders, triggers, collision events, and swapping backends |
| [Scenes and Shipping a Build](scenes-and-shipping.md) | Saving scenes, the startup scene, and packaging a standalone game |

## Systems reference

| Page | Covers |
|---|---|
| [Rendering](rendering.md) | The pass pipeline, shadows, PBR/IBL, sky, transparency, LOD, culling, instancing |
| [Post-processing & Quality Tiers](post-processing.md) | Bloom, ink outline, colour grade, vignette, FXAA, and the Low/Med/High tiers |
| [Assets](assets.md) | Model and texture loading, async requests, import settings, the cooker |
| [Performance](performance.md) | Measuring frames, reading the stats panel, and what actually costs time |
| [Building on Linux](../BUILDING_LINUX.md) | The Linux build path (the engine targets Windows and Linux) |

## Design principles

A few decisions shape everything else, and knowing them makes the rest of the
engine predictable:

- **Content lives in the scene file, never in code.** The editor and the player
  both open the same startup scene, so what you author is what ships. Nothing
  is hardcoded into the engine or the editor.
- **The fixed tick is for simulation.** Physics and gameplay step at a fixed
  rate with a deterministic accumulator; rendering runs per frame. They are
  deliberately separate.
- **Third-party libraries stay behind seams.** Physics is the clearest case:
  no Jolt or PhysX type appears in any engine header, so a backend can be
  swapped at runtime and a new library added without touching call sites.
- **The editor and the player share their engine-side setup.** Anything
  installed for one is installed for the other, so "works in Play" and "works
  in the shipped game" cannot drift apart.

## Conventions in these pages

- Source files are referenced by repo-relative path, e.g.
  `Engine/src/core/Scene.h`, so you can jump straight to the code.
- **Gotcha** notes flag behaviour that has bitten someone before. They are
  worth reading; most of them were expensive to discover.
- Code samples use `using namespace MyCoreEngine;` for brevity.

## Status

The engine is under active development and this manual describes what exists
today, not what is planned. The roadmap and audit live in
[ENGINE_AUDIT_2026-07.md](../ENGINE_AUDIT_2026-07.md).
