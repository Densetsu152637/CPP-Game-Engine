cmake_minimum_required(VERSION 3.25)

project(CPPGameEngine
    VERSION 0.1.0
    DESCRIPTION "A Vulkan-based C++ game engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPP_GAME_ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CPP_GAME_ENGINE_ENABLE_AVX2 "Enable AVX2 SIMD paths for supported vector operations" OFF)
option(CPP_GAME_ENGINE_BUILD_TESTS "Build local engine test executables" ON)

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
        src/structs/sorted_array.h
        src/functional/result.h
        src/structs/smartstring.cpp
        src/structs/smartstring.h
        src/util/string_search.cpp
        src/util/string_search.h
        src/util/stopwatch.h
        src/async/threadpool.cpp
        src/async/threadpool.h
        src/async/promise.h
        src/structs/LinkedQueue.h
        src/async/thread_timer.h
        src/util/fader.h
        src/util/toolbox.cpp
        src/util/toolbox.h
        src/logging/logger.cpp
        src/logging/logger.h
        src/logging/text_colour.h
        src/logging/text_colour.cpp
        src/logging/logger.cpp
        src/rendering/uniform.h
        src/ecs/pools/component_pool.h
        src/interaction/keyboard.cpp
        src/interaction/keyboard.h
        src/interaction/mouse.cpp
        src/interaction/mouse.h
        src/interaction/window_event.h
        src/core/engine.cpp
        src/core/engine.h
        src/ecs/core/entity.h
        src/ecs/ecs.cpp
        src/ecs/ecs.h
        src/ecs/aliases/component_alias.h
        src/ecs/aliases/component_alias_basic.h
        src/ecs/aliases/component_alias_buffered.h
        src/ecs/aliases/component_alias_ops.h
        src/ecs/aliases/component_alias_traits.h
        src/ecs/pools/component_pool_dirty.h
        src/ecs/pools/component_pool_base.h
        src/ecs/pools/component_pool_simulation.h
        src/ecs/pools/component_pool_render.h
        src/ecs/pools/component_pool_storage.h
        src/ecs/pools/tag_pool.h
        src/ecs/core/archetype_storage_registry.h
        src/ecs/core/component_storage_registry.h
        src/ecs/core/component_type_id.h
        src/ecs/core/structural_command_buffer.h
        src/ecs/core/entity_registry.cpp
        src/ecs/core/entity_registry.h
        src/ecs/views/view.h
        src/ecs/views/view_cache.h
        src/ecs/views/view_execution.h
        src/ecs/views/view_matcher.h
        src/ecs/jobs/ecs_access_analyzer.h
        src/core/interfaces.h
        src/async/buffer.h
        src/async/lock.h
        src/runtime/glfw_main.h
        src/runtime/glfw_main.cpp
        src/core/display.cpp
        src/core/display.h
        src/structs/sparse_set.h
        src/structs/page_set.h
        src/structs/templates.h
        src/ecs/jobs/ecs_sim_detail.h
        src/ecs/jobs/ecs_sim_access.h
        src/ecs/jobs/ecs_sim_arg_traits.h
        src/ecs/jobs/ecs_sim_execution.h
        src/ecs/jobs/ecs_sim_invocation.h
        src/ecs/jobs/ecs_sim_types.h
        src/ecs/jobs/ecs_job_scheduler.cpp
        src/ecs/jobs/ecs_job_scheduler.h
        src/ecs/jobs/ecs_job_factory.h
        src/ecs/rendering/ecs_render_bridge.cpp
        src/ecs/rendering/ecs_render_bridge.h
        src/components/alias.h
        src/components/systems.cpp
        src/components/systems.h
        src/structs/packed_flags.cpp
        src/structs/packed_flags.h
        src/structs/array.h
        src/functional/result.cpp
        src/ecs/processor.cpp
        src/ecs/processor.h
        src/structs/sparse_bit_field.cpp
        src/structs/sparse_bit_field.h
)

