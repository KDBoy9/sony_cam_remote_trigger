#pragma once
#include "camera_wire.h"

void camera_control_init(void);
/* Core 0 (radio) */
uint8_t camera_command(uint8_t action, uint16_t seconds);
camera_status_t camera_snapshot(void);
/* Core 1 (USB). Call tick only with an open Sony session. False requests recovery. */
void camera_set_ready(bool ready, uint16_t rc);
bool camera_tick(void);
