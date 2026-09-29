cmake_minimum_required(VERSION 3.22)
project(CPPGameEngineMobile LANGUAGES C CXX)

if(NOT ANDROID AND NOT IOS)
    message(FATAL_ERROR "The mobile target requires Android or iOS")
endif()

if(IOS AND CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR "iOS xcframework linkage requires CMake 3.28 or newer")
endif()
if(IOS AND (NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 16.3))
    message(FATAL_ERROR "iOS 16.3+ deployment target is required by libc++ floating-point std::to_chars")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED SDL3_SOURCE_DIR OR NOT EXISTS "${SDL3_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set SDL3_SOURCE_DIR to SDL release-3.4.16 source (run mobile/<platform>/build.sh)")
endif()
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
if(IOS)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory("${SDL3_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdl")
if(IOS)
    enable_language(OBJC OBJCXX)
    list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{VULKAN_SDK}")
    find_package(Vulkan REQUIRED COMPONENTS MoltenVK)
else()
    find_package(Vulkan REQUIRED)
endif()

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
# Compile the authored Project/Runtime path and its complete ECS, Lua, render,
# and Vulkan dependencies. The legacy desktop Engine/GLFW shell is a separate
# entry point and cannot be included in Android or iOS translation units.
set(ENGINE_SOURCES
    "${ROOT}/src/async/threadpool.cpp"
    "${ROOT}/src/ecs/ecs.cpp"
    "${ROOT}/src/ecs/processor.cpp"
    "${ROOT}/src/ecs/core/dynamic_component_storage.cpp"
    "${ROOT}/src/ecs/core/entity_registry.cpp"
    "${ROOT}/src/ecs/jobs/ecs_job_scheduler.cpp"
    "${ROOT}/src/ecs/rendering/ecs_render_bridge.cpp"
    "${ROOT}/src/functional/result.cpp"
    "${ROOT}/src/logging/logger.cpp"
    "${ROOT}/src/logging/text_colour.cpp"
    "${ROOT}/src/platform/mobile_main.cpp"
    "${ROOT}/src/platform/sdl_mobile_window.cpp"
    "${ROOT}/src/project/project.cpp"
    "${ROOT}/src/project/runtime.cpp"
    "${ROOT}/src/rendering/content.cpp"
    "${ROOT}/src/rendering/render_device.cpp"
    "${ROOT}/src/rendering/renderer.cpp"
    "${ROOT}/src/rendering/shader.cpp"
    "${ROOT}/src/rendering/uniform_registry.cpp"
    "${ROOT}/src/scripting/lua_script_system.cpp"
    "${ROOT}/src/structs/packed_flags.cpp"
    "${ROOT}/src/structs/smartstring.cpp"
    "${ROOT}/src/structs/sparse_bit_field.cpp"
    "${ROOT}/src/structs/vector.cpp"
    "${ROOT}/src/tooling/iteration.cpp"
    "${ROOT}/src/util/string_search.cpp"
    "${ROOT}/src/util/toolbox.cpp"
    "${ROOT}/src/vulcan/vulkan_frame_backend.cpp"
    "${ROOT}/src/vulcan/vulkan_memory.cpp"
    "${ROOT}/src/vulcan/vulkan_renderer.cpp"
    "${ROOT}/src/vulcan/vulkan_shader.cpp"
    "${ROOT}/src/vulcan/vulkan_surface_provider.cpp"
    "${ROOT}/src/vulcan/vulkan_swapchain.cpp")
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${ROOT}/third_party/lua/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|onelua|ltests)\\.c$")
list(APPEND ENGINE_SOURCES ${LUA_SOURCES})

if(ANDROID)
    add_library(main SHARED ${ENGINE_SOURCES})
