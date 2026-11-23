cmake_minimum_required(VERSION 3.25)
project(ComponEng)

set(CMAKE_CXX_STANDARD 17)

# compile_commands.json, needed by clang-tidy and clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Renderer backend selection: "opengl" or "vulkan"
set(RENDERER_BACKEND "opengl" CACHE STRING "Renderer backend to use (opengl or vulkan)")
set_property(CACHE RENDERER_BACKEND PROPERTY STRINGS "opengl" "vulkan")

# Find dependencies
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

# Split first-party from vendored so warning flags can target only our code
file(GLOB ENGINE_FIRST_PARTY_SOURCES
    "src/componeng/*.cpp"
    "src/componeng/*.c"
    "src/componeng/core/*.cpp"
    "src/componeng/renderer/*.cpp"
    "src/componeng/renderer/asset/*.cpp"
    "src/componeng/renderer/backend/opengl/*.cpp"
    "src/componeng/renderer/batching/*.cpp"
    "src/componeng/renderer/culling/*.cpp"
    "src/componeng/renderer/draw/*.cpp"
    "src/componeng/renderer/lod/*.cpp"
    "src/componeng/renderer/pass/*.cpp"
    "src/componeng/renderer/pipeline/*.cpp"
    "src/componeng/audio/*.cpp"
    "src/componeng/input/*.cpp"
    "src/componeng/camera/*.cpp"
    "src/componeng/physics/*.cpp"
    "src/componeng/ecs/*.cpp"
    "src/componeng/utils/*.cpp"
)
# Two independent switches for what a build shows and measures. ImGui is always
# built: with the debug panels off it still draws a small FPS window.
#
# COMPONENG_DEBUG_UI: the debug panels (camera info, anything a game adds
#   through DebugUI). OFF turns those calls into empty inlines and leaves only
#   the FPS window.
# COMPONENG_PROFILING (default OFF): measurement. GPU timer queries (frame, render pass,
#   shader), per-pass and per-system CPU timings, and the timing panes that
#   show them. OFF compiles all of that out, for a benchmark or a profile that
#   should not pay for its own measurement.
option(COMPONENG_DEBUG_UI "Show the ImGui debug panels (off: only the FPS window)" ON)
option(COMPONENG_PROFILING "Measure GPU/CPU timings and show the timing panes" OFF)
# COMPONENG_NETWORKING: build the ComponEngNet library (peer-to-peer networking).
#   The engine does not depend on it: a game links ComponEngNet and calls
#   net::installNetworking(world). OFF skips building it and its tests.
# COMPONENG_MARCH: CPU target for Release builds. "native" is fastest on the
#   build machine but the binary may crash elsewhere; use x86-64-v3 for a binary
#   you hand to someone else (any x86-64 CPU from about 2015 on).
set(COMPONENG_MARCH "native" CACHE STRING "-march value for Release builds")
# COMPONENG_WERROR: treat warnings in engine code as errors.
option(COMPONENG_WERROR "Treat engine warnings as errors" ON)
if(COMPONENG_WERROR)
    set(COMPONENG_WARNING_FLAGS -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Werror)
else()
    set(COMPONENG_WARNING_FLAGS -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual)
endif()
option(COMPONENG_NETWORKING "Build the optional networking library (ComponEngNet)" OFF)

file(GLOB ENGINE_THIRD_PARTY_SOURCES
    "src/third_party/glad/*.c"
    "src/third_party/imgui/*.cpp"
)

if(RENDERER_BACKEND STREQUAL "vulkan")
    # Device, resources and the ImGui Vulkan backend (already in the third-party glob).
    file(GLOB VULKAN_BACKEND_SOURCES "src/componeng/renderer/backend/vulkan/*.cpp")
    list(APPEND ENGINE_FIRST_PARTY_SOURCES ${VULKAN_BACKEND_SOURCES})
elseif(NOT RENDERER_BACKEND STREQUAL "opengl")
    message(FATAL_ERROR "RENDERER_BACKEND must be 'opengl' or 'vulkan', got '${RENDERER_BACKEND}'")
else()
    list(FILTER ENGINE_THIRD_PARTY_SOURCES EXCLUDE REGEX "imgui_impl_vulkan\\.cpp$")
endif()

