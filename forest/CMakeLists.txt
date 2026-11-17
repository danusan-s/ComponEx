file(GLOB FOREST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# forest/dev/ (the FOREST_SHOT screenshot tool, kept out of the repo) is not
# built: it needs an engine frame-capture API that the engine no longer has.

add_executable(ForestDemo ${FOREST_SOURCES})

target_include_directories(ForestDemo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The demo's shaders, models and textures live here, not in the engine tree;
# main() registers this directory as an asset root.
target_compile_definitions(ForestDemo
    PRIVATE
        FOREST_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ForestDemo
  PRIVATE ComponEng
)
