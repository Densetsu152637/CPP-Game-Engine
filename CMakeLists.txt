cmake_minimum_required(VERSION 3.25)

project(CPPGameEngine
    VERSION 0.1.0
    DESCRIPTION "A Vulkan-based C++ game engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPP_GAME_ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CPP_GAME_ENGINE_ENABLE_AVX2 "Enable AVX2 SIMD paths for supported vector operations" OFF)

find_package(Vulkan REQUIRED)

add_executable(CPPGameEngine

        src/main.cpp
        src/structs/vector.cpp
        src/structs/vector.h
        src/structs/arraylist.cpp
        src/structs/arraylist.h
        src/functional/result.h
        src/structs/smartstring.cpp
        src/structs/smartstring.h
        src/structs/string_search.cpp
        src/structs/string_search.h
        src/util/stopwatch.h
        src/async/threadpool.cpp
        src/async/threadpool.h
        src/async/promise.cpp
        src/async/promise.h
        src/async/lock/syncronized.h
        src/structs/queue.cpp
        src/structs/queue.h
        src/async/lock/atomic.h
        src/functional/functions.h
        src/async/lock/waiter.h
        src/async/lock/notifier.h
        src/async/thread_timer.h
        src/async/buffer/double_buffer.h
        src/async/buffer/triple_buffer.h
        src/util/fader.h
        src/util/toolbox.cpp
        src/util/toolbox.h

)

target_link_libraries(CPPGameEngine
    PRIVATE Vulkan::Vulkan
)

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
