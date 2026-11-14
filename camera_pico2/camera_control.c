#include "camera_control.h"
#include "lora_proto.h"
#include "pico/mutex.h"
#include "ptp_transport.h"
#include "sony_ptp.h"

/* The lock covers the mailbox and published state only. USB transfers never hold it. */
static mutex_t lock;
static camera_status_t state;
static uint32_t deadline, period_ms, image_base;

static void finish(uint8_t result, uint16_t rc) {
    state.phase = CAM_INACTIVE;
    state.result = result;
    state.ptp_rc = rc;
}

void camera_control_init(void) {
    mutex_init(&lock);
}

uint8_t camera_command(uint8_t action, uint16_t seconds) {
    if (!camera_command_valid(action, seconds)) return ACK_BAD_PAYLOAD;

    mutex_enter_blocking(&lock);
    uint8_t ack = ACK_OK;
    if (action == CAM_STOP_INTERVAL) {
        if (state.mode == CAM_INTERVAL && state.phase != CAM_INACTIVE) {
            if (state.phase == CAM_SHOOTING || state.phase == CAM_STOPPING) state.phase = CAM_STOPPING;
            else finish(CAM_STOPPED, 0);
        }
    } else if (!state.ready) {
        ack = ACK_NOT_READY;
    } else if (state.phase != CAM_INACTIVE) {
        ack = ACK_BUSY;
    } else {
        state = (camera_status_t){.mode = action, .phase = CAM_QUEUED, .ready = true};
        period_ms = (uint32_t)seconds * 1000;
        deadline = ptp_millis() + (action == CAM_DELAY ? period_ms : 0);
    }
    mutex_exit(&lock);
    return ack;
}

camera_status_t camera_snapshot(void) {
    mutex_enter_blocking(&lock);
    camera_status_t snapshot = state;
    if (snapshot.phase == CAM_COUNTDOWN || snapshot.phase == CAM_EXPOSING ||
        (snapshot.phase == CAM_QUEUED && snapshot.mode == CAM_DELAY)) {
        int32_t left = (int32_t)(deadline - ptp_millis());
        if (left > 0) snapshot.remaining_ms = (uint32_t)left;
    }
    mutex_exit(&lock);
    return snapshot;
}

void camera_set_ready(bool ready, uint16_t rc) {
    mutex_enter_blocking(&lock);
    state.ready = ready;
    if (!ready && state.phase != CAM_INACTIVE) {
        bool bulb_held = state.phase == CAM_EXPOSING || state.phase == CAM_RELEASING;
        finish(bulb_held ? CAM_RELEASE_ERROR : CAM_DISCONNECTED, rc);
    } else if (state.mode == CAM_IDLE) {
        state.ptp_rc = rc;
    }
    mutex_exit(&lock);
}

bool camera_tick(void) {
    mutex_enter_blocking(&lock);
    uint32_t now = ptp_millis();
    uint8_t phase = state.phase;
    uint8_t mode = state.mode;
    if ((phase == CAM_QUEUED || phase == CAM_COUNTDOWN || phase == CAM_EXPOSING) &&
        (int32_t)(now - deadline) < 0) {
        if (phase == CAM_QUEUED) state.phase = CAM_COUNTDOWN;
        mutex_exit(&lock);
        return true;
    }
    if (phase == CAM_INACTIVE) {
        mutex_exit(&lock);
        return true;
    }
    bool releasing = phase == CAM_EXPOSING;
    state.phase = releasing ? CAM_RELEASING : CAM_SHOOTING;
    mutex_exit(&lock);

    if (!releasing) image_base = ptp_usb_object_events();
    bool opening = mode == CAM_BULB && !releasing;
    bool ok = releasing ? sony_release() : opening ? sony_bulb_start() : sony_shoot();
    uint32_t finished_at = ptp_millis();
    uint16_t rc = sony_last_rc();
    if (opening && ok) {
        mutex_enter_blocking(&lock);
        deadline = finished_at + period_ms;  /* starts after the camera accepts the bulb press */
        state.phase = CAM_EXPOSING;
        mutex_exit(&lock);
        return true;
    }

    bool release_failed = false;
    if (!ok && mode == CAM_BULB && ptp_usb_ready()) {
        bool released = sony_release();   /* retry an uncertain release or clean up a failed start */
        if (releasing && released) {
            ok = true;
            rc = sony_last_rc();
        }
        release_failed = !released;
    } else if (!ok && mode == CAM_BULB) {
        release_failed = true;
    }

    mutex_enter_blocking(&lock);
    state.shots++;
    state.image_events = ptp_usb_object_events() - image_base;
    if (!ok) {
        state.ready = false;
        finish(release_failed ? CAM_RELEASE_ERROR : CAM_USB_ERROR, rc);
    } else if (state.phase == CAM_STOPPING) {
        finish(CAM_STOPPED, rc);
    } else if (mode == CAM_INTERVAL) {
        deadline = now + period_ms;
        /* A slow exposure can overrun its slot. Start the next shot when ready, with no backlog. */
        if ((int32_t)(deadline - finished_at) < 0) deadline = finished_at;
        state.phase = CAM_COUNTDOWN;
        state.result = CAM_COMPLETE;
        state.ptp_rc = rc;
    } else {
        finish(CAM_COMPLETE, rc);
    }
    mutex_exit(&lock);
    return ok;
}
