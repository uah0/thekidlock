idf_component_register(
    SRCS
        "main.c"
        "ap_mode.c"
        "app_config.c"
        "mqtt_config.c"
        "cloud_link.c"
        "cloud_commands.c"
        "cloud_auth.c"
        "event_log.c"
        "remote_log.c"
        "led.c"
        "lock_control.c"
        "ulp_controller.c"
        "ble_phone.c"
        "wifi_ota.c"
        "web_ui.c"
        "lock_ai_adapter.c"
    INCLUDE_DIRS "." "ulp" "${CMAKE_BINARY_DIR}/generated"
    REQUIRES
        ota_decrypt
        ai_agent
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
        json
        mqtt
    PRIV_REQUIRES
        esp_driver_gpio
        efuse
        led_strip
        driver
        mbedtls
        app_update
    EMBED_TXTFILES
        "web/index.html"
        "web/calibration.html"
)

add_dependencies(${COMPONENT_LIB} app_build_number)

set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_riscv_sources "ulp/main.c")
set(ulp_exp_dep_srcs "ulp_controller.c")
ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")
