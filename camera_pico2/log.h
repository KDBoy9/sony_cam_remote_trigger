#pragma once
/* Tiny printf logger: timestamp (ms since boot), level, tag. Goes to the USB serial console. */
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"

#ifndef LOG_LEVEL
#define LOG_LEVEL 4   /* 0 off, 1 error, 2 warn, 3 info, 4 debug */
#endif

#ifdef CAMERA_CDC_LOG
#include "applog.h"
#define LOG_OUTPUT(level, tag, fmt, ...) applog(level " %-6s " fmt, tag, ##__VA_ARGS__)
#else
#define LOG_OUTPUT(level, tag, fmt, ...) printf("[%8lu] " level " %-6s " fmt "\n", LOG_MS(), tag, ##__VA_ARGS__)
#endif

#define LOG_MS() ((unsigned long)to_ms_since_boot(get_absolute_time()))
#define LOGE(tag, fmt, ...) do { if (LOG_LEVEL >= 1) LOG_OUTPUT("E", tag, fmt, ##__VA_ARGS__); } while (0)
#define LOGW(tag, fmt, ...) do { if (LOG_LEVEL >= 2) LOG_OUTPUT("W", tag, fmt, ##__VA_ARGS__); } while (0)
#define LOGI(tag, fmt, ...) do { if (LOG_LEVEL >= 3) LOG_OUTPUT("I", tag, fmt, ##__VA_ARGS__); } while (0)
#define LOGD(tag, fmt, ...) do { if (LOG_LEVEL >= 4) LOG_OUTPUT("D", tag, fmt, ##__VA_ARGS__); } while (0)

static inline void log_hex(const char *tag, const char *label, const uint8_t *b, size_t n) {
    if (LOG_LEVEL < 4) return;
    printf("[%8lu] D %-6s %s (%u B):", LOG_MS(), tag, label, (unsigned)n);
    for (size_t i = 0; i < n; i++) printf(" %02X", b[i]);
    printf("\n");
}
