# Project commands

Build with `make VULKAN=0 all test`. On Windows the executable is
`build/debug-vk0/bin/CPPGameEngine.exe`. Replace `engine` below with that path.
All authored paths resolve relative to the project manifest, independent of the
working directory.

```text
engine project init examples/first-project build/my-project
engine project validate build/my-project/project.json --format json
engine scene inspect build/my-project/scenes/main.scene.json --project-root build/my-project --format json
engine run build/my-project/project.json --headless --ticks 120 --input move_right:0 --format json
engine project assets build/my-project/project.json
engine project shaders build/my-project/project.json --compiler <path-to-glslc>
engine project package build/my-project/project.json build/my-package --runtime <engine-executable>
engine editor build/my-project/project.json
engine mcp build/my-project
engine platform check android --format json
engine platform check ios --format json
```

`project init` copies a validated template into a new destination. Asset indexing
writes derived data below `.derived`; `project assets --rebuild --compiler <path>`
also imports shaders. Packaging copies project content and the selected runtime
into a new directory with a launcher; run `run.cmd --headless --ticks 120` on
Windows. Windows is the tested packaging platform. Linux remains unvalidated;
macOS and Wayland are not configured.

Every ordinary project/scene/run command emits exactly one JSON document to
stdout, including on failure. `--format json` is optional and is the only format.
Human diagnostics and script logs go to stderr. MCP instead uses its documented
[stdio protocol](mcp.md); the native editor has its own window.

`platform check` reports whether this checkout and host have the build tools and
files for Android or iOS. The JSON result includes `readyToBuild`, backend,
platform requirements, and coded diagnostics. It exits 1 when a prerequisite is
missing or the host is unsupported. A successful check does not establish that
the app launches or presents frames on a device; see the [mobile build guide](../mobile/README.md)
for the separate runtime gates.

```json
{"schema":1,"ok":false,"command":"project validate","result":{"diagnostics":[{"code":"project.read","severity":"error","file":"project.json","path":"","message":"..."}]}}
```

Exit status 0 means success, 1 means validation or content failure, 2 means invalid
command arguments, and 3 means runtime/operational failure. Diagnostic codes and
field paths are the machine contract; messages may change. Result schema 1 has
`schema`, `ok`, `command`, and command-specific `result` fields. Inspect returns
the authored scene plus its revision. Run returns `ticks`, `fixed_delta`, and
the final scene with runtime positions and persistent entity IDs in sorted order.

Bounded runs accept 1–1,000,000 ticks, defaulting to 120, at a fixed 60 Hz step.
Repeated `--input action:tick` options inject a pressed/held action at the
zero-based tick and a release on the following tick. Unknown actions fail.
The manifest's `inputActions` object declares action names and keyboard bindings
(`Right`, `Left`, `Up`, `Down`, or `Space`). With a Vulkan build, omit `--headless`
and pass `--shaders build/debug-vk1/shaders` to display the authored
`MeshRenderer` components. The window samples key state once per tick: a down
transition emits pressed, down state emits held, and an up transition emits
released. Key repeat adds no pressed events. Focus loss clears held state and
emits releases on the next sampled tick. The renderer uses a fixed orthographic
camera, authored entity world positions, the declared mesh/texture, and an
opaque white texture when the optional texture reference is omitted.

For the supplied sample, equal ticks and input produce equal normalized state
on the same platform. The fixture uses no random values. This is not a guarantee
for arbitrary Lua programs, external effects, other architectures, logs, or GPU
pixels. Static validation compiles referenced Lua and checks top-level component
and system declarations in a separate restricted, budgeted Lua state. It does
not call lifecycle or system callbacks. Both validation and `run` use the same
restricted project-script globals: safe logging and project-local `require` are
available, while direct file, OS, network, package, debug, and dynamic loading
APIs are unavailable. Native host callbacks and logs can still have effects.
Each Lua source file, including project-local modules, has a 1 MiB size limit;
oversized project scripts report `project.script.too_large` during validation.

The legacy no-subcommand demo flags remain available for renderer smoke/manual
comparison. They print human text and are not part of the JSON command contract.

Run `powershell -File tools/test-cli.ps1` after a headless build to exercise the
command/exit contract, deterministic fixture, initialization, and a packaged
runtime launched from a different working directory.
