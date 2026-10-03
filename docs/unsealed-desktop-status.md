# Unsealed desktop engine integration status

## Result and scope

All R01-R16 engine capabilities are implemented and validated for the local Windows desktop scope at code commit `4d3cfbdb3deda2cc21981caaa61bf8b1496bbe38`. Integration branch `codex/unsealed-desktop` was fast-forwarded to that exact independently reviewed clean foundation revision; the final local documentation commit changes only README/index/this record. At the original implementation handoff, source engine main remained at `34a9a13e782be2a2822cbf8f1cc2ca0497e99c67`. Unsealed's imported engine and existing authored changes were preserved; its staged consumption pin remains `e110e846600133db058a32048df1adcada8126d6`.

Phone-only work, unresolved game canon and Steam SDK integration are excluded. The synthetic lab chooses no production resolution, story or approved combat format. No publishing, PR, main merge or Unsealed pin update was performed during the original implementation task. Physical audibility, subjective text/art quality and physical gamepad interaction remain manual checks. Linux, macOS, Wayland and mobile execution of these extensions are unverified; existing mobile platform documentation retains its separate gates.

## Acceptance matrix

The references below identify implemented contracts and behavior evidence, rather than claiming that passing a module test alone establishes integration. `F0/F1`, `W`, `G`, and `P` refer to the evidence key following the table.

| ID | Implemented desktop behavior | Source / contract | Evidence |
| --- | --- | --- | --- |
| R01 | Lua/ECS stable IDs and lifecycle | [runtime](../src/project/runtime.cpp), [Lua API](scripting.md) | F0/F1 runtime/Lua suites, W |
| R02 | RGBA PNG, alpha, sprite atlases/layers/tint | [rendering](desktop-2d-rendering.md) | F0/F1 CPU rendering, G alpha/painter readback, P |
| R03 | Deterministic animation | [sprites](../src/rendering/sprite2d.cpp), [runtime](../src/project/runtime.cpp) | F0/F1 rendering/runtime, W |
| R04 | Pixel snapping, logical camera, integer/fractional viewport and resize | [rendering](desktop-2d-rendering.md) | G viewport/letterbox/resize pixels, F1 smoke |
| R05 | Swept solids, simultaneous corners, triggers and queries | [mechanics](gameplay2d.md) | F0/F1 gameplay tests, W movement/trigger assertions |
| R06 | Transactional rooms and stale-handle cleanup; candidate effects confined | [runtime](../src/project/runtime.cpp), [Lua API](scripting.md) | F0/F1 runtime failure/lifetime tests, W transitions/rejection |
| R07 | Strict UTF-8 atlas text, wrapping/scrolling/clipping/focus | [rendering](desktop-2d-rendering.md), [UI](desktop-input-ui.md) | F0/F1 rendering/UI, G font pixels, W modal selection |
| R08 | Modal gameplay-input ownership and transition quarantine | [input/UI](desktop-input-ui.md) | F0/F1 input/UI and runtime, W masking/confirm/cancel |
| R09 | Lua follow/flock queries, collision and animation | [mechanics](gameplay2d.md), [controller](../examples/desktop-2d/scripts/controller.lua) | F0/F1 mechanics/runtime, W followers/idempotence |
| R10 | PCM playback, mixer, loops/mute/buses and scene cleanup | [services](desktop-services.md), [audio](../src/audio/audio.cpp) | F0/F1 mixer tests and actual WinMM device open/queue/stop/reset/close; audibility unverified |
| R11 | Durable saves, backup/recovery and failure paths | [services](desktop-services.md) | F0/F1 persistence injection/bounds/recovery, W cross-process restore |
| R12 | Version migrations and stable data IDs | [persistence](../src/project/persistence.cpp), [progression](../examples/desktop-2d/scripts/progression.lua) | F0/F1 future-version/migration tests, W pure/new/restore |
| R13 | Settings separate from progression | [services](desktop-services.md) | F0/F1 persistence/runtime, W persisted bindings/gains/comfort |
| R14 | Keyboard/mouse/gamepad mapping, remap and focus/device transitions | [input/UI](desktop-input-ui.md), [desktop bridge](../src/project/desktop_input_bridge.h) | F0/F1 mapper/bridge tests, W; physical devices manual |
| R15 | Shared Lua quest/reveal/event/branch/cutscene/checkpoint/idempotence demonstration | [synthetic lab](../examples/desktop-2d/README.md) | W both variants, reward/cancel/defeat/control/restore assertions; no actual canon |
| R16 | Shipping shaders/assets/licenses/launchers and CLI/package contracts | [packaging](desktop-packaging.md), [commands](commands.md) | F0/F1 tooling, CLI, P headless/visible unrelated-cwd launch |

