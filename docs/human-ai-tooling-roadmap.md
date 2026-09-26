# Human and AI Tooling Roadmap

This document describes a practical path from the current engine prototype to a
project that people can author, inspect, and iterate on, and that automated
tools can operate safely. It is an implementation guide, not a description of
features already shipped. Code examples, command names, file names, and schemas
labelled **proposed** are design examples only. No scene format, project
manifest, JSON protocol, editor, or external UI/serialization dependency is
currently selected.

The central recommendation is to build one explicit, versioned project and
scene contract, then expose it through deterministic headless commands. Human
interfaces and AI integrations should call the same validated operations.
This gives each interface the same rules for identity, references, edits,
diagnostics, and saves. Start with an inspectable scene and validator; do not
begin by coupling an editor or an agent directly to live ECS storage.

## Current state

The repository is a C++23 engine prototype. The root [README](../README.md)
documents submodule checkout, GNU Make builds, headless execution, optional
Vulkan setup, and platform caveats. Current entry points include:

- `make`, `make test`, and `make run`; optional graphics commands use
  `VULKAN=1` and the Vulkan SDK.
- The executable accepts `--headless`, `--ticks N`, `--script file.lua`, and
  `--shaders directory`. It does not yet load a project or scene. The sample
  script path and procedural shader path are defaults in `src/main.cpp`.
- `assets/scripts/moving_entity.lua` creates one entity, moves its position,
  then destroys it during script teardown. The only other project assets are
  the triangle vertex and fragment shaders under `assets/shaders/`.
- The Lua `engine` table currently offers logging, entity creation/liveness/
  destruction, and position get/set. Script files return lifecycle tables with
  `on_create`, `on_update`, and `on_destroy`; see [Lua scripting](scripting.md).
- The engine uses EnTT behind its ECS APIs and has generation-aware runtime
  entity handles. Programmer-facing ECS setup, queries, and job declarations
  are C++ APIs described in [ECS design](../src/ecs/README.md).
- The Makefile invokes `glslc` to compile GLSL into SPIR-V; the Vulkan backend
  loads that SPIR-V, draws the sample triangle, and uploads declared uniform
  buffers. [Vulkan rendering](vulkan.md) documents current restrictions: no
  mesh or vertex-layout API, no depth testing, no culling, no texture/sampler
  interface, no material system, and no shader reflection. The current draw
  path is procedural (`gl_VertexIndex`).
- Keyboard and mouse listener types exist in `src/interaction/`, but the sample
  executable does not wire an authored input map or script-facing controls.
- The Makefile test targets cover ECS behavior, Lua lifecycle/error handling,
  and engine-to-Lua-to-ECS integration. The Vulkan smoke test is a real-GPU
  check and is necessarily dependent on a usable Vulkan environment.

There is currently no project manifest, persistent scene serialization,
asset-import/indexing pipeline, editor, structured command protocol, MCP
adapter, or packaged game workflow in the project-owned source/docs/assets.
These are proposed work, not hidden capabilities of EnTT, Lua, or Vulkan.

## Recommended order

Build the smallest shared authoring foundation first. Each phase below has a
bounded outcome and a gate for deciding whether to continue. Paths introduced
below are suggestions; create them only when a phase is implemented.

### 1. Define a project and scene contract

**Outcome:** a checked-in sample project can name a startup scene, scripts, and
assets without relying on paths relative to the engine repository or a
particular build directory.

Proposed locations might be `docs/project-format.md`,
`src/project/`, `src/scene/`, and `examples/first-project/`. Choose a stable
project root and a manifest format before adding editor-specific data. Keep
source assets and authored files in the project; put generated shader binaries,
imported caches, temporary thumbnails, and build output in derived directories.
Derived data should be reproducible or safely discardable.

A project-relative asset reference should resolve from a project asset ID or
project-relative URI, rather than an absolute workstation path. A filename can
be useful metadata, but should not be the sole durable identity if users can
rename or move assets. Define how duplicate IDs, missing files, case
sensitivity, path traversal, and references outside the project root are
handled. Keep the choice of manifest/scene encoding open until its parser,
error reporting, and tooling support have been evaluated; this roadmap does
not select a third-party library.

