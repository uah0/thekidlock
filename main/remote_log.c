#include "remote_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define REMOTE_LOG_CAPACITY (32 * 1024)
#define REMOTE_LOG_FORMAT_BUFFER 512

static char s_ring[REMOTE_LOG_CAPACITY];
static uint64_t s_total_written;
static vprintf_like_t s_uart_vprintf;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void append_bytes(const char *data, size_t length)
{
    if (!data || length == 0) return;
    portENTER_CRITICAL(&s_lock);
    size_t offset = s_total_written % REMOTE_LOG_CAPACITY;
    size_t first = length < REMOTE_LOG_CAPACITY - offset ? length : REMOTE_LOG_CAPACITY - offset;
    memcpy(s_ring + offset, data, first);
    memcpy(s_ring, data + first, length - first);
    s_total_written += length;
    portEXIT_CRITICAL(&s_lock);
}

static int remote_log_vprintf(const char *format, va_list args)
{
    char formatted[REMOTE_LOG_FORMAT_BUFFER];
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(formatted, sizeof(formatted), format, copy);
    va_end(copy);
    if (needed > 0) {
        size_t stored = (size_t)needed < sizeof(formatted)
                            ? (size_t)needed : sizeof(formatted) - 1;
        append_bytes(formatted, stored);
    }

    if (!s_uart_vprintf) return needed;
    va_copy(copy, args);
    int result = s_uart_vprintf(format, copy);
    va_end(copy);
    return result;
}

void remote_log_init(void)
{
    s_uart_vprintf = esp_log_set_vprintf(remote_log_vprintf);
}

size_t remote_log_read(uint64_t since, char *out, size_t out_size,
                       uint64_t *next_cursor, uint64_t *oldest_cursor,
                       bool *dropped)
{
    if (!out || out_size == 0) return 0;
    portENTER_CRITICAL(&s_lock);
    const uint64_t total = s_total_written;
    const uint64_t oldest = total > REMOTE_LOG_CAPACITY
                                ? total - REMOTE_LOG_CAPACITY : 0;
    bool lost = since < oldest || since > total;
    if (lost) since = oldest;
    uint64_t available = total - since;
    /* Bound interrupt-masked work irrespective of the caller's 16 KiB buffer.
     * Cursor protocol permits a short read; subsequent requests drain the rest. */
    size_t limit = out_size < 1024 ? out_size : 1024;
    size_t length = available < limit ? (size_t)available : limit;
    size_t offset = since % REMOTE_LOG_CAPACITY;
    size_t first = length < REMOTE_LOG_CAPACITY - offset ? length : REMOTE_LOG_CAPACITY - offset;
    memcpy(out, s_ring + offset, first);
    memcpy(out + first, s_ring, length - first);
    portEXIT_CRITICAL(&s_lock);

    if (next_cursor) *next_cursor = since + length;
    if (oldest_cursor) *oldest_cursor = oldest;
    if (dropped) *dropped = lost;
    return length;
}
