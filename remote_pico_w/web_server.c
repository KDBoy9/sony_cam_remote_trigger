#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "camera_page.h"
#include "web_server.h"

web_state_t web;

typedef struct {
    char line[192];
    size_t used;
} http_conn_t;

static err_t respond(struct tcp_pcb *pcb, const char *status, const char *type, const char *body) {
    char header[192];
    size_t size = strlen(body);
    int n = snprintf(header, sizeof header,
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, type, (unsigned)size);
    err_t err = tcp_write(pcb, header, (u16_t)n, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    if (err == ERR_OK) err = tcp_write(pcb, body, (u16_t)size, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(pcb);
        if (tcp_close(pcb) == ERR_OK) return ERR_OK;
    }
    tcp_abort(pcb);
    return ERR_ABRT;
}

static bool number(const char **query, uint16_t *out) {
    const char *p = *query;
    if (*p < '0' || *p > '9') return false;
    unsigned long value = 0;
    do {
        value = value * 10 + (unsigned)(*p++ - '0');
        if (value > CAMERA_MAX_SECONDS) return false;
    } while (*p >= '0' && *p <= '9');
    *query = p;
    *out = (uint16_t)value;
    return true;
}

static bool command_args(const char *query, uint16_t *action, uint16_t *seconds) {
    const char separator[] = "&seconds=";
    if (!number(&query, action) || strncmp(query, separator, sizeof separator - 1) != 0) return false;
    query += sizeof separator - 1;
    return number(&query, seconds) && *query == 0 && camera_command_valid(*action, *seconds);
}

static err_t json(struct tcp_pcb *pcb, const char *status, const char *body) {
    return respond(pcb, status, "application/json", body);
}

static err_t dispatch(struct tcp_pcb *pcb, char *line) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t age = now - web.received_at;
    bool fresh = web.has_status && age < web.stale_after;
    if (strcmp(line, "GET /") == 0) {
        return respond(pcb, "200 OK", "text/html; charset=utf-8", PAGE);
    }
    if (strcmp(line, "GET /api/status") == 0) {
        const camera_status_t *s = &web.camera;
        uint32_t left = fresh && s->remaining_ms > age ? s->remaining_ms - age : 0;
        char body[420];
        snprintf(body, sizeof body,
                 "{\"fresh\":%s,\"pending\":%s,\"ready\":%s,\"mode\":%u,\"phase\":%u,"
                 "\"result\":%u,\"remaining\":%lu,\"shots\":%lu,\"images\":%lu,\"rc\":%u,\"feedback\":\"%s\"}",
                 fresh ? "true" : "false", web.pending ? "true" : "false", s->ready ? "true" : "false",
                 s->mode, s->phase, s->result, (unsigned long)left, (unsigned long)s->shots,
                 (unsigned long)s->image_events, s->ptp_rc, web.feedback ? web.feedback : "");
        return json(pcb, "200 OK", body);
    }

    const char prefix[] = "POST /api/command?action=";
    if (strncmp(line, prefix, sizeof prefix - 1) == 0) {
        const char *query = line + sizeof prefix - 1;
        uint16_t action, seconds;
        if (!command_args(query, &action, &seconds)) {
            return json(pcb, "400 Bad Request", "{\"ok\":false,\"error\":\"Invalid action or seconds\"}");
        }
        if (web.pending) {
            return json(pcb, "409 Conflict", "{\"ok\":false,\"error\":\"Another command is being sent\"}");
        }
        if (action != CAM_STOP_INTERVAL && (!fresh || !web.camera.ready)) {
            return json(pcb, "503 Unavailable", "{\"ok\":false,\"error\":\"Camera is unavailable\"}");
        }
        web.action = (uint8_t)action;
        web.seconds = seconds;
        web.pending = true;
        web.feedback = NULL;
        return json(pcb, "202 Accepted", "{\"ok\":true}");
    }

    return respond(pcb, "404 Not Found", "text/plain", "Not found");
}

static void on_error(void *arg, err_t err) {
    (void)err;
    free(arg);
}

static void clear_conn(struct tcp_pcb *pcb, void *arg) {
    free(arg);
    tcp_arg(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
}

static err_t on_timeout(void *arg, struct tcp_pcb *pcb) {
    clear_conn(pcb, arg);
    tcp_abort(pcb);
    return ERR_ABRT;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (!p || err != ERR_OK) {
        if (p) pbuf_free(p);
        return on_timeout(arg, pcb);
    }
    http_conn_t *conn = arg;
    size_t room = sizeof conn->line - 1 - conn->used;
    conn->used += pbuf_copy_partial(p, conn->line + conn->used, (u16_t)room, 0);
    conn->line[conn->used] = 0;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    char *newline = strchr(conn->line, '\n');
    bool complete = newline != NULL;
    if (!complete && conn->used < sizeof conn->line - 1) return ERR_OK;
    if (complete) *newline = 0;
    char request[sizeof conn->line];
    strcpy(request, conn->line);
    clear_conn(pcb, conn);

    char *version = strstr(request, " HTTP/1.");
    if (!complete || !version) return respond(pcb, "400 Bad Request", "text/plain", "Bad request");
    *version = 0;
    return dispatch(pcb, request);
}

static err_t on_accept(void *arg, struct tcp_pcb *client, err_t err) {
    (void)arg;
    if (err != ERR_OK || !client) return ERR_VAL;
    http_conn_t *conn = calloc(1, sizeof *conn);
    if (!conn) {
        tcp_abort(client);
        return ERR_ABRT;
    }
    tcp_arg(client, conn);
    tcp_recv(client, on_recv);
    tcp_err(client, on_error);
    tcp_poll(client, on_timeout, 8);
    return ERR_OK;
}

bool web_server_start(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, 80) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }
    struct tcp_pcb *listener = tcp_listen_with_backlog(pcb, 4);
    if (!listener) {
        tcp_close(pcb);
        return false;
    }
    tcp_accept(listener, on_accept);
    return true;
}
