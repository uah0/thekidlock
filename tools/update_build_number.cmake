if(NOT DEFINED NUMBER_FILE OR NOT DEFINED HEADER_FILE)
    message(FATAL_ERROR "NUMBER_FILE and HEADER_FILE are required")
endif()

if(EXISTS "${NUMBER_FILE}")
    file(READ "${NUMBER_FILE}" current_number)
    string(STRIP "${current_number}" current_number)
else()
    set(current_number "0")
endif()

if(NOT current_number MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Invalid build number: ${current_number}")
endif()

math(EXPR next_number "${current_number} + 1")
string(TIMESTAMP build_timestamp "%Y-%m-%d %H:%M:%S")
file(WRITE "${NUMBER_FILE}" "${next_number}\n")
get_filename_component(header_directory "${HEADER_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${header_directory}")
file(WRITE "${HEADER_FILE}"
    "#pragma once\n#define APP_BUILD_NUMBER_STRING \"${next_number}\"\n#define APP_BUILD_TIMESTAMP \"${build_timestamp}\"\n")
message(STATUS "ESP32 firmware build number: ${next_number}")
