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

The default build runs without Vulkan. Its sample executes 120 engine ticks:
Lua creates an EnTT entity, moves it one unit per simulated second, and destroys
it on shutdown. Test targets cover ECS behavior, Lua lifecycle/error handling,
and the engine's Lua-to-ECS integration.

For graphics, set `VULKAN_SDK` to the SDK directory and make its `Bin` directory
available on `PATH`. The SDK supplies headers, the loader library, and `glslc`.

```sh
make -j8 VULKAN=1
make VULKAN=1 run
make VULKAN=1 smoke
make VULKAN=1 smoke-validation
```

`run` opens a scripted moving triangle. `smoke-validation` requires the installed
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

The executable accepts `--headless`, `--ticks N`, `--script file.lua`, and
`--shaders directory`. For example:

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
