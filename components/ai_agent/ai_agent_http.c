#include "ai_agent_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static ai_agent_authorize_fn authorizer;
static void *auth_ctx;
esp_err_t ai_agent_set_http_authorizer(ai_agent_authorize_fn fn, void *ctx)
{
    if (ai_agent_ready) return ESP_ERR_INVALID_STATE;
    authorizer = fn;
    auth_ctx = ctx;
    return ESP_OK;
}
esp_err_t ai_agent_http_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}
esp_err_t ai_agent_http_error(httpd_req_t *req, const char *status, const char *message)
{
    /* Messages are component constants or esp_err_to_name, never user input. */
    char json[160];
    snprintf(json, sizeof(json), "{\"error\":\"%s\"}", message);
    httpd_resp_set_status(req, status);
    return ai_agent_http_json(req, json);
}
esp_err_t ai_agent_http_guard(httpd_req_t *req, const char *op)
{
    if (!ai_agent_ready) {
        ai_agent_http_error(req, "503 Service Unavailable", "Agent not ready");
        return ESP_ERR_INVALID_STATE;
    }
    if (authorizer && authorizer(req, op, auth_ctx) != ESP_OK) {
        ai_agent_http_error(req, "403 Forbidden", "Access denied");
        return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t document(httpd_req_t *req, const char *op,
                          esp_err_t (*read)(char *, size_t))
{
    if (ai_agent_http_guard(req, op) != ESP_OK) return ESP_OK;
    char *json = malloc(AI_AGENT_JSON_CAPACITY);
    if (!json) return ai_agent_http_error(req, "500 Internal Server Error", "No memory");
    esp_err_t err = read(json, AI_AGENT_JSON_CAPACITY);
    esp_err_t sent = err == ESP_OK ? ai_agent_http_json(req, json) :
        ai_agent_http_error(req, err == ESP_ERR_NOT_SUPPORTED ? "501 Not Implemented" :
            "500 Internal Server Error", esp_err_to_name(err));
    free(json);
    return sent;
}
static esp_err_t info(httpd_req_t *r) { return document(r, "info", ai_agent_info_json); }
static esp_err_t capabilities(httpd_req_t *r) { return document(r, "capabilities", ai_agent_capabilities_json); }
static esp_err_t status(httpd_req_t *r) { return document(r, "status", ai_agent_read_status); }
static esp_err_t events(httpd_req_t *r) { return document(r, "events", ai_agent_read_events); }

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '%') {
            if (!s[1] || !s[2] || hex(s[1]) < 0 || hex(s[2]) < 0) return false;
            int c = hex(s[1]) * 16 + hex(s[2]);
            if (!c) return false;
            *out++ = (char)c;
            s += 3;
        } else { *out++ = *s == '+' ? ' ' : *s; s++; }
    }
    *out = 0;
    return true;
}
static esp_err_t command(httpd_req_t *req)
{
    if (ai_agent_http_guard(req, "command") != ESP_OK) return ESP_OK;
    char query[1024];
    if (req->content_len || httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return ai_agent_http_error(req, "400 Bad Request", "Expected query action and optional arguments");
    ai_agent_arg_t items[AI_AGENT_MAX_ARGS];
    ai_agent_args_t args = {.items = items};
    char *name = NULL, *save = NULL;
    for (char *p = strtok_r(query, "&", &save); p; p = strtok_r(NULL, "&", &save)) {
        char *v = strchr(p, '=');
        if (!v) return ai_agent_http_error(req, "400 Bad Request", "Invalid argument");
        *v++ = 0;
        if (!decode(p) || !decode(v) || !*p)
            return ai_agent_http_error(req, "400 Bad Request", "Invalid encoding");
        if (!strcmp(p, "action")) {
            if (name) return ai_agent_http_error(req, "400 Bad Request", "Duplicate action");
            name = v;
        } else {
            if (args.count == AI_AGENT_MAX_ARGS || ai_agent_arg(&args, p))
                return ai_agent_http_error(req, "400 Bad Request", "Too many or duplicate arguments");
            items[args.count++] = (ai_agent_arg_t){p, v};
        }
    }
    if (!name || !*name) return ai_agent_http_error(req, "400 Bad Request", "Missing action");
    esp_err_t err = ai_agent_execute(name, &args);
    if (err != ESP_OK) return ai_agent_http_error(req,
        err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_ARG ? "400 Bad Request" :
        err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT ? "503 Service Unavailable" :
        "500 Internal Server Error", err == ESP_ERR_NOT_FOUND ? "Unknown action" : esp_err_to_name(err));
    char json[128];
    /* Successful names came from the validated registry (ASCII identifiers). */
    snprintf(json, sizeof(json), "{\"accepted\":true,\"action\":\"%s\"}", name);
    return ai_agent_http_json(req, json);
}
static esp_err_t monitor(httpd_req_t *req)
{
    if (ai_agent_http_guard(req, "monitor") != ESP_OK) return ESP_OK;
    if (!ai_agent_config.monitor) return ai_agent_http_error(req, "501 Not Implemented", "No monitor provider");
    uint64_t since = 0;
    char query[64] = {0}, value[32] = {0};
    if (httpd_req_get_url_query_len(req)) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            httpd_query_key_value(query, "since", value, sizeof(value)) != ESP_OK)
            return ai_agent_http_error(req, "400 Bad Request", "Invalid cursor");
        char *end;
        errno = 0;
        since = strtoull(value, &end, 10);
        if (errno || !*value || *end || value[0] == '-')
            return ai_agent_http_error(req, "400 Bad Request", "Invalid cursor");
    }
    const size_t capacity = 16 * 1024;
    char *text = malloc(capacity);
    if (!text) return ai_agent_http_error(req, "500 Internal Server Error", "No memory");
    uint64_t next = 0, oldest = 0;
    bool dropped = false;
    size_t length = ai_agent_config.monitor(since, text, capacity, &next, &oldest,
                                            &dropped, ai_agent_config.ctx);
    if (length > capacity) {
        free(text);
        return ai_agent_http_error(req, "500 Internal Server Error", "Invalid provider length");
    }
    char cursor[32], oldest_text[32];
    snprintf(cursor, sizeof(cursor), "%llu", (unsigned long long)next);
    snprintf(oldest_text, sizeof(oldest_text), "%llu", (unsigned long long)oldest);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Log-Cursor", cursor);
    httpd_resp_set_hdr(req, "X-Log-Oldest", oldest_text);
    httpd_resp_set_hdr(req, "X-Log-Dropped", dropped ? "1" : "0");
    esp_err_t err = httpd_resp_send(req, text, length);
    free(text);
    return err;
}
static esp_err_t pulses(httpd_req_t *req)
{
    if (ai_agent_http_guard(req, "pulses") != ESP_OK) return ESP_OK;
    if (!ai_agent_config.pulses) return ai_agent_http_error(req, "501 Not Implemented", "No pulse provider");
    uint64_t since = 0;
    char query[64] = {0}, value[32] = {0};
    if (httpd_req_get_url_query_len(req) &&
        (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
         httpd_query_key_value(query, "since", value, sizeof(value)) != ESP_OK))
        return ai_agent_http_error(req, "400 Bad Request", "Invalid cursor");
    if (value[0]) {
        char *end;
        errno = 0;
        since = strtoull(value, &end, 10);
        if (errno || *end || value[0] == '-')
            return ai_agent_http_error(req, "400 Bad Request", "Invalid cursor");
    }
    const size_t capacity = 16 * 1024;
    char *text = malloc(capacity);
    if (!text) return ai_agent_http_error(req, "500 Internal Server Error", "No memory");
    uint64_t next = 0, oldest = 0;
    bool dropped = false;
    size_t length = ai_agent_config.pulses(since, text, capacity, &next, &oldest,
                                           &dropped, ai_agent_config.ctx);
    if (length > capacity) {
        free(text);
        return ai_agent_http_error(req, "500 Internal Server Error", "Invalid provider length");
    }
    char cursor[32], oldest_text[32];
    snprintf(cursor, sizeof(cursor), "%llu", (unsigned long long)next);
    snprintf(oldest_text, sizeof(oldest_text), "%llu", (unsigned long long)oldest);
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Pulse-Cursor", cursor);
    httpd_resp_set_hdr(req, "X-Pulse-Oldest", oldest_text);
    httpd_resp_set_hdr(req, "X-Pulse-Dropped", dropped ? "1" : "0");
    esp_err_t err = httpd_resp_send(req, text, length);
    free(text);
    return err;
}
esp_err_t ai_agent_register_http_handlers(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ai_agent_seal();
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri="/api/agent/info", .method=HTTP_GET, .handler=info},
        {.uri="/api/agent/capabilities", .method=HTTP_GET, .handler=capabilities},
        {.uri="/api/agent/status", .method=HTTP_GET, .handler=status},
        {.uri="/api/agent/events", .method=HTTP_GET, .handler=events},
        {.uri="/api/agent/monitor", .method=HTTP_GET, .handler=monitor},
        {.uri="/api/agent/pulses", .method=HTTP_GET, .handler=pulses},
        {.uri="/api/agent/command", .method=HTTP_POST, .handler=command},
        {.uri="/api/agent/restart", .method=HTTP_POST, .handler=ai_agent_http_restart},
        {.uri="/api/agent/ota", .method=HTTP_POST, .handler=ai_agent_http_ota},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            while (i) { --i; httpd_unregister_uri_handler(server, routes[i].uri, routes[i].method); }
            return err;
        }
    }
    return ESP_OK;
}
