idf_component_register(
    SRCS
        "main.c"
        "ap_mode.c"
        "app_config.c"
        "event_log.c"
        "led.c"
        "lock_control.c"
        "ble_phone.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        nvs_flash
        esp_timer
    PRIV_REQUIRES
        esp_driver_gpio
        led_strip
)
