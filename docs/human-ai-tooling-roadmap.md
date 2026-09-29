# Human and AI Tooling Roadmap

This document describes a practical path from the current engine prototype to a
project that people can author, inspect, and iterate on, and that automated
tools can operate safely. It records the implementation sequence and the bounded
version 1 interfaces now provided. Examples labelled **proposed** remain design
illustrations; the current-state table and linked interface documents describe
shipped behavior.

The central recommendation is to build one explicit, versioned project and
scene contract, then expose it through deterministic headless commands. Human
interfaces and AI integrations should call the same validated operations.
This gives each interface the same rules for identity, references, edits,
diagnostics, and saves. Start with an inspectable scene and validator; do not
begin by coupling an editor or an agent directly to live ECS storage.

## Current state

The six phases below are implemented as a deliberately bounded version 1
interface. The detailed design discussion records the original sequence;
examples still labelled proposed are illustrative rather than command syntax.
Use [project format](project-format.md), [commands](commands.md),
[Lua scripting](scripting.md), [Vulkan rendering](vulkan.md), and [MCP](mcp.md)
for the shipped interfaces and limits.

| Phase | Implementation | Acceptance evidence |
| --- | --- | --- |
| 1. Project/scene contract | `src/project/project.*`: JSON schema 1, stable scene/entity/asset IDs, confined catalog paths, typed Transform/Script/MeshRenderer, authored input actions, shared revisions and saves | `src/test/project/project_tests.cpp`: round trips, unknown fields/versions, references, confinement, edits and conflict/failure checks |
| 2. Headless commands | `src/commands.cpp`: inspect/validate/run with versioned JSON, stderr diagnostics, bounded fixed-step state snapshots | `tools/test-cli.ps1` and RuntimeTests: result/exit checks and equal normalized state under equal input |
| 3. Content slice | `examples/first-project`: entity-owned Lua, declared keyboard action, mesh and texture references; CPU loaders plus camera/depth/material/texture Vulkan drawing | RuntimeTests and rendering CPU tests; separately gated VulkanSmoke real-GPU/readback checks |
| 4. Minimal editor | `src/editor`: Windows native hierarchy, typed inspector and stable script picker, logs, save/reload, undo/redo, isolated play/stop | EditorTests; native UI smoke remains separate from document-model tests |
| 5. Iteration/package | `src/tooling`: initialization, stable-ID index and dependency hashes, shader imports, staged script reload, standalone runtime package | WorkflowTests, CLI package test from an external working directory, `.github/workflows/windows-headless.yml` |
| 6. AI adapter | `src/automation`: read-only project validation and scene inspection through bounded, path-confined MCP stdio | WorkflowTests transport fixtures and executable routing |

The current Lua-defined component interface accepts `number`, `boolean`, and
`string` fields with versioned schemas, defaults, and registration limits; see
[Lua scripting](scripting.md). Vector, enum, and reference fields are deferred
until storage and validation support them. The phase 8 design below lists the
broader type choices and remaining contract work, rather than claiming those
types are available in version 1.

Version 1 has no earlier released format to migrate. Unsupported versions are
rejected; future migrations must be explicit and tested. Transform exposes world
position only; parent hierarchies, rotation, prefabs, arbitrary component
reflection, game save-state, and transactional external script side effects are
not part of this contract. Undo/redo covers supported position and script-reference
edits. The editor has no embedded viewport. MCP deliberately remains read-only.

Windows is the active validated platform; the Linux branch remains unvalidated,
and macOS/Wayland are not configured. GPU smoke is independent of headless tests;
an unsupported readback skip is not evidence of verified pixels. Determinism is
limited to normalized state from the supplied non-random fixture on the same
platform with equal fixed delta, tick count, and input events.

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

## Planned extensions: renderer, Lua ECS, and mobile platforms

The milestones below extend the implemented Windows-oriented version 1. They
are **planned**, not shipped: the current-state table above remains the evidence
for implemented behavior. Keep each platform and backend claim tied to the
actual build and runtime checks listed in its gate.

### 7. Separate renderer contracts from backend execution

**Outcome:** engine code can prepare and submit rendering work through a
backend-neutral interface, and worker threads can record independent work
without violating backend or resource lifetime rules. Vulkan remains the first
backend; this phase does not claim portability to another graphics API.

Define backend-neutral frame, command-list, resource-description, capability,
and diagnostic contracts before moving Vulkan-specific objects behind them.
Expose a capability query for required and optional features. Return a stable
feature identifier and actionable diagnostic when initialization or a requested
draw cannot proceed. Keep backend feature checks close to the backend, while
allowing project/runtime code to select a supported rendering path before
opening a confusing blank window.

Replace raw lifetime assumptions with persistent renderer resource handles.
Handles need a resource kind and generation (or equivalent stale-handle
protection), explicit creation/destruction ownership, and documented behavior
when a resource is replaced or the device is lost. Define which thread may
create, update, destroy, and resolve each resource. Frame snapshots must retain
all referenced resources until GPU execution completes; callers must not need
to keep temporary CPU descriptions or shader objects alive unless the contract
explicitly says so.

