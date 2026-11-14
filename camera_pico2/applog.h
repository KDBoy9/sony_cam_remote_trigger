#pragma once
#include <stdbool.h>
/* Tiny cross-core log queue: core1 (USB host + app) writes, core0 (USB CDC device) prints. */
#define APPLOG_LINE 160
void applog_init(void);
void applog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool applog_peek(void);               /* true if a line is waiting */
bool applog_pop(char *out);           /* out must hold APPLOG_LINE bytes */
