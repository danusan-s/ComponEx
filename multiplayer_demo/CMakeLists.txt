file(GLOB MULTIPLAYER_DEMO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(MultiplayerDemo ${MULTIPLAYER_DEMO_SOURCES})

target_include_directories(MultiplayerDemo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ComponEngNet is the engine's optional networking library; it brings ComponEng
# along. The demo needs a build with COMPONENG_NETWORKING=ON (the default).
target_link_libraries(MultiplayerDemo
  PRIVATE ComponEngNet
)
