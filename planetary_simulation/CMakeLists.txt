cmake_minimum_required(VERSION 3.25)
project(PlanetarySim)

set(COMPONENG_NETWORKING OFF CACHE BOOL "Disable ComponEng networking library" FORCE)

set(EXCLUDE_EXAMPLE ON CACHE BOOL "Exclude ComponEng example projects" FORCE)
add_subdirectory(engine)

file(GLOB PLANETARY_SIM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(PlanetarySim ${PLANETARY_SIM_SOURCES})

target_include_directories(PlanetarySim
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlanetarySim
  PRIVATE ComponEng
)
