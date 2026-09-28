idf_component_register(SRCS "ai_agent.c" "ai_agent_http.c" "ai_agent_ota.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "."
    REQUIRES esp_http_server
    PRIV_REQUIRES app_update esp_system esp_hw_support freertos json)
