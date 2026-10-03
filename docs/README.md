# Documentation

## Getting started

- [Desktop 2D synthetic lab](../examples/desktop-2d/README.md): Windows desktop
  fixture, shared Lua walkthrough, both variants and save/restore acceptance.
- [Desktop packages](desktop-packaging.md): shaders, pinned dependency notices,
  and launchers independent of the working directory.
- [First project](../examples/first-project/README.md): authored sample project,
  its assets, and deterministic headless run.
- [Project commands](commands.md): build targets, CLI commands, JSON results,
  and packaging.
- [Project and scene format](project-format.md): version 1 manifest, scene,
  asset, and validation contract.
- [Mobile builds](../mobile/README.md): Android and iOS prerequisites, build
  steps, and verified runtime scope.

## Engine interfaces

- [ECS design](../src/ecs/README.md): EnTT backend, entity/component storage,
  views, and job behavior.
- [ECS and legacy utility cleanup](ecs-modernization.md): removed unused APIs,
  preserved features, current ECS usage, and validation checks.
- [Lua scripting](scripting.md): script lifecycle, host API, and project-script
  restrictions.
- [Vulkan rendering](vulkan.md): desktop rendering backend, frame submission,
  and Vulkan build/smoke requirements.
- [Desktop 2D rendering](desktop-2d-rendering.md): RGBA sprites, camera scaling,
  UTF-8 font atlases and Vulkan readback checks.
- [2D gameplay](gameplay2d.md): deterministic sweeps, triggers, placement and flock helpers.
- [Desktop input/UI](desktop-input-ui.md): action mapping, device/focus transitions,
  modal capture, scrolling and pointer selection.
- [Desktop audio and persistence](desktop-services.md): PCM playback/mixing,
  durable versioned saves, recovery and separate settings.
- [MCP adapter](mcp.md): read-only stdio protocol, supported tools, and limits.

## Project direction

- [Human and AI tooling roadmap](human-ai-tooling-roadmap.md): implementation
  phases, current capabilities, and remaining scope.