Specify command recording, queue submission, and synchronization separately.
Document which work can run concurrently, which queues own each submission,
how dependencies and fences/semaphores establish completion, how frame reuse
waits for completion, and how cancellation, resize, shutdown, and device loss
retire pending work. Keep window/surface operations on their required owner
thread. Do not infer safety from the existing ECS render-job thread pool: its
jobs currently finish before caller-thread frame coordination.

**Done gate:** backend-neutral contract tests build without Vulkan headers or an
SDK; the Vulkan implementation passes the existing smoke and validation checks
plus repeated multithreaded recording/submission, resize, cancellation,
resource-replacement, shutdown, and reinitialization checks. Tests must catch
stale resource handles and prove resources are not reused before their fence
signals. Vulkan feature queries must report both supported and unsupported
capabilities with stable diagnostics. A second backend is a separate gate
before claiming more than one graphics backend.

### 8. Allow Lua-defined typed components and ECS systems

**Outcome:** project scripts can register bounded component types and systems
through a typed host API while ECS storage, views, and scheduled-job caches stay
coherent across structural changes and script reloads.

Build on the component/property registration layer in phase 1. Define stable,
versioned type and property names; allowed scalar/vector/enum/reference types;
defaults and validation; per-project registration limits; and how duplicate,
unknown, and incompatible registrations fail. Store authored values in dense
component storage with a documented alignment, relocation, and destruction
contract. Lua receives typed values or checked proxies, never arbitrary C++
memory, object layouts, or pointers into storage that may move.

Let Lua register named systems with explicit read/write component sets,
execution phase or wall, ordering constraints, and lifecycle ownership. Validate
access declarations and conflicts before scheduling. Define what happens when a
component schema changes while systems or scenes use it, and how reload removes
or replaces registrations without leaving stale callbacks or storage behind.

Route structural mutations from Lua through a command buffer applied at a
documented tick boundary. Commands cover component registration where allowed,
entity/component creation and removal, and system registration/removal. Validate
the full batch before commit, or document and test the precise partial-apply
behavior on failure. Coalesce query/cache generation changes where possible,
but ensure all affected view matches, query caches, and cached executable job
batches are invalidated before the next read or dispatch. Stale entity and
component handles must fail safely after destruction, dense-storage relocation,
or reload.

**Done gate:** tests first populate and reuse view/query and scheduler caches,
then perform Lua-authored structural changes and verify the next tick sees each
matching entity exactly once with no removed entity or system retained. Cover
dense-storage growth/relocation, component removal, invalid batches, conflicting
system access declarations, reload/unload cleanup, stale handles, and failed
callbacks. Repeated identical fixed-tick fixtures produce the same normalized
component state. Existing static project validation reports invalid component
and system declarations before runtime execution.

### 9. Verify iOS and Android as supported targets

**Outcome:** iOS and Android become declared targets only after their complete
toolchain, platform integration, packaging, and runtime gates pass. This phase
does not imply that desktop builds or cross-compilation alone establish mobile
compatibility.

Add a versioned platform-support manifest and a `platform check` command (names
are proposed) that report each target's status, required compiler and SDK,
deployment/API target, enabled native extensions, build configuration, and
missing prerequisites. The command should distinguish implemented, build-
verified, and device-verified status and point to the failed check. CI must use
clean builds for every target marked build-verified; manifests and reports must
not promote a target automatically when a prerequisite is absent.

Audit and implement platform-specific window/surface creation, input and
application lifecycle hooks, filesystem and asset lookup, thread restrictions,
and packaging. Document the Vulkan instance/device extensions and surface
requirements actually selected on each OS, including any portability or native
surface integration needed by the chosen SDK/window path. Where Vulkan is not
available or not selected, name the supported backend and its verified feature
set rather than silently falling back. Handle suspend/resume, orientation or
resize, context/device recreation, app storage paths, and asset packaging in
the runtime contract. Record signing, provisioning, deployment-target, and
store-package steps as external release prerequisites when CI cannot validate
them.

**Done gate:** clean iOS and Android CI jobs build the project and package the
sample with documented toolchain/SDK versions. Each target launches on a named
simulator/emulator or physical device, loads packaged assets from the installed
app, accepts its declared input, renders a known sample through the selected
backend, and survives suspend/resume plus orientation/resize and renderer
recreation. Where pixel readback is unavailable, report that limitation and
verify rendering through an explicitly documented device-side signal. The
platform check command agrees with CI evidence, and the support matrix names
the exact tested OS/runtime/device class. Until those gates pass, preserve the
current Windows-active, Linux-unvalidated, macOS/Wayland-unconfigured status
and mark iOS/Android unsupported or unverified.

## First implementable vertical slice

The recommended first feature is **scene file â†’ headless validation and load â†’
scripted sample â†’ structured CLI result**. Keep it deliberately small:

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