## Exact-revision evidence

- **F0/F1**: foundation handoff and inspected `../../unsealed-foundation/build/foundation-vk0.log` / `foundation-vk1.log`, `VULKAN=0/1 all test`, seven suites each PASS at exact code commit above. F1 also runs `desktop2d-smoke smoke-validation`; validation enabled, direct renderer/RenderDevice readbacks and persistent-resource frame backend pass. Native PCM device lifecycle passes, with explicit audibility unverified. CLI JSON/diagnostics/determinism/init/standalone-package checks passed in the exact-revision foundation handoff.
- **G**: F1 `desktop2d-smoke` checks alpha/transparent background/painter order/font clipping/integer and fractional viewport/resize readbacks on the existing RTX 5080. `../../unsealed-foundation/build/desktop2d` retains pixel artifacts. Validation smoke covers cancel, resize, surface loss, move and reinitialization. This establishes tested pixels, not subjective legibility or visual polish.
- **W**: foundation `../../unsealed-foundation/build/desktop-example-check-006` passes both placeholder variants and separate restore processes, including room B, safe exact saved position and accepted choice. Integration rerun uses this checkout's source/scripts/assets and the verified same-code foundation V0 binary: [log](../build/integration-walkthrough-final.log), [revision/hash/exit/time record](../build/integration-walkthrough-final-record.json), [fresh artifacts](../build/integration-desktop-example-final-001). Exit 0; wall time 1.2553848 s for static/validation/pure and both variant new/restore runs. Binary SHA256 `54835708E3F5ECA60B1E71FD78E85413199B31F15332A2CBE5950609E3520BC1`. This timing includes subprocess startup and makes no FPS claim.
- **P**: exact foundation V1 binary package `../../unsealed-foundation/build/desktop-package-final-002` contains real shaders and engine/dependency notices. Package binary SHA256 matches the final foundation V1 binary: `920926254C7D993670777EEF00288F9BE11A87CE74AF13E00A88297FAF244672`. `../../unsealed-foundation/build/packaged-headless-final-002.log` and `packaged-visible-final-002.log` pass 360 ticks launched from an unrelated cwd, reach room B and `DESKTOP2D_WALKTHROUGH_PASS:male`. Visible run wall time 8.35 s; normal visible 120 ticks 2.72 s, both including startup/fixed-tick limiter, not frame-budget measurements.
- Toolchain: GNU Make 4.4.1, MSVC 14.50.35717, Windows PowerShell Desktop and Python 3 authoring fixture. Portable LunarG SDK 1.4.357.0 in ignored `build/tools/VulkanSDK/1.4.357.0`; valid LunarG signature and SHA256 `81f474711e9042f4cd22b31b2f7a8870db2e428b21586fb43dd80150be97310d`. Copy-only extraction uses no system environment/registry changes. [Provenance](../build/portable-vulkan-status.md); component notices in SDK `Licenses/LICENSE.txt`.
- Original baseline output is preserved under this checkout's ignored build directory. Exact reviewed code was reused rather than repeated cold builds; documentation changes do not alter those binary contracts.

## Ownership, review and preservation

All task worktrees live under `D:\Git Repositories\_Workspaces_\CPP-Game-Engine` on matching `codex/<worktree-name>` branches. Root owns scope, final review and publication decisions. All feature writers stopped before integration.

