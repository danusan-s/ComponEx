cmake_minimum_required(VERSION 3.25)

file(GLOB BOUNCING_HELL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(Demo ${BOUNCING_HELL_SOURCES})

target_include_directories(Demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Demo
  PRIVATE ComponEng
)
