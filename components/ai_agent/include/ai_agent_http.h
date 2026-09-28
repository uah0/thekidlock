#pragma once
#include "ai_agent.h"
#include "esp_http_server.h"

/* Called before parsing/dispatch for ALL routes, including compatibility
 * aliases. Return ESP_OK to allow; otherwise transport sends JSON HTTP 403.
 * Install before registering routes. NULL means trusted LAN, no authentication.
 * Other transports must enforce their own identity/authorization boundary. */
typedef esp_err_t (*ai_agent_authorize_fn)(httpd_req_t *req,
                                         const char *operation, void *ctx);
esp_err_t ai_agent_set_http_authorizer(ai_agent_authorize_fn fn, void *ctx);
/* Registers eight routes in an existing server. Rolls back on failure.
 * Can be called again after the application's httpd has been stopped. */
esp_err_t ai_agent_register_http_handlers(httpd_handle_t server);
/* Optional legacy aliases must point here to share authorization and service. */
esp_err_t ai_agent_http_restart(httpd_req_t *req);
esp_err_t ai_agent_http_ota(httpd_req_t *req);
