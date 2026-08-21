cmake_minimum_required(VERSION 3.16)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ble_lock_stage1_s3 VERSION 1.0.0)

add_custom_target(app_build_number ALL
    COMMAND ${CMAKE_COMMAND}
        -DNUMBER_FILE=${CMAKE_SOURCE_DIR}/BUILD_NUMBER
        -DHEADER_FILE=${CMAKE_BINARY_DIR}/generated/app_build_number.h
        -P ${CMAKE_SOURCE_DIR}/tools/update_build_number.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/app_build_number.h
    VERBATIM
)
