idf_component_register(
    SRCS
        "main.c"
        "ap_mode.c"
        "app_config.c"
        "event_log.c"
        "led.c"
        "lock_control.c"
        "ulp_controller.c"
        "ble_phone.c"
        "wifi_ota.c"
        "web_ui.c"
    INCLUDE_DIRS "." "ulp" "${CMAKE_BINARY_DIR}/generated"
    REQUIRES
        bt
        nvs_flash
        esp_timer
        esp_adc
        ulp
        soc
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp_https_ota
        esp_http_server
    PRIV_REQUIRES
        esp_driver_gpio
        led_strip
        driver
        mbedtls
        app_update
)

add_dependencies(${COMPONENT_LIB} app_build_number)

set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_riscv_sources "ulp/main.c")
set(ulp_exp_dep_srcs "ulp_controller.c")
ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")
