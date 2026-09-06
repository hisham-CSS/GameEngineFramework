# Cat Splat Engine — Manual

Verified: 2026-09-02 @ 3d8c055

A C++17 / OpenGL 3.3 game engine with an editor, a standalone player, a headless
asset cooker, and one title — a deterministic rollback fighting game — under
`Games/`.

This manual explains how the engine works and how to build things with it. There
is no per-class API reference: the public surface is one façade header
(`Engine/include/Engine.h`), and the header comments are where the contracts and
the reasons live. Read those.

Four documents outside this manual answer questions it deliberately does not:

| Document | Answers |
|---|---|
| [ROADMAP.md](../ROADMAP.md) | What is built, what is in flight, what is next. **The only place status lives** — no page here lists gaps |
| [NORTHSTAR.md](../NORTHSTAR.md) | What the engine is for, and the test that decides whether each property is done |
| [DETERMINISM.md](../DETERMINISM.md) | Every rule the simulation, the build and the authored data must obey, and what enforces each |
| [adr/](../adr/README.md) | Why a decision was made, frozen the day it was accepted |

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
| [Physics](physics.md) | Rigid bodies, colliders, triggers, collision events, and swapping backends |
| [Scenes and Shipping a Build](scenes-and-shipping.md) | Saving scenes, changing scene at runtime, the startup scene, and packaging a standalone game |
| [The Fighting-Game Core](fighting-core.md) | The integer gameplay kernel, character files and their load assertions, `MatchBuilder` and its loss table, the rollback session seam, and the combo prover panel |

## Systems reference

| Page | Covers |
|---|---|
| [Rendering](rendering.md) | The pass pipeline, shadows, PBR/IBL, sky, transparency, LOD, culling, instancing |
| [Post-processing & Quality Tiers](post-processing.md) | Bloom, ink outline, colour grade, vignette, FXAA, and the Low/Med/High tiers |
| [In-game UI & the 2D layer](ui.md) | `.cxml` markup, `.cstyle` stylesheets, flexbox, events, data binding, hot reload, and `Renderer2D` for 2D games |
| [Assets](assets.md) | Model and texture loading, async requests, import settings, the cooker |
| [The Art Pipeline](art-pipeline.md) | Blender as the authoring tool, the pinned glTF export and its sidecar, the clip contract, the MCP server's place, and the clips gate |
| [Performance](performance.md) | Measuring frames, reading the stats panel, and what actually costs time |

## Design principles

A few decisions shape everything else, and knowing them makes the rest of the
engine predictable:

- **Content lives in the scene file, never in code.** The editor and the player
  both open the same startup scene, so what you author is what ships. Three
  deliberate exceptions: if the startup scene fails to load — or you pick
  **New Scene** — the editor's `createDefaultScene_` seeds a Main Camera (plus a
  ground plane, when `Exported/Model/plane.obj` is available) so a scene is never
  camera-less; `InstallDemoUIContent` registers the sample HUD's starting
  values, its `addScore` action and its `healthTint` converter; and
  `InstallMenuUIContent` registers the main menu's `menu*` actions together with
  the host verbs behind them (load a scene, quit, master volume, quality tier,
  vsync) — because a named C++ function is the one thing markup cannot carry.
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

## Working on the engine itself

These pages describe how to USE the engine. Two more describe how to change it:

- **[Maintenance guide](../MAINTENANCE.md)** -- the change loop (including
  proving a fix by reverting it), how the documentation audit works, the
  invariants that keep biting, and the closed lists that must be updated
  together.
- **[Style guide](../STYLE.md)** -- how code is written here: comments that name
  the bug they prevent, tests named as properties, diagnostics that list what
  exists, and the API shapes the engine relies on.

## What this manual does not say

**What is built and what is not.** That is [ROADMAP.md](../ROADMAP.md), and
keeping a second copy here is how the two came to disagree. Every page describes
what exists today; if something is missing from a page, the roadmap says which
work package adds it and what test will prove it.
