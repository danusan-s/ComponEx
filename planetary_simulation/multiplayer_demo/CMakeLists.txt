cmake_minimum_required(VERSION 3.25)
project(MultiplayerDemo)

set(COMPONENG_NETWORKING ON CACHE BOOL "Enable ComponEng networking library" FORCE)

set(EXCLUDE_EXAMPLE ON CACHE BOOL "Exclude ComponEng example projects" FORCE)
add_subdirectory(engine)

file(GLOB MULTIPLAYER_DEMO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(MultiplayerDemo ${MULTIPLAYER_DEMO_SOURCES})


target_include_directories(MultiplayerDemo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ComponEngNet is the engine's optional networking library; ComponEngNetNat adds
# STUN and invite codes on top (it brings ComponEngNet and ComponEng along). The
# MultiplayerDemo enables COMPONENG_NETWORKING for this project.
target_link_libraries(MultiplayerDemo
  PRIVATE ComponEngNetNat
)
