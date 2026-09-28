#pragma once
#include "ai_agent_http.h"
extern ai_agent_config_t ai_agent_config;
extern bool ai_agent_ready;
esp_err_t ai_agent_http_guard(httpd_req_t *req, const char *operation);
esp_err_t ai_agent_http_error(httpd_req_t *req, const char *status, const char *message);
esp_err_t ai_agent_http_json(httpd_req_t *req, const char *json);
