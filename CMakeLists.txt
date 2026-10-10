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

add_custom_target(mqtt_broker_choices ALL
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/check_mqtt_brokers.py
        --input ${CMAKE_SOURCE_DIR}/main/web/adress_broker.txt
        --header ${CMAKE_BINARY_DIR}/generated/mqtt_brokers.h
        --report ${CMAKE_BINARY_DIR}/generated/mqtt_brokers_report.json
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/mqtt_brokers.h
    VERBATIM
)