if(Vulkan_FOUND)
    message(STATUS "Vulkan found: building CPPGameEngine with Vulkan support")
    target_link_libraries(CPPGameEngine PRIVATE Vulkan::Vulkan)
    target_compile_definitions(CPPGameEngine PRIVATE CPP_GAME_ENGINE_USE_VULKAN=1)
else()
    message(STATUS "Vulkan not found: building CPPGameEngine as a plain C++ executable")
endif()

target_link_libraries(CPPGameEngine PRIVATE glfw)
set_target_properties(CPPGameEngine PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_include_directories(CPPGameEngine
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(CPPGameEngine
    PRIVATE $<$<CONFIG:Debug>:CPP_GAME_ENGINE_DEBUG=1>
)

if(MSVC)
    target_compile_options(CPPGameEngine PRIVATE /W4 /permissive- /EHsc /wd4100 /wd4189)
    if(CPP_GAME_ENGINE_ENABLE_AVX2)
        target_compile_options(CPPGameEngine PRIVATE /arch:AVX2)
    endif()
    if(CPP_GAME_ENGINE_WARNINGS_AS_ERRORS)
        target_compile_options(CPPGameEngine PRIVATE /WX)
    endif()
else()
    target_compile_options(CPPGameEngine PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-variable)
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

if(CPP_GAME_ENGINE_BUILD_TESTS)
    add_executable(CPPGameEngineEcsTests
            src/test/ecs_tests.cpp
            src/ecs/ecs.cpp
            src/ecs/ecs.h
            src/ecs/processor.cpp
            src/ecs/processor.h
            src/ecs/aliases/component_alias.h
            src/ecs/aliases/component_alias_basic.h
            src/ecs/aliases/component_alias_buffered.h
            src/ecs/aliases/component_alias_ops.h
            src/ecs/aliases/component_alias_traits.h
            src/ecs/pools/component_pool.h
            src/ecs/pools/component_pool_dirty.h
            src/ecs/pools/component_pool_base.h
            src/ecs/pools/component_pool_simulation.h
            src/ecs/pools/component_pool_render.h
            src/ecs/pools/component_pool_storage.h
            src/ecs/pools/tag_pool.h
            src/ecs/core/archetype_storage_registry.h
            src/ecs/core/component_storage_registry.h
            src/ecs/core/component_type_id.h
            src/ecs/core/structural_command_buffer.h
            src/ecs/core/entity_registry.cpp
            src/ecs/core/entity_registry.h
            src/ecs/views/view.h
            src/ecs/views/view_cache.h
            src/ecs/views/view_execution.h
            src/ecs/views/view_matcher.h
            src/ecs/jobs/ecs_access_analyzer.h
            src/ecs/jobs/ecs_sim_detail.h
            src/ecs/jobs/ecs_sim_access.h
            src/ecs/jobs/ecs_sim_arg_traits.h
            src/ecs/jobs/ecs_sim_execution.h
            src/ecs/jobs/ecs_sim_invocation.h
            src/ecs/jobs/ecs_sim_types.h
            src/ecs/jobs/ecs_job_scheduler.cpp
            src/ecs/jobs/ecs_job_scheduler.h
            src/ecs/jobs/ecs_job_factory.h
            src/ecs/rendering/ecs_render_bridge.cpp
            src/ecs/rendering/ecs_render_bridge.h
            src/async/threadpool.cpp
            src/async/threadpool.h
            src/async/promise.h
            src/functional/result.cpp
            src/functional/result.h
            src/structs/sparse_bit_field.cpp
            src/structs/sparse_bit_field.h
            src/logging/logger.cpp
            src/logging/logger.h
            src/logging/text_colour.cpp
            src/logging/text_colour.h
            src/structs/smartstring.cpp
            src/structs/smartstring.h
            src/util/string_search.cpp
            src/util/string_search.h
    )

    target_include_directories(CPPGameEngineEcsTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    set_target_properties(CPPGameEngineEcsTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    if(MSVC)
        target_compile_options(CPPGameEngineEcsTests PRIVATE /W4 /permissive- /EHsc /wd4100 /wd4189)
    else()
        target_compile_options(CPPGameEngineEcsTests PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-variable)
    endif()
endif()