else()
    add_executable(CPPGameEngineMobile MACOSX_BUNDLE ${ENGINE_SOURCES})
    set_target_properties(CPPGameEngineMobile PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.cppgameengine.mobile"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/ios/Info.plist"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
endif()

if(ANDROID)
    set(APP_TARGET main)
else()
    set(APP_TARGET CPPGameEngineMobile)
endif()
target_include_directories(${APP_TARGET} PRIVATE
    "${ROOT}/src"
    "${ROOT}/third_party/entt/src"
    "${ROOT}/third_party/lua"
    "${ROOT}/third_party/glfw/include")
target_compile_definitions(${APP_TARGET} PRIVATE
    CPP_GAME_ENGINE_USE_VULKAN=1 CPP_GAME_ENGINE_MOBILE=1 ENTT_USE_ATOMIC=1)
if(IOS)
    # Lua's iOS configuration replaces unavailable system() in loslib.c and
    # selects the supported POSIX/Darwin interfaces for its C library.
    target_compile_definitions(${APP_TARGET} PRIVATE LUA_USE_IOS=1)
endif()
target_link_libraries(${APP_TARGET} PRIVATE SDL3::SDL3 Vulkan::Vulkan)

if(ANDROID)
    target_link_libraries(${APP_TARGET} PRIVATE log android)
else()
    file(GLOB_RECURSE PROJECT_RESOURCES CONFIGURE_DEPENDS
        "${ROOT}/examples/first-project/*")
    foreach(RESOURCE IN LISTS PROJECT_RESOURCES)
        if(IS_DIRECTORY "${RESOURCE}")
            continue()
        endif()
        file(RELATIVE_PATH PROJECT_RELATIVE "${ROOT}/examples" "${RESOURCE}")
        target_sources(${APP_TARGET} PRIVATE "${RESOURCE}")
        get_filename_component(RESOURCE_DIR "${PROJECT_RELATIVE}" DIRECTORY)
        set_source_files_properties("${RESOURCE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/${RESOURCE_DIR}")
    endforeach()
    foreach(RESOURCE IN ITEMS
        "${ROOT}/mobile/build/shaders/mesh_textured.vert.spv"
        "${ROOT}/mobile/build/shaders/mesh_textured.frag.spv")
        target_sources(${APP_TARGET} PRIVATE "${RESOURCE}")
        set_source_files_properties("${RESOURCE}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/shaders")
    endforeach()
    if(NOT Vulkan_MoltenVK_LIBRARY)
        message(FATAL_ERROR "Vulkan SDK iOS MoltenVK was not found; source its iOS/setup-env.sh")
    endif()
    set_target_properties(${APP_TARGET} PROPERTIES
        XCODE_EMBED_FRAMEWORKS "${Vulkan_MoltenVK_LIBRARY};${Vulkan_LIBRARY}"
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY "NO")
    get_filename_component(VULKAN_TARGET_SDK "$ENV{VULKAN_SDK}/.." REALPATH)
    if(EXISTS "${VULKAN_TARGET_SDK}/iOS/share/vulkan")
        target_sources(${APP_TARGET} PRIVATE "${VULKAN_TARGET_SDK}/iOS/share/vulkan")
        set_source_files_properties("${VULKAN_TARGET_SDK}/iOS/share/vulkan"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    endif()
endif()

# Xcode UI automation drives actual touch and foreground transitions. The test
# bundle is separate from the app target so normal simulator builds stay small.
if(IOS)
    add_library(CPPGameEngineMobileUITests MODULE "${CMAKE_CURRENT_LIST_DIR}/ios/MobileUITests.m")
    set_target_properties(CPPGameEngineMobileUITests PROPERTIES
        BUNDLE TRUE
        XCTEST TRUE
        XCODE_PRODUCT_TYPE "com.apple.product-type.bundle.ui-testing"
        XCODE_ATTRIBUTE_TEST_TARGET_NAME "CPPGameEngineMobile"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.cppgameengine.mobile.uitests"
        XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "YES"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_GENERATE_SCHEME TRUE)
    target_link_libraries(CPPGameEngineMobileUITests PRIVATE "-framework XCTest" "-framework UIKit")
    add_dependencies(CPPGameEngineMobileUITests CPPGameEngineMobile)
endif()
