#pragma once

#include <stddef.h>

void event_log_init(void);
void event_log_add(const char *fmt, ...);
size_t event_log_to_json(char *out, size_t out_size);
size_t event_log_to_text(char *out, size_t out_size);
