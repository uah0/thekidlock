#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void remote_log_init(void);
size_t remote_log_read(uint64_t since, char *out, size_t out_size,
                       uint64_t *next_cursor, uint64_t *oldest_cursor,
                       bool *dropped);
