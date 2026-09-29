# Android and iOS sample

The mobile target compiles the engine's ECS, Lua scripting, project/runtime,
rendering, and Vulkan sources with an SDL3 window. `src/platform/mobile_main.cpp`
loads `examples/first-project/project.json`, validates its asset catalog, starts
the project Runtime, and renders each authored MeshRenderer with its mesh and
texture. Touching the right half of the screen presses the project's
`move_right` action; the authored Lua script moves the Player. The SDL event
watch stops rendering in the background and rebuilds the Vulkan device and
surface after foreground or pixel-size changes. Both platforms copy the
packaged project and SPIR-V shaders into private app storage because the
existing project, Lua, mesh, texture, and shader loaders use ordinary files.
`MOBILE_STATE` log entries report startup, completed observed frames (including
the Player x position and surface generation), pause/resume, surface recreation,
and shutdown for simulator/device smoke tests.

The support manifest is [platform-support.json](platform-support.json).
`CPPGameEngine platform check android` or `... ios` checks this checkout and the
current host. Its `readyToBuild` result means local prerequisites are present;
runtime presentation remains a separate gate. The manifest keeps `verified`
false until CI and hardware evidence exist.

## Android

Install Android SDK API 35, NDK 27.2.12479018, CMake 3.22.1, JDK 17, and
`glslc`; set `ANDROID_HOME` and `ANDROID_NDK_HOME`. Run:

```sh
bash mobile/android/build.sh
adb install -r mobile/android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.cppgameengine.mobile/.GameActivity
```

The script clones SDL release 3.4.16 at its pinned commit and uses SDL's
Gradle wrapper. Gradle packages the authored project and generated SPIR-V, then
cross-compiles the same core code through `mobile/CMakeLists.txt` for arm64 and
x86_64 emulator ABIs. The APK
declares Vulkan hardware; devices lacking Vulkan cannot install it. No camera,
audio, network, or storage permission is requested.

## iOS

On macOS, install Xcode, CMake 3.28 or newer, and a Vulkan SDK with its **iOS
development libraries** (including MoltenVK). Set `VULKAN_SDK` to the SDK
directory containing `iOS/setup-env.sh`, then run:

```sh
bash mobile/ios/build.sh
xcrun simctl install booted build/mobile-ios/Debug-iphonesimulator/CPPGameEngineMobile.app
xcrun simctl launch booted com.cppgameengine.mobile
```

The script sources the SDK's `iOS/setup-env.sh`, compiles shaders, clones the
same pinned SDL source, and builds an unsigned iOS Simulator bundle. A device
build needs a signing team and `iphoneos` configuration. The bundle embeds the
Vulkan loader/MoltenVK frameworks and Vulkan manifest resources. A Simulator
build is a compile gate; actual Vulkan presentation depends on the simulator
and installed SDK. Hardware runtime remains to be verified.

The project uses the following primary platform references:

- [SDL3 Android integration and APK assets](https://wiki.libsdl.org/SDL3/README-android)
- [SDL3 base paths and bundle resources](https://wiki.libsdl.org/SDL3/SDL_GetBasePath)
- [SDL3 iOS framework and entry point](https://wiki.libsdl.org/SDL3/README-ios)
- [SDL3 Vulkan surface creation](https://wiki.libsdl.org/SDL3/SDL_Vulkan_CreateSurface)
- [SDL3 lifecycle events](https://wiki.libsdl.org/SDL3/SDL_EventType)
- [Android Vulkan API availability](https://developer.android.com/ndk/guides/stable_apis)
- [Khronos Vulkan portability enumeration](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_portability_enumeration.html)
- [Khronos iOS build requirements](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/docs/build.adoc)
- [CMake Vulkan/MoltenVK discovery](https://cmake.org/cmake/help/latest/module/FindVulkan.html)
