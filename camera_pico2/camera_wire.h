#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Command payload: action:u8, duration in seconds:u16 LE.
 * A camera ACK carries this status snapshot after the existing RF metrics.
 * Browser polling is local. Only the Pico W requests snapshots over LoRa. */
#define CAMERA_COMMAND_LEN 3
#define CAMERA_STATUS_LEN 18
#define CAMERA_MAX_SECONDS 65535u

enum camera_action {
    CAM_IDLE, CAM_FIRE, CAM_INTERVAL, CAM_DELAY, CAM_BULB, CAM_STOP_INTERVAL
};
enum camera_phase {
    CAM_INACTIVE, CAM_QUEUED, CAM_COUNTDOWN, CAM_SHOOTING, CAM_EXPOSING, CAM_RELEASING, CAM_STOPPING
};
enum camera_result {
    CAM_NO_RESULT, CAM_COMPLETE, CAM_STOPPED, CAM_USB_ERROR, CAM_DISCONNECTED, CAM_RELEASE_ERROR
};

static inline bool camera_command_valid(uint16_t action, uint16_t seconds) {
    if (action < CAM_FIRE || action > CAM_STOP_INTERVAL) return false;
    bool timed = action == CAM_INTERVAL || action == CAM_DELAY || action == CAM_BULB;
    return timed ? seconds > 0 : seconds == 0;
}

typedef struct {
    uint8_t mode, phase, result;
    bool ready;
    uint32_t remaining_ms;
    uint32_t shots;          /* completed PTP shutter attempts in this run */
    uint32_t image_events;   /* object events observed during the latest attempt */
    uint16_t ptp_rc;
} camera_status_t;

static inline void cam_write_le(uint8_t *p, uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static inline uint32_t cam_read_le(const uint8_t *p, unsigned width) {
    uint32_t value = 0;
    for (unsigned i = 0; i < width; i++) value |= (uint32_t)p[i] << (8 * i);
    return value;
}

static inline void camera_status_encode(uint8_t *p, const camera_status_t *s) {
    p[0] = s->mode;
    p[1] = s->phase;
    p[2] = s->result;
    p[3] = s->ready;
    cam_write_le(p + 4, s->remaining_ms, 4);
    cam_write_le(p + 8, s->shots, 4);
    cam_write_le(p + 12, s->image_events, 4);
    cam_write_le(p + 16, s->ptp_rc, 2);
}

static inline camera_status_t camera_status_decode(const uint8_t *p) {
    return (camera_status_t){
        .mode = p[0], .phase = p[1], .result = p[2], .ready = p[3] != 0,
        .remaining_ms = cam_read_le(p + 4, 4), .shots = cam_read_le(p + 8, 4),
        .image_events = cam_read_le(p + 12, 4), .ptp_rc = cam_read_le(p + 16, 2)
    };
}
