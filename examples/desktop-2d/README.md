# Desktop 2D synthetic lab

This engine example demonstrates desktop host capabilities using original test
art, glyphs and sine tones. It is not an authored Unsealed game, Bible edition,
story scene, approved combat format or production resolution. Male and female
are placeholder variant IDs with the same shared route. Developer passage A is
original test text; no scripture is included.

The runtime needs the desktop extensions documented in
[Lua scripting](../../docs/scripting.md),
[desktop rendering](../../docs/desktop-2d-rendering.md),
[input/UI](../../docs/desktop-input-ui.md), and
[project format](../../docs/project-format.md). Older schema-1 engines lacking
SpriteRenderer, Camera2D, Collider2D, font/audio/scene assets and inputBindings
can run only the pure progression/declaration fixtures below.

## Launch and controls

From the engine repository, using a built Vulkan runtime and shader directory:

```powershell
& ./build/debug-vk1/bin/CPPGameEngine.exe run ./examples/desktop-2d/project.json --ticks 36000 --user-data ./build/desktop-example-user --shaders ./build/debug-vk1/shaders
```

The user-data directory contains player saves and settings separately from the
read-only authored example. Reuse it to load a previous session; choose a fresh
directory for a separate experiment. The runtime starts a new session; L loads
the saved progression deliberately. Persisted bindings/audio apply at startup.

| Action | Keyboard / pointer | Standard gamepad |
| --- | --- | --- |
| Move | WASD or arrows | Left stick |
| Inspect nearby marker | E; Q after F remap | A |
| Open/reopen developer page | J | Y |
| Rest/save at green marker | R or inspect | X or A |
| Load last successful save | L | Back |
| New session | N | — |
| Choose placeholder variant | P | — |
| Developer story reveal | T | — |
| Authored injury/descent demo | H | — |
| Ordinary defeat/retry demo | B | — |
| Save complete action map with interact remapped to Q | F | — |
| Mute/unmute mixer | M | — |
| Confirm / back | Enter / Escape | A / B |
| Choice navigation / page scrolling | Up/Down; wheel over panel | Dpad Up/Down |
| Choose focused UI row | Left click | — |

Key sharing between movement and UI is intentional: modal capture and release
quarantine prevent movement or repeated interaction beneath an overlay. Return
focus after releasing a held physical control. A disconnected/reconnected pad
must likewise return to neutral before acting.

## Manual walkthrough

1. The cyan player starts at the left. Move against room borders and the raised
   obstacle: solid sweeps stop motion. Two white birds follow without blocking
   the player. The camera follows on the 16-pixels-per-unit grid.
2. Inspect the yellow page marker at (-2,0). It initially shows UNREVEALED and
   a locked placeholder only. Dismiss and reopen it; confirm must not move the
   player or activate the marker underneath. T reveals original test text.
   Scroll the long page, including the explicit Ω and 十 glyphs.
3. Inspect the orange quest marker at (2,0), accept, then inspect the right blue
   exit at (6,0). In room B inspect the lower blue objective at (1,-2). Return
   through the exit and inspect the quest marker again. Reward delivery is
   repeatable safely: one passage ID retains both quest and developer story
   provenance. Choosing either placeholder variant preserves this route.
4. Reach the green rest marker at (-4,2). Rest gathers the birds, and a successful
   durable write produces explicit SAVE CONFIRMED and a cue. A failure shows
   SAVE FAILED and does not play the success cue. Quit/restart and press L:
   the room, exact safe saved position, accepted choice, quest, reveal IDs and provenance return.
5. H runs a cancellable developer injury/descent. Escape restores control with
   no healing award; waiting three seconds completes the event once. B ordinary
   defeat enters retry state and cannot grant authored healing. Replaying H
   after completion cannot spawn extra followers or repeat completion.
6. F saves a full binding map with E changed to Q plus separate mixer/comfort
   settings. Restart with the same directory: Q remains inspect, music gain
   remains .25, and reduced motion holds sprite/glow frames still. This setting
   also makes authored descent stationary while preserving the completion
   interval. M allows the entire walkthrough with sound muted.
7. Resize and minimize/restore the window. At framebuffer sizes at least
   320x180, use integer viewport scaling and centered letterboxes. Smaller
   framebuffers use a fractional fit; pixel-perfect output is not claimed.
   Text uses a separate logical UI camera; inspect long pages and visible focus
   at the smallest target window.

Rooms use stable object/entity IDs; opaque runtime handles are never serialized.
Follower doves (actor:dove1/2) and the one hidden scene dove have separate owners.
Scene controls are locked with an explicit key and released on completion,
cancellation and teardown. Room loops stop in on_destroy and restart only in
the accepted room's first update. All irreversible services are deferred until
on_update so candidate on_create validation cannot play/save external effects.

## Automated acceptance

