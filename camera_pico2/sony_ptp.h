#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef SONY_HOLD_MS
#define SONY_HOLD_MS 1000   /* time the shutter stays pressed while polling (capture: ~1.08 s) */
#endif

bool sony_session_open(void);   /* handshake exactly as seen in the Linux capture */
void sony_session_close(void);
bool sony_ctrl(uint16_t prop, uint16_t value);
bool sony_shoot(void);          /* S1 down, S2 down, poll, S2 up, S1 up */
bool sony_bulb_start(void);     /* S1 up, Sony RequestOneShooting down */
bool sony_release(void);        /* S2 up, then S1 up, attempt both */
uint16_t sony_last_rc(void);    /* last PTP response code (0xFFFF = transport error) */