The scene contract needs an explicit schema version, persistent scene and
object IDs, component type names, component properties, and references to
scripts/assets. It must not serialize raw EnTT handles, packed entity values,
storage indices, or memory addresses as persistent IDs. Runtime handles are
implementation details with generation/lifetime semantics. A persistent ID
resolves to a runtime handle when a scene instance is loaded; destroying and
reloading the scene may create different runtime handles while the authored ID
stays the same. If JSON is selected, represent external IDs as strings: JSON
number consumers can disagree about large integer precision, and string IDs
make the authored identifier visibly distinct from the current Lua API's
opaque integer runtime entity handle.

Example **proposed only** scene data:

```yaml
schema: 1
scene_id: "scene:sample"
entities:
  - id: "object:player"
    name: "Player"
    components:
      Transform:
        position: [0.0, 0.0, 0.0]
      Script:
        asset: "asset:moving-player"
```

This illustrates the concepts, not a committed YAML choice or supported parser.
Before freezing a format, decide whether component data is nested or flat,
whether defaults are omitted or written, how floats and vectors are encoded,
how ordering affects diffs, and how human edits preserve comments/formatting
(if that is a requirement). Establish a migration policy: reject newer
unsupported schema versions; migrate older versions through explicit,
testable transformations; do not silently reinterpret fields.

Define spatial conventions at the same time: world and local coordinate
handedness, distance units, rotation representation/order, parent transform
composition, and the relationship between authored coordinates and Vulkan
clip-space/Y/depth conventions. The triangle demo's current shader transform is
not a project-wide coordinate contract. Put explicit defaults in the sample
format rather than relying on renderer assumptions.

Use an explicit component/property registration layer as the common description
of authored data. It should provide a stable serialized type name, schema
version, property name/type, default, validation, and serialization hooks.
Where meaningful, the same registration should drive scene loading/saving,
CLI inspection and edits, an editor inspector, and a restricted Lua API. Avoid
trying to reflect arbitrary C++ object layouts or exposing every internal
component by default. Keep runtime-only state and derived caches out of the
authored schema unless there is a clear persistence requirement.

For scripts, persist the script asset reference and declared configuration
properties. Do not attempt to serialize an arbitrary Lua VM, closure, stack, or
local variable as scene state. If a game needs save-game state, define a
separate versioned state table or component contract and let script callbacks
read/write that supported data explicitly.

Validation should be separate from mutation. It should detect malformed
documents, duplicate persistent IDs, unknown component/property names,
unsupported versions, missing assets/scripts, unresolved entity references,
invalid enum values, non-finite numbers, out-of-range values, and invalid
reference cycles where the domain forbids them. Define whether unknown fields
are errors or warnings; never silently drop them on save. Diagnostics should
include a stable code, severity, source path, field path, and readable message.

**Done gate:** one real example project parses and validates offscreen; valid
round-trip fixtures preserve authored meaning; invalid fixtures report useful
field-local diagnostics; all referenced assets are resolved relative to the
project root; no persistent ID depends on EnTT allocation order.

### 2. Add a shared headless command and validation API

**Outcome:** a human or automation client can ask the engine to inspect and
validate a project without opening a window, then run a bounded scene.

First implement ordinary C++ operations such as `loadProject`,
`validateScene`, `inspectScene`, and `runScene` behind one command/service
layer. The exact names are proposed. The CLI should call that layer; a future
editor or MCP adapter should call the same layer rather than shelling out to
private implementation details or mutating EnTT directly.

Suggested first commands (all **proposed**, none currently shipped):

```text
engine project validate examples/first-project/project.yml --format json
engine scene inspect examples/first-project/scenes/main.yml --format json
engine run examples/first-project/project.yml --headless --ticks 120 --format json
```

Machine mode needs a contract, not merely JSON-looking logs. Reserve stdout for
one result document (or a documented stream protocol); send human logs and
progress to stderr. Define stable exit statuses and stable diagnostic codes,
including a per-error field path. Keep data and schema versions explicit.
Example response shape, **proposed only**:

```json
{
  "schema": 1,
  "ok": false,
  "diagnostics": [
    {
      "code": "scene.asset.missing",
      "severity": "error",
      "file": "scenes/main.yml",
      "path": "entities[0].components.Script.asset",
      "message": "Asset 'asset:moving-player' was not found"
    }
  ]
}
```

Keep error codes stable across wording changes, and document which failures
make a command nonzero. Do not parse human log messages as an automation API.
For future edit commands, accept explicit operations and validate them against
the component/property schema. Include an expected scene revision or content
hash so a stale client cannot unknowingly overwrite newer edits. Use request
IDs/idempotency keys where retries might repeat an operation. Idempotency
does not make side effects reversible: script callbacks, file writes, or
external actions need separate semantics and must not be advertised as safely
retryable without evidence.

**Done gate:** validation and inspection work on a machine with no display and
no Vulkan SDK; stdout parses as the documented result format; stderr remains
human-readable; a controlled fixture with fixed delta, seed, and input produces
the same selected normalized engine-state result across bounded headless runs.
This early guarantee covers that fixture and result only, not arbitrary Lua,
all logs, cross-platform floating-point results, or GPU pixels. Exit codes and
diagnostic codes have fixtures.

### 3. Make the sample an interactive content slice

**Outcome:** a new user can open a sample project, understand its scene, make a
small change, and see a meaningful result.

Use the scene contract for the sample rather than hardcoding entity creation
and asset paths in `src/main.cpp`. Add authored input actions and a small set
of script-facing operations through registered, validated properties and
commands. Keep the initial capability small: predictable keyboard actions,
entity transform, and a visible result. Existing keyboard/mouse classes are
low-level listeners, not yet a project input-map or Lua action API. Input
should be sampled into a stable per-tick snapshot or queued as ordered events;
define focus loss, key repeat, and pressed/held/released behavior before
scripts depend on them. Do not let scripts poll GLFW directly.

The next graphics increment should be a mesh/vertex-buffer path, then camera
and depth, then basic material and texture resources. Document the supported
formats and limits as each exists. Keep the procedural triangle as a renderer
smoke fixture; do not present it as an asset-driven renderer. Each increment
should have one small sample and a GPU-independent validation path for authored
references and CPU-side data. GPU behavior remains covered by the existing
smoke path where the environment supports it.

CPU component/property registration does not define a GPU buffer layout.
Vulkan material uploads must use explicit GPU-side packing and binding metadata
that agrees with the shader interface. The current shader API explicitly
requires callers to match SPIR-V descriptor bindings and byte layout, including
std140 padding and matrix order. Never upload arbitrary C++ object bytes as
material data: components can contain padding, pointers, or non-GPU fields.

Script properties and component access should use an allowlisted, typed bridge.
Do not expose generic reflection that lets untrusted or generated scripts
write arbitrary C++ memory. Keep script lifecycle semantics explicit:
`LuaScriptSystem` rejects nested lifecycle operations from a native callback
while Lua executes, and shutdown marks the runtime unavailable before close
finalizers run. Any future reload feature should parse/compile/validate a
replacement first, keep the last-known-good script active on failure, and
define how script-owned entities/state are migrated or cleaned up. A reload is
not automatically safe merely because Lua can load another file.

**Done gate:** the sample scene is authored data; it runs both headlessly and,
when Vulkan is available, visibly; one input action affects the simulation;
errors in a referenced script or asset are reported before a confusing blank
window; the sample explains how a person can edit and rerun it.

### 4. Build a minimal editor on the same operations

**Outcome:** a person can inspect and make common scene edits while the editor
remains a client of the project/scene contract.

Start with project open, scene hierarchy, selection, typed inspector, asset
reference picker, validation/log panel, save/reload, and play/stop. Add an
engine viewport only when the rendering path can display authored scene
content. Avoid an editor that writes raw EnTT storage or duplicates scene
validation. Keep project state and runtime state distinct: play mode should
run an isolated scene instance; stopping play should discard runtime changes
unless a deliberate apply/revert operation is chosen.

Represent edits as validated commands. This gives later undo/redo a natural
history of intent, while scene persistence remains the source of authored
truth. Undo/redo needs explicit semantics for entity deletion, references,
asset changes, and commands whose effects cannot be reversed. Do not claim
all edits are transactional if scripts have already run side effects. Keep
undo history at the editor/project layer, not in ad hoc UI widget callbacks.