Python 3 standard library is an authoring/test convenience, not a build or
runtime dependency. Run static checks and the full engine integration runner:

```powershell
python ./examples/desktop-2d/tools/validate_fixture.py
python ./examples/desktop-2d/tools/run_walkthrough.py --engine ./build/debug-vk0/bin/CPPGameEngine.exe --artifacts ./build/desktop-example-check-001
```

Choose a fresh artifacts directory per independent new walkthrough. The runner
starts both male/female fixture variants and a separate restore process for each,
reusing only that variant's user-data directory. It checks exit status, parsed
JSON ok and assertion markers, retaining stdout JSON and stderr logs. Running
from the executable directory also checks project paths independent of cwd.

The exact new-game commands emitted by the runner are:

```powershell
& ./build/debug-vk0/bin/CPPGameEngine.exe run ./examples/desktop-2d/project-test-new-male.json --headless --ticks 360 --user-data ./build/check-male --input move_right:5 --input ui_confirm:6 --input interact:6 --input ui_down:8 --input ui_confirm:9 --input ui_back:16 --input ui_back:18
& ./build/debug-vk0/bin/CPPGameEngine.exe run ./examples/desktop-2d/project-test-restore-male.json --headless --ticks 120 --user-data ./build/check-male
```

Repeat with female manifests and a separate check-female directory. Successful
new/restore runs log DESKTOP2D_WALKTHROUGH_PASS:<variant> and
DESKTOP2D_RESTART_PASS:<variant>. The restore script additionally reports
DESKTOP2D_RESTART_RESTORE_PASS immediately after verifying on-disk data/settings.
A missing injected UI action fails the corresponding assertion and faults the
session; merely reaching the tick limit is insufficient.

The final durable checkpoint deliberately uses nondefault room B, position
(3.25,-2.25), and accepted choice 2. Restore asserts the on-disk values before
changing state, queues B from the A startup room, then verifies the accepted
room, exact applied position, and choice on subsequent ticks. Position application
runs only in the accepted target scene; a blocked saved position is rejected
with visible feedback rather than silently moving to a different checkpoint.

The headless controller checks real movement through a solid sweep, stable
trigger overlaps/events, modal masking of an injected move, confirm/selection/
cancel UI events, early reward rejection, duplicate acceptance/rewards, midquest
save/read, shared reward provenance, scene cancellation, ordinary defeat,
three seconds of actual on_update timing with no early healing and exactly
one completion, full settings/rebind persistence, two room transitions,
missing scene rejection and progress preservation across separate processes.
The new fixture does not overwrite an existing save automatically except during
its deliberately isolated scripted walkthrough.

The pure progression suite covers both variants, objectives observed before
acceptance, idempotent rewards and provenance, locked text concealment, explicit
v1-to-v2 snapshot migration, and rejection of newer schemas/unknown passage IDs.
It can run on the older runtime:

```powershell
python ./examples/desktop-2d/tools/run_walkthrough.py --engine ./build/debug-vk0/bin/CPPGameEngine.exe --pure-only --artifacts ./build/desktop-example-pure-001
```

project-syntax.json compiles every Lua file and declaration without executing
new host-service callbacks; project-pure.json executes reducer assertions only.
Neither proves the desktop extensions. Static checks validate all manifest paths,
stable references, sprite/font atlas bounds, RGBA transparency/translucency,
ASCII/Ω/十 atlas coverage, PNG CRCs and PCM16 WAV headers. Pure/static passes do
not establish GPU text legibility, actual audio output, durable platform behavior,
focus/device sampling or failure recovery. Keep the engine's service failure,
Vulkan validation/readback and input/UI suites plus a visible/audible walkthrough
as independent integration gates.

## Original assets and reusable logic

[Asset provenance](assets/provenance.json) records the original fixture files.
[The generator](tools/generate_assets.py) reproduces the PNG, font and WAV outputs
with Python standard library only. The sprite atlas contains clear-alpha edges,
partially transparent markers/glow, four player frames and four dove frames.
The original 5x7 glyph grid covers printable ASCII scalars (lowercase intentionally
shares uppercase shapes), Ω and 十. Unspecified punctuation uses the question
shape; this is a developer font, not universal language/font support. The loop
is a one-second 220 Hz sine with an integral number of cycles; cue gain is tapered.
All fixture artwork, glyph definitions, tones and scripts use the engine's MIT
license. No third-party or generated model media is used.

[progression.lua](scripts/progression.lua) is a reusable pure reducer example.
[controller.lua](scripts/controller.lua) connects it to confined state/save,
settings, movement, UI, audio and scene services. Mutations publish a validated
versioned snapshot; ordinary presentation does not award progression. Catalog
content IDs, scene rules, exact controls, visual metrics and sound gains are
fixture configuration, not mandatory engine or production game choices.