# Remove main.cpp if it exists in src/
list(FILTER ENGINE_FIRST_PARTY_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

set(ENGINE_SOURCES ${ENGINE_FIRST_PARTY_SOURCES} ${ENGINE_THIRD_PARTY_SOURCES})

# Create STATIC library instead of executable
add_library(ComponEng STATIC ${ENGINE_SOURCES})

# PUBLIC so games that include debug_ui.hpp / profiling.hpp see the same
# settings the library was built with.
if (NOT COMPONENG_DEBUG_UI)
  message(STATUS "Debug panels disabled (COMPONENG_DEBUG_UI=OFF): FPS window only")
  target_compile_definitions(ComponEng PUBLIC COMPONENG_DISABLE_DEBUG_UI)
endif()
if (NOT COMPONENG_PROFILING)
  message(STATUS "Profiling disabled (COMPONENG_PROFILING=OFF)")
  target_compile_definitions(ComponEng PUBLIC COMPONENG_DISABLE_PROFILING)
endif()

# Include directories (PUBLIC so game can access them)
target_include_directories(ComponEng
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/include/third_party/imgui
)

target_compile_options(ComponEng PUBLIC
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=${COMPONENG_MARCH}>
)
target_link_options(ComponEng PUBLIC
    $<$<CONFIG:Debug>:-fsanitize=address>
)

# PRIVATE: don't force these diagnostics onto example/ and tests
# -Werror: the engine builds warning-clean, so keep it that way
target_compile_options(ComponEng PRIVATE
    ${COMPONENG_WARNING_FLAGS}
)
# Vendored code isn't ours to fix; silence it so it can't drown out our warnings
set_source_files_properties(${ENGINE_THIRD_PARTY_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w"
)

# Link dependencies
target_link_libraries(ComponEng
    PUBLIC
        glfw
        OpenGL::GL
)

# GLFW must not pull in the system GL header: glad provides the GL API, and only
# the OpenGL backend includes it. Public so games that include GLFW agree.
target_compile_definitions(ComponEng PUBLIC GLFW_INCLUDE_NONE)

# windows.h (pulled in by miniaudio) defines min/max macros that break std::min
# and std::max.
if(WIN32)
    target_compile_definitions(ComponEng PUBLIC NOMINMAX)
endif()

# Vulkan backend (-DRENDERER_BACKEND=vulkan). Needs the Vulkan loader and
# glslang: GLSL is compiled to SPIR-V at load time, so there is no offline
# shader step.
if(RENDERER_BACKEND STREQUAL "vulkan")
    find_package(Vulkan REQUIRED)
    find_package(glslang CONFIG REQUIRED)

    target_link_libraries(ComponEng
        PUBLIC
            Vulkan::Vulkan
        PRIVATE
            glslang::glslang
            glslang::SPIRV
            glslang::glslang-default-resource-limits
    )
    target_compile_definitions(ComponEng PUBLIC COMPONENG_VULKAN)
    # `VkFooInfo info{sType};` is the Vulkan idiom; -Wextra flags the rest.
    set(VULKAN_VMA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/componeng/renderer/backend/vulkan/vma_impl.cpp")
    set(VULKAN_FIRST_PARTY_SOURCES ${VULKAN_BACKEND_SOURCES})
    list(REMOVE_ITEM VULKAN_FIRST_PARTY_SOURCES ${VULKAN_VMA_SOURCE})
    set_source_files_properties(${VULKAN_FIRST_PARTY_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wno-missing-field-initializers")
    # Vulkan Memory Allocator is vendored: it is only ever included here.
    set_source_files_properties(${VULKAN_VMA_SOURCE} PROPERTIES COMPILE_OPTIONS "-w")
    message(STATUS "Renderer backend: Vulkan")
else()
    message(STATUS "Renderer backend: OpenGL")
endif()

# Networking is a library of its own; the engine does not link it. A game opts
# in with target_link_libraries(Game ComponEngNet), which brings ComponEng along
# (the ECS glue needs the World). Internet play (STUN, invite codes) is a second
# opt-in on top: ComponEngNetNat.
if(COMPONENG_NETWORKING)
    file(GLOB NET_SOURCES "src/componeng/net/*.cpp")
    add_library(ComponEngNet STATIC ${NET_SOURCES})
    target_link_libraries(ComponEngNet PUBLIC ComponEng)
    target_compile_options(ComponEngNet PRIVATE
        ${COMPONENG_WARNING_FLAGS}
    )
    # Winsock is a separate library on Windows; POSIX sockets are part of libc.
    if(WIN32)
        target_link_libraries(ComponEngNet PUBLIC ws2_32)
    endif()

    # STUN, NAT detection and invite codes: a game that only plays on a LAN
    # leaves this one out.
    file(GLOB NAT_SOURCES "src/componeng/net/nat/*.cpp")
    add_library(ComponEngNetNat STATIC ${NAT_SOURCES})
    target_link_libraries(ComponEngNetNat PUBLIC ComponEngNet)
    target_compile_options(ComponEngNetNat PRIVATE
        ${COMPONENG_WARNING_FLAGS}
    )
    message(STATUS "Networking: ComponEngNet and ComponEngNetNat enabled")
else()
    message(STATUS "Networking disabled (COMPONENG_NETWORKING=OFF)")
endif()

if (EXCLUDE_EXAMPLE)
  message(STATUS "Excluding example executables")
else()
  add_subdirectory(example)
endif()


# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test executables - one per test file
enable_testing()

set(TEST_SOURCES
    tests/test_entity_manager.cpp
    tests/test_component_registry.cpp
    tests/test_archetype.cpp
    tests/test_query.cpp
    tests/test_collision.cpp
    tests/test_broad_phase.cpp
    tests/test_serialization.cpp
    tests/test_math.cpp
    tests/test_instance_data.cpp
    tests/test_transform_matrix_cache.cpp
    tests/test_render_pipeline.cpp
    tests/test_render_state_policy.cpp
    tests/test_culling.cpp
    tests/test_mesh.cpp
    tests/test_lod.cpp
    tests/test_procedural_instances.cpp
    tests/test_material_bindings.cpp
    tests/test_utils.cpp
    tests/test_world.cpp
    tests/test_audio.cpp
    tests/test_plugins.cpp
)

set(NET_TEST_SOURCES
    tests/test_net.cpp
    tests/test_transport.cpp
    tests/test_net_archive.cpp
    tests/test_network.cpp
    tests/test_lan_discovery.cpp
    tests/test_session.cpp
    tests/test_object_sync.cpp
)

set(NAT_TEST_SOURCES
    tests/test_stun.cpp
    tests/test_nat_traversal.cpp
    tests/test_nat_plugin.cpp
)

foreach(TEST_SRC ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} PRIVATE ComponEng gtest_main)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

if(COMPONENG_NETWORKING)
    foreach(TEST_SRC ${NET_TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_link_libraries(${TEST_NAME} PRIVATE ComponEngNet gtest_main)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()

    foreach(TEST_SRC ${NAT_TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_link_libraries(${TEST_NAME} PRIVATE ComponEngNetNat gtest_main)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()