| Owner | Worktree | Completed bounded work |
| --- | --- | --- |
| engine_interfaces | unsealed-foundation | central project/runtime/Lua/commands/Makefile/schema/API docs and dependency integration |
| desktop_rendering | unsealed-rendering | rendering helpers, PNG/font/alpha/GPU tests; corrected `6a996e5` |
| desktop_mechanics | unsealed-mechanics | gameplay2d; final `67d601b` resolves corner behavior |
| desktop_services | unsealed-services | audio/persistence; original `34d48f5`, corrected `0b6ffc6` |
| desktop_input_ui | unsealed-input-ui | actions/UI; original `99cf795`, corrected `3f70a6a`, later schema consistency `858f437` |
| desktop_requirements | unsealed-example | examples/desktop-2d only; original handoff `9047f27`, later restore correction `61ade9f` |
| desktop_services (packaging assignment) | unsealed-packaging | iteration helper/tests shaders and notices; `f3078b8` |
| engine_baseline | unsealed-desktop | setup/tooling, authorized exact-code fast-forward, README/index/status documentation and fresh integration fixture |

Focused independent reviews found material issues; corrected scene effects, static sprites, bounded Lua/JSON reads, input edges/fractional pointer/schema consistency, helpers, UI and packaging were consumed before the clean final foundation handoff. Root reported all material findings resolved; packaging independent review `05c3d98` found none. Root final integration artifact review passed before closure.

No active integration commands remain. Main/source and imported consumption were preserved. Root final artifact review is complete. The user subsequently authorized local main consolidation; root reviewed the candidate and approved the exact local source-main fast-forward. Remote review/publication and Unsealed pin updates remain separate actions.

## Authorized local branch consolidation

The user explicitly requested merging all eight `codex/unsealed-*` task branches into local engine main. All nine engine worktrees were clean at preflight. The original integration checkpoint was `c9ad1761d5539c0ad2ee3b81a66874e91ddeaf6a`, tree `788318d4915ce9d1b999ae108b01aabe6764da1a`; original main was `34a9a13e782be2a2822cbf8f1cc2ca0497e99c67`.

Foundation was already an ancestor. Each other original feature history had only patch-equivalent commits (zero unmatched patches); normal merge simulations were conflict-free and tree-identical. Six normal merge commits preserve those original histories. Consolidated graph checkpoint `146b471f4b806153d9fd8841a9b902d640b5c231` has exactly the original integration tree, retaining all reviewed fixes without implementation changes. This record-only commit adds the consolidation audit; production source remains identical to tested `4d3cfbd`.

| Captured branch | Original tip now proven ancestor |
| --- | --- |
| codex/unsealed-desktop | c9ad1761d5539c0ad2ee3b81a66874e91ddeaf6a |
| codex/unsealed-foundation | 4d3cfbdb3deda2cc21981caaa61bf8b1496bbe38 |
| codex/unsealed-example | 61ade9f74e5a4d489bcead3ade6fd3f2ab5108a3 |
| codex/unsealed-input-ui | 858f437067824ca44105c9ee3e9be7372fe311e9 |
| codex/unsealed-mechanics | 67d601bcc007f314d3ab5f33ff61f6f3ce234f65 |
| codex/unsealed-packaging | f3078b8ecea1dca6d82cd235821cedd41349baaa |
| codex/unsealed-rendering | 6a996e5273cd7c47bea9f15aa89bc2be0f75516d |
| codex/unsealed-services | 0b6ffc6b1b8a9d698830329fb3f46fd0e75554a9 |

Evidence: [preflight and patch audit](../build/merge-consolidation-preflight.json), [merge simulations](../build/merge-consolidation-simulations.json), [merge commits](../build/merge-consolidation-merges.json), [fresh full fixture log](../build/merge-consolidation-walkthrough.log), [revision/binary-hash/exit/time record](../build/merge-consolidation-walkthrough-record.json). The full fixture passes static/validation/pure, both variants and separate restoration processes, exit 0, using unchanged foundation V0 binary SHA256 `54835708E3F5ECA60B1E71FD78E85413199B31F15332A2CBE5950609E3520BC1` and isolated artifacts. Previous exact-code Windows CPU/GPU/CLI/package checks remain applicable. Remote CI and SA/PR review are not claimed; no publication, branch/worktree cleanup, or Unsealed change occurred. Root candidate review passed and the local main fast-forward is approved. The actual resulting main revision is recorded in ignored integration evidence `build/merge-consolidation-main-result.json`.
