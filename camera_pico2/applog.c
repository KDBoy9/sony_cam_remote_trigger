#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "applog.h"

typedef struct { char s[APPLOG_LINE]; } line_t;
static queue_t q;

void applog_init(void) { queue_init(&q, sizeof(line_t), 64); }

void applog(const char *fmt, ...) {
    line_t l;
    int n = snprintf(l.s, sizeof l.s - 3, "[%7lu] ", (unsigned long)to_ms_since_boot(get_absolute_time()));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l.s + n, sizeof l.s - 3 - (size_t)n, fmt, ap);
    va_end(ap);
    size_t len = strlen(l.s);
    l.s[len] = '\r'; l.s[len + 1] = '\n'; l.s[len + 2] = 0;
    if (!queue_try_add(&q, &l)) {            /* full: drop the oldest line, keep the newest */
        line_t dropped;
        queue_try_remove(&q, &dropped);
        queue_try_add(&q, &l);
    }
}

bool applog_peek(void) { line_t l; return queue_try_peek(&q, &l); }
bool applog_pop(char *out) {
    line_t l;
    if (!queue_try_remove(&q, &l)) return false;
    memcpy(out, l.s, APPLOG_LINE);
    return true;
}