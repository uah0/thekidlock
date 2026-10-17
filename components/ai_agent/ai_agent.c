#include "ai_agent.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "cJSON.h"
#include "esp_random.h"

ai_agent_config_t ai_agent_config;
bool ai_agent_ready;
static bool initialized;
static ai_agent_command_t commands[AI_AGENT_MAX_COMMANDS];
static size_t count;
static atomic_flag executing = ATOMIC_FLAG_INIT;
static char boot_id[17];

static bool bounded(const char *s, size_t max) { return s && strlen(s) <= max; }
esp_err_t ai_agent_init(const ai_agent_config_t *cfg)
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    if (!cfg || !bounded(cfg->device_type, 96) || !bounded(cfg->device_name, 96) ||
        !bounded(cfg->firmware_version, 96) || !bounded(cfg->device_id, 96) ||
        (cfg->history_url && !bounded(cfg->history_url, 512)) ||
        (cfg->build && !bounded(cfg->build, 96))) return ESP_ERR_INVALID_ARG;
    if (cfg->legacy_capabilities) {
        size_t n = 0;
        while (cfg->legacy_capabilities[n]) {
            if (n >= 24 || !bounded(cfg->legacy_capabilities[n], 64)) return ESP_ERR_INVALID_ARG;
            n++;
        }
    }
    ai_agent_config = *cfg;
    snprintf(boot_id, sizeof(boot_id), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    initialized = true;
    return ESP_OK;
}
esp_err_t ai_agent_register_command(const ai_agent_command_t *cmd)
{
    if (!initialized || ai_agent_ready) return ESP_ERR_INVALID_STATE;
    if (!cmd || !cmd->execute || !bounded(cmd->name, 48) || !cmd->name[0] ||
        !bounded(cmd->description, 160)) return ESP_ERR_INVALID_ARG;
    for (const char *p = cmd->name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; i++)
        if (!strcmp(commands[i].name, cmd->name)) return ESP_ERR_INVALID_STATE;
    if (count == AI_AGENT_MAX_COMMANDS) return ESP_ERR_NO_MEM;
    commands[count++] = *cmd;
    return ESP_OK;
}
esp_err_t ai_agent_seal(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (!ai_agent_ready) ai_agent_ready = true;
    return ESP_OK;
}
const char *ai_agent_arg(const ai_agent_args_t *args, const char *name)
{
    if (args && name) for (size_t i = 0; i < args->count; i++)
        if (!strcmp(args->items[i].name, name)) return args->items[i].value;
    return NULL;
}
esp_err_t ai_agent_execute(const char *name, const ai_agent_args_t *args)
{
    if (!ai_agent_ready) return ESP_ERR_INVALID_STATE;
    if (!name || (args && (args->count > AI_AGENT_MAX_ARGS ||
                          (args->count && !args->items)))) return ESP_ERR_INVALID_ARG;
    if (args) for (size_t i = 0; i < args->count; i++)
        if (!args->items[i].name || !args->items[i].value) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < count; i++) if (!strcmp(commands[i].name, name)) {
        if (atomic_flag_test_and_set(&executing)) return ESP_ERR_INVALID_STATE;
        esp_err_t err = commands[i].execute(args, commands[i].ctx);
        atomic_flag_clear(&executing);
        return err;
    }
    return ESP_ERR_NOT_FOUND;
}
static esp_err_t read_json(ai_agent_json_fn fn, char *out, size_t capacity)
{
    if (!ai_agent_ready) return ESP_ERR_INVALID_STATE;
    if (!out || capacity < 2) return ESP_ERR_INVALID_ARG;
    if (!fn) return ESP_ERR_NOT_SUPPORTED;
    memset(out, 0, capacity);
    esp_err_t err = fn(out, capacity, ai_agent_config.ctx);
    if (err == ESP_OK && !memchr(out, 0, capacity)) return ESP_ERR_INVALID_SIZE;
    return err;
}
esp_err_t ai_agent_read_status(char *out, size_t cap) { return read_json(ai_agent_config.status, out, cap); }
esp_err_t ai_agent_read_events(char *out, size_t cap) { return read_json(ai_agent_config.events, out, cap); }

