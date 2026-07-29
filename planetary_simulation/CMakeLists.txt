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