Use a single authority for scene mutation. The current [Engine loop](../src/core/engine.cpp)
serializes polling, Lua updates, simulation, and render coordination on its
caller thread, waiting for scheduled jobs between phases. README guidance says
the caller must own the window thread and external code should not mutate ECS
while running. Proposed editor/CLI edits should therefore be validated off to
the side and applied at a known owner-thread frame/tick boundary. Use a
structural command buffer or queued scene command path where work can originate
off-thread; never hand UI or AI clients a live registry pointer. Renderer and
window operations must also stay on their owning thread.

Scene load should follow the same discipline: parse and validate into a
temporary representation, resolve IDs and assets, then commit at a safe
boundary. If commit can fail, preserve the existing scene or define a clear
rollback strategy. Saving should write a temporary file and, where the
platform supports it, replace the destination atomically on the same
filesystem after serialization succeeds. On serialization or replace failure,
retain the original destination. A stream flush alone is not a universal
power-loss durability guarantee; stronger durability depends on platform file
and directory synchronization semantics. Test serializer and replace failures
with injection rather than claiming crash-proof saves. Script `on_create`
callbacks and other external effects complicate rollback; defer side effects
until commit where possible and report effects that cannot be undone.

**Done gate:** editor edits round-trip through the same scene API and CLI;
stale revisions are rejected; undo/redo tests cover supported operations;
play/stop behavior is explicit; no editor code mutates ECS storage from the
wrong thread.

### 5. Improve iteration, packaging, and integration

**Outcome:** projects are easy to bootstrap, diagnose, share, and run on the
platforms the engine claims to support.

After the authoring contract is stable, add project initialization/template
commands, asset indexing/import with source-versus-cache separation, dependency
tracking, useful rebuild diagnostics, script/shader validation, and targeted
hot reload. Then define an export/package format, runtime asset lookup, and a
small build artifact that does not require a developer checkout. Keep generated
outputs out of versioned source unless a workflow specifically needs them.

Improve diagnostics at their source. A missing compiler, SDK, shader, script,
asset, or validation layer should report the failed prerequisite and relevant
path/command. Preserve the current README caveat that Windows is the active
environment and Linux is unvalidated here; macOS and Wayland are not configured.
Do not claim portable packaging until CI actually builds and runs it on each
declared platform.

For regression coverage, build deterministic headless fixtures around a fixed
delta, seed, input-event sequence, stable iteration/order policy, and explicit
tick count. Compare authored scene output or stable state snapshots, not raw
EnTT handles. A deterministic promise must name what is stable (for example,
same-platform simulation state) and what is not (GPU pixels across drivers or
floating-point results across architectures). Keep GPU smoke and validation
layer checks as separately gated checks because they require real graphics
support. Current Vulkan smoke also reports when a surface cannot support its
optional pixel readback; that skip is not evidence of a verified image.

**Done gate:** a clean checkout can initialize, validate, run, and package the
sample using documented commands; declared CI platforms pass their actual
checks; unsupported paths fail with useful diagnostics; headless determinism
has a scoped, tested guarantee.

### 6. Add AI adapters after the shared API is useful

An MCP adapter or other agent integration is a later transport layer, not the
engine's project model. It should call the same inspect/validate/edit/run
operations and return structured diagnostics and bounded outputs. Begin with
read-only project/scene inspection and validation. Add edit operations only
after schema validation, revision checks, command boundaries, and undo/audit
behavior exist. Scope paths to the opened project, require explicit operations,
and avoid raw component-memory access as the engine's integration contract.
AI edits should use the same semantic operations, preconditions, and revision
checks as editor edits; they should not depend on patching private C++ source or
writing registry internals. A project `run` command executes that project's
Lua code. Structured scene validation does not sandbox arbitrary project code,
so the command contract and product expectations should distinguish static
validation from executing scripts.

The same command API can serve scripts, a future GUI, CI, and an AI bridge.
That shared base is the main reason to make CLI behavior and schemas stable
before building several independent interfaces.

## First implementable vertical slice

