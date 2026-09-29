# CPP-Game-Engine

C++23 engine with the official EnTT ECS, embedded Lua 5.4 scripting, and a Vulkan
graphics pipeline. Dependencies are pinned Git submodules under `third_party/`:

```sh
git submodule update --init --recursive
```

The root build uses **GNU Make 4.x**, compiling GLFW and Lua directly; CMake is not
needed. Run commands from the repository root so sample asset paths resolve.

On Windows, install Visual Studio C++ tools, GNU Make (for example
`mingw32-make`), and optionally the Vulkan SDK. Open an **x64 Native Tools Command
Prompt** and substitute `mingw32-make` for `make` below if needed. The build uses
MSVC `/std:c++latest`; a compiler supporting C++23 `std::expected` is required.

```sh
make -j8
make test
make run
```

The default build runs without Vulkan. `make run` loads the authored
[first project](examples/first-project/README.md) for 120 fixed simulation ticks.
Its persistent Player entity is owned by the scene and controlled through an
entity-owned Lua script. Test targets cover ECS, Lua, engine integration,
project validation/editing, isolated runtime, editor, asset tooling, and MCP.

Project commands return a versioned JSON result on stdout and readable logs on
stderr. For example (append `.exe` on Windows):

```sh
build/debug-vk0/bin/CPPGameEngine project validate examples/first-project/project.json
build/debug-vk0/bin/CPPGameEngine run examples/first-project/project.json --headless --ticks 120 --input move_right:0
build/debug-vk0/bin/CPPGameEngine editor examples/first-project/project.json
```

See [command and packaging usage](docs/commands.md), the
[project/scene contract](docs/project-format.md), and the [read-only MCP adapter](docs/mcp.md).
Android and iOS builds use the same authored project runtime through SDL3; see
the [mobile build guide](mobile/README.md) and run `CPPGameEngine platform check android`
or `CPPGameEngine platform check ios` to inspect host prerequisites.
The native authoring editor is Windows-only and provides hierarchy, selection,
typed position/script edits, asset selection, validation, save/reload, undo/redo,
and isolated play/stop. The editor has no embedded viewport; the visible `run`
command displays authored mesh/texture assets with Vulkan.

For graphics, set `VULKAN_SDK` to the SDK directory and make its `Bin` directory
available on `PATH`. The SDK supplies headers, the loader library, and `glslc`.

```sh
make -j8 VULKAN=1
make VULKAN=1 run
make VULKAN=1 smoke
make VULKAN=1 smoke-validation
```

`run` opens the authored textured sample; its declared Right-arrow action moves
the Player. `smoke-validation` requires the installed
Khronos validation layer; for a portable SDK on Windows, set `VK_LAYER_PATH` to
`%VULKAN_SDK%\Bin`. The smoke test exercises multiple draws, resize/minimize,
cancellation, invalid inputs, and renderer reinitialization on a real Vulkan GPU.

Output lives in `build/debug-vk0` or `build/debug-vk1`. `CONFIG=release` selects
separate optimized output directories. `BUILD=path` can isolate custom compiler
flags/toolchains. Effective compiler/link options are recorded; changed options
invalidate existing objects. Use a fresh directory after replacing an SDK or
compiler in place. Project and vendor header changes trigger rebuilding (conservative dependencies
on MSVC, plus generated `.d` dependencies on GCC). To remove output, delete the
repository's `build` directory; `make clean` prints this instruction.

The legacy demonstration (without a subcommand) still accepts `--headless`,
`--ticks N`, `--script file.lua`, and `--shaders directory`. For example:

```sh
build/debug-vk1/bin/CPPGameEngine --headless --ticks 60
build/debug-vk1/bin/CPPGameEngine --ticks 180 --shaders build/debug-vk1/shaders
```

On Windows append `.exe`. Headless execution advances fixed timesteps without
sleeping; the graphical loop polls GLFW and submits rendering on the main thread.
`Engine::run()` likewise executes polling, Lua updates, simulation, and frame
coordination sequentially on its caller thread, waiting for scheduled ECS jobs
between phases. Call it on the window-owning thread. `setLogicTickLimit(N)` gives
a bounded run; `finishExecution()` requests stopping after the current phase,
and `awaitTermination()` waits until idle, script teardown, and shutdown listeners
have finished. External callers should not mutate the ECS while a run is active.

The Makefile also includes a Linux/X11 branch requiring a C++23 GCC/libstdc++, X11
development headers (Xrandr, Xinerama, Xcursor, and Xi), and `libX11`, `dl`, pthread,
and math libraries. Vulkan builds additionally require Vulkan headers/loader and
`glslc` on `PATH`. Linux is not validated in the current Windows environment;
macOS and Wayland builds are not configured.

See [ECS design](src/ecs/README.md), [Lua scripting](docs/scripting.md), and
[Vulkan rendering](docs/vulkan.md) for API details and current rendering limits.
For implementation scope and phase evidence, see the
[human and AI tooling roadmap](docs/human-ai-tooling-roadmap.md).
