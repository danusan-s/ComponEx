cmake_minimum_required(VERSION 3.10)
project(ComponEx)

set(CMAKE_CXX_STANDARD 17)

set(EXCLUDE_EXAMPLE ON CACHE BOOL "Exclude engine-internal examples")
add_subdirectory(engine)

add_subdirectory(aim_train)
add_subdirectory(planetary_simulation)
add_subdirectory(forest)
# Needs the engine's optional networking library (COMPONENG_NETWORKING, default ON).
if(TARGET ComponEngNet)
    add_subdirectory(multiplayer_demo)
endif()
