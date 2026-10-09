set(ota_key "${PROJECT_DIR}/.security/ota_decryption_key.pem")
if(NOT EXISTS "${ota_key}")
    message(FATAL_ERROR "Missing private OTA decryption key: ${ota_key}")
endif()
idf_component_register(SRCS "ota_decrypt.c" INCLUDE_DIRS "include"
    REQUIRES esp_encrypted_img esp_https_ota esp_partition)
configure_file("${ota_key}" "${CMAKE_CURRENT_BINARY_DIR}/ota_decryption_key.pem" COPYONLY)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_BINARY_DIR}/ota_decryption_key.pem" TEXT)
