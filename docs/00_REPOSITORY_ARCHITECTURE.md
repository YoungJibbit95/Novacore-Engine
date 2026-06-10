# 00 Repository Architecture

## Purpose

NovaCore is a standalone engine repository providing reusable technology for games built on top of it.

It owns engine infrastructure only and intentionally contains no game-specific balance, content, or rules.

Nemisis (or any future game) depends on NovaCore, but NovaCore must never depend on a game repository.

Dependency direction is strictly one-way:

```text
Game (Nemisis)
        │
        ▼
NovaCore Engine
        │
        ▼
Third-party Libraries
```

Reverse dependencies are forbidden.

---

# Repository Responsibilities

NovaCore owns:

* Core runtime
* Platform abstraction
* Window management
* Input system
* Math library
* ECS
* Renderer
* Vulkan backend
* Physics framework
* Networking primitives
* Dedicated server runtime
* Asset registry
* Asset streaming
* Shader infrastructure
* Build system
* Engine testing
* Engine documentation
* Development tools

NovaCore should remain reusable across multiple projects.

---

# Game Responsibilities

Game repositories own:

* Weapons
* Movement tuning
* Characters
* Maps
* Missions
* Objectives
* UI screens
* Audio content
* Art assets
* Gameplay balancing
* Match rules
* Cosmetic systems

Gameplay should compose engine systems rather than modify them.

---

# Directory Structure

```text
/
├── engine/
│   ├── include/
│   └── src/
├── server/
├── configs/
├── shaders/
├── tests/
├── tools/
├── docs/
└── CMakeLists.txt
```

Each directory should have clearly defined ownership and minimal overlap.

---

# Public API

`engine/include/novacore` exposes the stable engine interface.

Public headers should:

* Minimize implementation details
* Avoid unnecessary dependencies
* Maintain backwards compatibility where practical
* Hide private implementation behind source files

Games should include only public headers.

---

# Private Implementation

`engine/src` contains implementation details.

Private code may change freely provided public interfaces remain stable.

Private headers should not be included by external projects.

---

# Module Boundaries

Subsystems communicate through explicit interfaces.

Example dependencies:

```text
Game
    ↓
Renderer
    ↓
Platform

Game
    ↓
Physics
    ↓
Math

Game
    ↓
Networking
    ↓
Core
```

Renderer should not depend on gameplay.

Networking should not depend on rendering.

Physics should not depend on UI.

---

# Ownership Rules

Each subsystem owns its own resources.

Examples:

* Renderer owns GPU resources.
* Asset system owns asset handles.
* ECS owns entity lifetime.
* Physics owns collision state.
* Networking owns packet serialization.

Cross-subsystem ownership should occur through stable handles or identifiers rather than raw pointers.

---

# Engine Layering

Recommended layering:

```text
Game Logic
      ↓
Engine API
      ↓
Subsystems
      ├── Renderer
      ├── ECS
      ├── Physics
      ├── Networking
      ├── Assets
      └── Platform
      ↓
Operating System / Drivers
```

Higher layers may depend on lower layers.

Lower layers must never depend on higher layers.

---

# Build Targets

Primary targets:

* `novacore_engine`
* `Novacore::Engine`
* `novacore_server`
* `novacore_smoke_tests`

Optional tooling may introduce additional executables without affecting engine consumers.

---

# Configuration Ownership

NovaCore owns:

* Network defaults
* Tick rates
* Server runtime configuration
* Renderer backend configuration
* Platform configuration

Games own:

* Weapon balance
* Movement values
* Match settings
* Gameplay configuration
* Cosmetic data

This separation keeps engine behavior independent of game tuning.

---

# Asset Ownership

NovaCore provides:

* Asset registry
* Manifest parsing
* Streaming
* Residency management
* Runtime handles

Games provide:

* Asset identifiers
* Asset files
* Metadata
* Gameplay categorization
* Content organization

Runtime loading should always pass through NovaCore systems.

---

# Testing Ownership

NovaCore tests verify:

* Engine correctness
* Runtime stability
* Serialization
* ECS behavior
* Renderer initialization
* Asset loading
* Networking primitives

Games should maintain their own gameplay and balance tests.

---

# Coding Principles

Engine code should favor:

* Explicit ownership
* Value semantics
* Deterministic execution
* Data-oriented layouts
* Minimal global state
* Clear subsystem boundaries
* Simplicity over unnecessary abstraction

Abstractions should exist only when they protect real architectural boundaries.

---

# Repository Workflow

Large engine changes should:

1. Complete implementation.
2. Pass smoke tests.
3. Update documentation.
4. Update status documents.
5. Commit cleanly.
6. Push to the NovaCore repository.

Only then should dependent game work begin.

Keeping engine and game evolution synchronized minimizes integration issues and simplifies long-term maintenance.

---

# Acceptance Criteria

Repository architecture is considered stable when:

* NovaCore builds independently.
* Games depend only on the public API.
* No game-specific logic exists inside engine modules.
* Dedicated server builds without renderer dependencies.
* Public interfaces remain clean and well documented.
* Module ownership is explicit and respected.
* Dependency direction remains strictly from game to engine and never the reverse.
