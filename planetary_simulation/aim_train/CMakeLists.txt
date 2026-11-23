cmake_minimum_required(VERSION 3.25)
project(AimTrain)

set(COMPONENG_NETWORKING OFF CACHE BOOL "Disable ComponEng networking library" FORCE)

set(EXCLUDE_EXAMPLE ON CACHE BOOL "Exclude ComponEng example projects" FORCE)
add_subdirectory(engine)

file(GLOB AIM_TRAIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(AimTrain ${AIM_TRAIN_SOURCES})

target_include_directories(AimTrain
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(AimTrain
  PRIVATE ComponEng
)
