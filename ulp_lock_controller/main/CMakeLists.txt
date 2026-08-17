idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "../ulp"
    REQUIRES soc ulp esp_adc esp_driver_gpio esp_timer driver
)

set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_riscv_sources "../ulp/main.c")
set(ulp_exp_dep_srcs "main.c")
ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")
