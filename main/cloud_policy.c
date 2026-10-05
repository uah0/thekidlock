#include "cloud_policy.h"

#include <ctype.h>
#include <string.h>

#define MAX_COMMAND_ID UINT64_C(9007199254740991)

static bool decimal(const char *value, uint64_t *out)
{
    if (!value || !*value || strlen(value) > 16 || (value[0] == '0' && value[1])) return false;
    uint64_t result = 0;
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        result = result * 10 + (unsigned)(*p - '0');
        if (result > MAX_COMMAND_ID) return false;
    }
    *out = result; return true;
}

bool cloud_command_parse(const char *payload, cloud_command_t *out)
{
    if (!payload || !out || strlen(payload) > 1024 || strstr(payload, "\\u0000")) return false;
    const char *end = NULL;
    cJSON *json = cJSON_ParseWithOpts(payload, &end, true);
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); return false; }
    const char *fields[6] = {0};
    bool valid = true;
    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        for (unsigned i = 0; i < 6; ++i) {
            char key[] = "field1"; key[5] = '1' + i;
            if (item->string && !strcmp(item->string, key)) {
                if (fields[i] || !cJSON_IsString(item)) valid = false;
                else fields[i] = item->valuestring;
            }
        }
    }
    for (unsigned i = 0; i < 6; ++i) if (!fields[i]) valid = false;
    cloud_command_t command = {0};
    uint64_t issued = 0, expires = 0;
    if (valid) valid = decimal(fields[1], &command.id) && command.id &&
                       decimal(fields[3], &expires) && decimal(fields[4], &issued);
    if (valid) {
        command.issued = (int64_t)issued; command.expires = (int64_t)expires;
        valid = strlen(fields[0]) == 17 && strlen(fields[5]) > 0 && strlen(fields[5]) <= 17;
        for (unsigned i = 0; valid && i < 17; ++i) {
            bool separator = i % 3 == 2;
            valid = separator ? fields[0][i] == ':' : isxdigit((unsigned char)fields[0][i]) != 0;
            command.target[i] = (char)tolower((unsigned char)fields[0][i]);
        }
        for (const char *p = fields[5]; valid && *p; ++p)
            valid = isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == '.';
        if (!strcmp(fields[2], "open")) command.operation = CLOUD_OPEN;
        else if (!strcmp(fields[2], "close")) command.operation = CLOUD_CLOSE;
        else if (!strcmp(fields[2], "hold_on")) command.operation = CLOUD_HOLD_ON;
        else if (!strcmp(fields[2], "hold_off")) command.operation = CLOUD_HOLD_OFF;
        else valid = false;
        if (valid) strcpy(command.actor, fields[5]);
    }
    cJSON_Delete(json);
    if (valid) *out = command;
    return valid;
}

bool cloud_command_valid(const cloud_command_t *command, const char *mac, uint64_t last_id,
                         int64_t now, bool synced)
{
    return command && mac && synced && command->id > last_id && command->id <= MAX_COMMAND_ID &&
           !strcmp(command->target, mac) && (unsigned)command->operation <= CLOUD_HOLD_OFF &&
           command->issued >= now - 60 && command->issued <= now + 5 &&
           command->expires > now && command->expires > command->issued &&
           command->expires - command->issued <= 60;
}

/* These tests exercise the deployed parser/policy without touching lock outputs. */
bool cloud_policy_selftest(void)
{
    const char *mac = "a0:f2:62:f1:14:b8";
    cloud_command_t command;
    const char *valid = "{\"field1\":\"A0:F2:62:F1:14:B8\",\"field2\":\"123\",\"field3\":\"open\","
                        "\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner-1\"}";
    if (!cloud_command_parse(valid, &command) || !cloud_command_valid(&command, mac, 122, 1000, true)) return false;
    if (cloud_command_valid(&command, mac, 123, 1000, true) ||
        cloud_command_valid(&command, mac, 124, 1000, true) ||
        cloud_command_valid(&command, mac, 0, 1040, true) ||
        cloud_command_valid(&command, mac, 0, 1000, false) ||
        cloud_command_valid(&command, "00:00:00:00:00:00", 0, 1000, true) ||
        cloud_command_valid(&command, mac, 0, 900, true)) return false;
    cloud_command_t altered = command;
    altered.expires = 1061;
    if (cloud_command_valid(&altered, mac, 0, 1000, true)) return false;
    altered = command; altered.issued = 1006;
    if (cloud_command_valid(&altered, mac, 0, 1000, true)) return false;
    altered = command; altered.issued = 939;
    if (cloud_command_valid(&altered, mac, 0, 1000, true)) return false;
    const char *bad[] = {
        "{}", "[]", "null", "{\"field1\":\"a0:f2:62:f1:14:b8\"}", "{}", "garbage",
        "{\"field1\":\"a0:f2:62:f1:14:b8\",\"field2\":\"123\",\"field2\":\"124\",\"field3\":\"open\","
        "\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner\"}",
        "{\"field1\":\"a0:f2:62:f1:14:b8\",\"field2\":\"-1\",\"field3\":\"open\","
        "\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner\"}",
        "{\"field1\":\"a0:f2:62:f1:14:b8\",\"field2\":\"9007199254740992\",\"field3\":\"open\","
        "\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner\"}",
        "{\"field1\":\"a0:f2:62:f1:14:b8\",\"field2\":\"123\",\"field3\":\"toggle\","
        "\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner\"}",
        "{\"field1\":\"a0:f2:62:f1:14:b8\",\"field2\":123,\"field3\":\"open\","
        "\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner\"}",
        "{\"field1\":\"a0:f2:62:f1:14:b8\",\"field2\":\"123\",\"field3\":\"open\","
        "\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner&field2=9\"}"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        if (cloud_command_parse(bad[i], &altered)) return false;
    if (cloud_command_parse("{\"field1\":\"a0:f2:62:f1:14:b8\",\"field2\":\"123\","
        "\"field3\":\"open\\u0000ignored\",\"field4\":\"1040\",\"field5\":\"1000\",\"field6\":\"owner\"}", &altered))
        return false;
    char extra[1026]; memset(extra, ' ', sizeof(extra)); extra[1025] = '\0';
    if (cloud_command_parse(extra, &altered)) return false;
    return true;
}
