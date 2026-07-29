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
