#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define AI_AGENT_API_VERSION 1
#define AI_AGENT_MAX_COMMANDS 24
#define AI_AGENT_JSON_CAPACITY 8192
#define AI_AGENT_MAX_ARGS 8

typedef struct { const char *name; const char *value; } ai_agent_arg_t;
typedef struct { const ai_agent_arg_t *items; size_t count; } ai_agent_args_t;
typedef esp_err_t (*ai_agent_command_fn)(const ai_agent_args_t *args, void *ctx);
typedef esp_err_t (*ai_agent_json_fn)(char *out, size_t capacity, void *ctx);
typedef size_t (*ai_agent_log_fn)(uint64_t since, char *out, size_t capacity,
    uint64_t *next, uint64_t *oldest, bool *dropped, void *ctx);
typedef struct {
    const char *name;
    const char *description;
    ai_agent_command_fn execute;
    void *ctx;
} ai_agent_command_t;
typedef struct {
    const char *device_type, *device_name, *firmware_version, *device_id;
    const char *history_url, *build;
    /* Optional compatibility metadata: NULL-terminated string array. */
    const char *const *legacy_capabilities;
    ai_agent_json_fn status, events;
    ai_agent_log_fn monitor;
    void (*prepare_restart)(void *ctx);
    void (*ota_complete)(void *ctx);
    void *ctx;
} ai_agent_config_t;

/* Single instance. Init/register/seal on one startup task before publishing any
 * transport. Strings, contexts and providers must live for the entire app.
 * Config/command descriptors are copied; their pointed-to data is borrowed.
 * After seal the registry is immutable. Callbacks must be short, nonblocking,
 * thread-safe, and must not recursively call execute (returns INVALID_STATE).
 * Provider JSON must be NUL-terminated; return INVALID_SIZE on truncation. */
esp_err_t ai_agent_init(const ai_agent_config_t *config);
esp_err_t ai_agent_register_command(const ai_agent_command_t *command);
esp_err_t ai_agent_seal(void);
esp_err_t ai_agent_execute(const char *name, const ai_agent_args_t *args);
const char *ai_agent_arg(const ai_agent_args_t *args, const char *name);
esp_err_t ai_agent_read_status(char *out, size_t capacity);
esp_err_t ai_agent_read_events(char *out, size_t capacity);
esp_err_t ai_agent_info_json(char *out, size_t capacity);
esp_err_t ai_agent_capabilities_json(char *out, size_t capacity);
