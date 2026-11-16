file(GLOB FOREST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# Optional developer tooling (the FOREST_SHOT screenshot system, helper scripts)
# lives in forest/dev/, which is kept out of the repo. The demo builds without it.
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/dev)
    file(GLOB FOREST_DEV_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dev/*.cpp)
    list(APPEND FOREST_SOURCES ${FOREST_DEV_SOURCES})
endif()

add_executable(ForestDemo ${FOREST_SOURCES})

if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/dev)
    target_include_directories(ForestDemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dev)
    target_compile_definitions(ForestDemo PRIVATE FOREST_DEV_TOOLS)
endif()

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