static esp_err_t finish(cJSON *root, char *out, size_t cap, bool ok)
{
    esp_err_t err = !ok ? ESP_ERR_NO_MEM :
        cJSON_PrintPreallocated(root, out, (int)cap, false) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    cJSON_Delete(root);
    return err;
}
esp_err_t ai_agent_info_json(char *out, size_t cap)
{
    if (!ai_agent_ready) return ESP_ERR_INVALID_STATE;
    if (!out || cap < 2 || cap > AI_AGENT_JSON_CAPACITY) return ESP_ERR_INVALID_ARG;
    const ai_agent_config_t *c = &ai_agent_config;
    cJSON *r = cJSON_CreateObject();
    bool ok = r && cJSON_AddNumberToObject(r, "agent_api_version", AI_AGENT_API_VERSION) &&
        cJSON_AddNumberToObject(r, "api_version", AI_AGENT_API_VERSION) &&
        cJSON_AddStringToObject(r, "device_type", c->device_type) &&
        cJSON_AddStringToObject(r, "device_name", c->device_name) &&
        cJSON_AddStringToObject(r, "device_id", c->device_id) &&
        cJSON_AddStringToObject(r, "firmware_version", c->firmware_version) &&
        cJSON_AddStringToObject(r, "firmware", c->firmware_version) &&
        cJSON_AddStringToObject(r, "boot_id", boot_id);
    if (ok && c->build) ok = cJSON_AddStringToObject(r, "build", c->build) != NULL;
    if (ok && c->history_url) ok = cJSON_AddStringToObject(r, "history_url", c->history_url) != NULL;
    if (ok && c->legacy_capabilities) {
        cJSON *a = cJSON_AddArrayToObject(r, "capabilities");
        ok = a != NULL;
        for (size_t i = 0; ok && c->legacy_capabilities[i]; i++) {
            cJSON *v = cJSON_CreateString(c->legacy_capabilities[i]);
            ok = v && cJSON_AddItemToArray(a, v);
            if (!ok) cJSON_Delete(v);
        }
    }
    return finish(r, out, cap, ok);
}
esp_err_t ai_agent_capabilities_json(char *out, size_t cap)
{
    if (!ai_agent_ready) return ESP_ERR_INVALID_STATE;
    if (!out || cap < 2 || cap > AI_AGENT_JSON_CAPACITY) return ESP_ERR_INVALID_ARG;
    cJSON *r = cJSON_CreateObject();
    cJSON *f = r ? cJSON_AddObjectToObject(r, "features") : NULL;
    cJSON *a = r ? cJSON_AddArrayToObject(r, "commands") : NULL;
    bool ok = r && f && a && cJSON_AddNumberToObject(r, "api_version", AI_AGENT_API_VERSION) &&
        cJSON_AddBoolToObject(f, "status", ai_agent_config.status != NULL) &&
        cJSON_AddBoolToObject(f, "events", ai_agent_config.events != NULL) &&
        cJSON_AddBoolToObject(f, "monitor", ai_agent_config.monitor != NULL) &&
        cJSON_AddBoolToObject(f, "pulses", ai_agent_config.pulses != NULL) &&
        cJSON_AddBoolToObject(f, "restart", true) && cJSON_AddBoolToObject(f, "ota", true);
    for (size_t i = 0; ok && i < count; i++) {
        cJSON *cmd = cJSON_CreateObject();
        ok = cmd && cJSON_AddItemToArray(a, cmd);
        if (!ok) { cJSON_Delete(cmd); break; }
        ok = cJSON_AddStringToObject(cmd, "name", commands[i].name) &&
             cJSON_AddStringToObject(cmd, "description", commands[i].description);
    }
    return finish(r, out, cap, ok);
}
