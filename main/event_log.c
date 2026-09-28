#include "event_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#define LOG_ENTRIES 32
#define LOG_TEXT_LEN 96

static const char *TAG = "event_log";
static char s_entries[LOG_ENTRIES][LOG_TEXT_LEN];
static int s_head;
static int s_count;
static SemaphoreHandle_t s_mutex;

/* Drop an incomplete trailing UTF-8 code point after bounded formatting. */
static void trim_utf8(char *text)
{
    size_t n = strlen(text);
    if (!n) return;
    size_t start = n - 1;
    while (start && ((unsigned char)text[start] & 0xc0) == 0x80) start--;
    unsigned char c = (unsigned char)text[start];
    size_t width = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
                   (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (n - start < width) text[start] = '\0';
}

void event_log_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
}

void event_log_add(const char *fmt, ...)
{
    char text[LOG_TEXT_LEN - 20];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    trim_utf8(text);

    uint64_t sec = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    char line[LOG_TEXT_LEN];
    snprintf(line, sizeof(line), "[%llu s] %s", (unsigned long long)sec, text);
    trim_utf8(line);
    ESP_LOGI(TAG, "%s", line);

    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snprintf(s_entries[s_head], LOG_TEXT_LEN, "%s", line);
    s_head = (s_head + 1) % LOG_ENTRIES;
    if (s_count < LOG_ENTRIES) s_count++;
    xSemaphoreGive(s_mutex);
}

static size_t append_json_string(char *out, size_t out_size, size_t pos, const char *s)
{
    if (pos + 1 >= out_size) return pos;
    out[pos++] = '"';
    for (; *s && pos + 2 < out_size; s++) {
        if (*s == '"' || *s == '\\') out[pos++] = '\\';
        if ((unsigned char)*s >= 0x20) out[pos++] = *s;
    }
    if (pos < out_size) out[pos++] = '"';
    return pos;
}

size_t event_log_to_json(char *out, size_t out_size)
{
    if (!out || out_size < 3) return 0;
    size_t pos = 0;
    out[pos++] = '[';

    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_count; i++) {
        int idx = (s_head - s_count + i + LOG_ENTRIES) % LOG_ENTRIES;
        size_t needed = 2;
        for (const unsigned char *p = (const unsigned char *)s_entries[idx]; *p; p++) {
            if (*p >= 0x20) needed += (*p == '"' || *p == '\\') ? 2 : 1;
        }
        /* Only whole entries: reserve comma, closing bracket and NUL. */
        if (needed + (i ? 1 : 0) + 2 > out_size - pos) break;
        if (i && pos < out_size - 1) out[pos++] = ',';
        pos = append_json_string(out, out_size, pos, s_entries[idx]);
    }
    if (s_mutex) xSemaphoreGive(s_mutex);

    if (pos < out_size - 1) out[pos++] = ']';
    out[pos] = '\0';
    return pos;
}

size_t event_log_to_text(char *out, size_t out_size)
{
    if (!out || out_size < 2) return 0;
    size_t pos = 0;

    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = s_count - 1; i >= 0; i--) {
        int idx = (s_head - 1 - i + LOG_ENTRIES) % LOG_ENTRIES;
        size_t len = strnlen(s_entries[idx], LOG_TEXT_LEN);
        if (len + (pos ? 1 : 0) >= out_size - pos) continue;
        if (pos) out[pos++] = '\n';
        memcpy(out + pos, s_entries[idx], len);
        pos += len;
    }
    if (s_mutex) xSemaphoreGive(s_mutex);

    out[pos] = '\0';
    return pos;
}
