#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

typedef enum { CLOUD_OPEN, CLOUD_CLOSE, CLOUD_HOLD_ON, CLOUD_HOLD_OFF } cloud_operation_t;
typedef struct {
    uint64_t id;
    int64_t issued, expires;
    cloud_operation_t operation;
    char target[18], actor[18];
} cloud_command_t;

bool cloud_command_parse(const char *payload, cloud_command_t *out);
bool cloud_command_valid(const cloud_command_t *command, const char *mac, uint64_t last_id,
                         int64_t now, bool synced);
bool cloud_policy_selftest(void);
