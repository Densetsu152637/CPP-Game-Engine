cmake_minimum_required(VERSION 3.22)
project(CPPGameEngineMobile LANGUAGES C CXX)

if(NOT ANDROID AND NOT IOS)
    message(FATAL_ERROR "The mobile target requires Android or iOS")
endif()

if(IOS AND CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR "iOS xcframework linkage requires CMake 3.28 or newer")
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
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${ROOT}/src/*.cpp"
    "${ROOT}/third_party/lua/*.c")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/test/")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/third_party/lua/(lua|onelua|ltests)\\.c$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/(main|commands)\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/core/display\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/interaction/(keyboard|mouse)\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/runtime/glfw_main\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/platform/platform_check\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/editor/")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/automation/")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/tooling/")
list(APPEND ENGINE_SOURCES "${ROOT}/src/tooling/iteration.cpp")

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
