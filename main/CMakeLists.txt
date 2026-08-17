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
    INCLUDE_DIRS "." "ulp"
    REQUIRES
        bt
        nvs_flash
        esp_timer
        esp_adc
        ulp
        soc
    PRIV_REQUIRES
        esp_driver_gpio
        led_strip
        driver
)

set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_riscv_sources "ulp/main.c")
set(ulp_exp_dep_srcs "ulp_controller.c")
ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")
