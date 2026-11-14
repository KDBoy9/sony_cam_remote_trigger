#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "camera_wire.h"

typedef struct {
    camera_status_t camera;
    uint32_t received_at, stale_after;
    uint8_t action;
    uint16_t seconds;
    bool pending, has_status;
    const char *feedback;
} web_state_t;

extern web_state_t web;
bool web_server_start(void);