The recommended first feature is **scene file → headless validation and load →
scripted sample → structured CLI result**. Keep it deliberately small:

1. Define a versioned manifest and scene format for one scene, stable object
   IDs, a transform component, and a script asset reference.
2. Register only the properties needed by that slice. Specify finite-number
   checks, defaults, unknown-field behavior, duplicate-ID behavior, and the
   migration rule for the first schema version.
3. Parse into temporary scene data; validate IDs and script paths before
   touching the live ECS. Resolve project-relative paths without escaping the
   project root.
4. Specify the script-instance contract before migrating the sample. The
   existing `moving_entity.lua` creates its own runtime entity, so attaching it
   unchanged to an authored Player would create a duplicate. Choose whether a
   script instance belongs to a scene, an entity, or both with distinct roles;
   for an entity-owned script, pass an explicit context that resolves the
   authored owner ID to its current runtime handle (for example, a proposed
   `self` API). Define ownership: scene-created entities are destroyed by the
   scene instance, while script-spawned entities are owned by the script or
   another explicitly named owner. Never destroy an entity merely because a
   script once received its handle. Migrate the sample so the authored Player
   is the entity that moves, without duplication.
5. Instantiate on the engine owner thread at a safe boundary. Compile and
   validate before commit, and clean up the isolated scene instance if
   engine-controlled setup fails. Define lifecycle timing and report callbacks
   or external effects already performed; do not promise rollback of file,
   network, or other irreversible script side effects.
6. Add proposed CLI commands equivalent to `project validate`, `scene inspect`,
   and bounded `run --headless --ticks N`; emit one documented machine result
   on stdout and readable logs on stderr.
7. Add fixtures for round-trip, unsupported versions, malformed values,
   duplicate persistent IDs, missing scripts, path traversal, and atomicity of
   the engine-controlled scene instance on setup failure. Separately report
   script side effects that are outside that guarantee. Compare persistent
   IDs/state, never runtime handles.
8. Update README with the actual shipped format and commands only after they
   exist. Mark roadmap items as implemented with evidence rather than turning
   this proposal into a feature claim.

This slice makes authored content inspectable to people and automation while
exercising identity, validation, loading, diagnostics, and headless execution.
It does not require an editor or choose a UI, serialization, JSON, or MCP
dependency prematurely.

## Design rules for implementation agents

- Read the owning API and nearby tests before changing a subsystem. The
  project currently has separate ECS, Lua, engine, renderer, and Makefile
  seams; preserve those boundaries unless a concrete use case requires changing
  them.
- Label a proposed interface as proposed until code, docs, and tests agree.
  Do not imply that a schema example, CLI command, editor operation, or AI
  endpoint already exists because this guide illustrates it.
- Implement one authoring slice at a time with one writer per file. Keep
  commits small and focused, preserve unrelated workspace changes, and avoid
  adding an external dependency until its role and maintenance cost are
  evaluated for that phase.
- Add tests at meaningful contracts: parser/serializer round-trip, negative
  validation, stable diagnostic codes, scene-load atomicity, stale revisions,
  script lifecycle behavior, deterministic headless state, and GPU behavior
  under explicitly available Vulkan checks. Do not substitute a passing
  headless check for a real GPU check or vice versa.
- Update the current-state section and phase gates when behavior actually
  changes. Cite source paths and supported platform evidence. Do not mark
  unvalidated or partially implemented work complete.

## Open decisions

The first implementation should resolve only decisions its vertical slice
needs. Keep these explicit in its design note:

- Project/scene encoding and parser dependency, including preservation of
  hand-written formatting if required.
- Persistent object and asset ID representation, collision handling, and
  rename/move behavior.
- Component registration mechanism and the allowlist exposed to scripts and
  tools.
- Unknown-field policy, schema migration support, and diagnostic/exit-code
  versioning policy.
- Whether scenes can reference other scenes, prefabs, or cyclic object graphs.
- How script side effects are deferred, rolled back, or reported when loading
  or applying edits fails.
- Which graphics/UI approach and platforms to support after the scene contract
  proves useful; neither is selected here.
- Whether and when to expose a transport such as MCP after the shared command
  API is stable.
