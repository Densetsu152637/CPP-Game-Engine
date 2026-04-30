cmake_minimum_required(VERSION 3.25)

project(CPPGameEngine
    VERSION 0.1.0
    DESCRIPTION "A Vulkan-based C++ game engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPP_GAME_ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CPP_GAME_ENGINE_ENABLE_AVX2 "Enable AVX2 SIMD paths for supported vector operations" OFF)

find_package(Vulkan QUIET)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target" FORCE)
add_subdirectory(glfw EXCLUDE_FROM_ALL)

add_executable(CPPGameEngine
        src/main.cpp
        src/structs/vector.cpp
        src/structs/vector.h
        src/structs/arraylist.h
        src/functional/result.h
        src/structs/smartstring.cpp
        src/structs/smartstring.h
        src/util/string_search.cpp
        src/util/string_search.h
        src/util/stopwatch.h
        src/async/threadpool.cpp
        src/async/threadpool.h
        src/async/promise.h
        src/structs/queue.h
        src/async/thread_timer.h
        src/util/fader.h
        src/util/toolbox.cpp
        src/util/toolbox.h
        src/logging/logger.cpp
        src/logging/logger.h
        src/logging/text_colour.h
        src/logging/text_colour.cpp
        src/logging/logger.cpp
        src/ecs/uniform.h
        src/interaction/keyboard.cpp
        src/interaction/keyboard.h
        src/interaction/mouse.cpp
        src/interaction/mouse.h
        src/interaction/window_event.h
        src/core/engine.cpp
        src/core/engine.h
        src/ecs/entity.cpp
        src/ecs/entity.h
        src/ecs/ecs.cpp
        src/ecs/ecs.h
        src/core/interfaces.h
        src/async/buffer.h
        src/async/lock.h
        src/runtime/glfw_main.h
        src/runtime/glfw_main.cpp
        src/core/display.cpp
        src/core/display.h
)

if(Vulkan_FOUND)
    message(STATUS "Vulkan found: building CPPGameEngine with Vulkan support")
    target_link_libraries(CPPGameEngine PRIVATE Vulkan::Vulkan)
    target_compile_definitions(CPPGameEngine PRIVATE CPP_GAME_ENGINE_USE_VULKAN=1)
else()
    message(STATUS "Vulkan not found: building CPPGameEngine as a plain C++ executable")
endif()

target_link_libraries(CPPGameEngine PRIVATE glfw)
target_compile_features(CPPGameEngine PRIVATE cxx_std_20)
target_include_directories(CPPGameEngine
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(CPPGameEngine
    PRIVATE $<$<CONFIG:Debug>:CPP_GAME_ENGINE_DEBUG=1>
)

if(MSVC)
    target_compile_options(CPPGameEngine PRIVATE /W4 /permissive- /EHsc)
    if(CPP_GAME_ENGINE_ENABLE_AVX2)
        target_compile_options(CPPGameEngine PRIVATE /arch:AVX2)
    endif()
    if(CPP_GAME_ENGINE_WARNINGS_AS_ERRORS)
        target_compile_options(CPPGameEngine PRIVATE /WX)
    endif()
else()
    target_compile_options(CPPGameEngine PRIVATE -Wall -Wextra -Wpedantic)
    if(CPP_GAME_ENGINE_ENABLE_AVX2)
        target_compile_options(CPPGameEngine PRIVATE -mavx2)
    endif()
    if(CPP_GAME_ENGINE_WARNINGS_AS_ERRORS)
        target_compile_options(CPPGameEngine PRIVATE -Werror)
    endif()
endif()

set_target_properties(CPPGameEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
