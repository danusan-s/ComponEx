file(GLOB FOREST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

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
