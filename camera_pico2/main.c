/* Pico 2: core 0 owns LoRa and native USB CDC. Core 1 owns PIO USB and Sony PTP. */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "pio_usb.h"
#include "tusb.h"
#include "applog.h"
#include "status.h"
#include "ptp_transport.h"
#include "sony_ptp.h"
#include "camera_control.h"
#include "lora_setup.h"

#define SETTLE_MS           800    /* wait after the camera enumerates before talking to it */
#define HEARTBEAT_MS        5000   /* heartbeat line on the console */
#define USB_DP_PIN          0      /* must match PIO_USB_DEFAULT_CONFIG: D+ = GP0, D- = GP1 */

/* While nothing is attached, pulse a USB bus reset on the host port this often so the stack
 * re-detects a camera that was plugged in late or powered up late. 0 disables it. */
#ifndef HOST_RETRY_MS
#define HOST_RETRY_MS       8000
#endif

app_status_t g_status;

/* ---- core 1: USB host + camera ------------------------------------------- */
static void host_bus_reset_pulse(void) {
    g_status.bus_resets++;
    applog("host: bus-reset pulse #%lu (no device attached, re-detecting)", (unsigned long)g_status.bus_resets);
    tuh_rhport_reset_bus(BOARD_TUH_RHPORT, true);
    sleep_ms(30);                                   /* USB spec: hold reset 10-50 ms */
    tuh_rhport_reset_bus(BOARD_TUH_RHPORT, false);
}

static void app_loop(void) {
    uint32_t retry_at = 0, next_host_retry = ptp_millis() + HOST_RETRY_MS;
    bool was_ready = false, session = false, cleanup_release = false;

    for (;;) {
        tuh_task();
        g_status.core1_loops++;
        uint32_t now = ptp_millis();
        bool ready = ptp_usb_ready();

        if (ready && !was_ready) {
            applog("camera PTP interface is up (PC Remote)");
            ptp_usb_arm_events();
            retry_at = now + SETTLE_MS;
        } else if (!ready && was_ready) {
            applog("camera PTP interface went away");
            session = false;
            camera_set_ready(false, 0xFFFF);
            cleanup_release |= camera_snapshot().result == CAM_RELEASE_ERROR;
        }
        was_ready = ready;

        if (HOST_RETRY_MS && g_status.devices == 0 && (int32_t)(now - next_host_retry) >= 0) {
            host_bus_reset_pulse();
            next_host_retry = ptp_millis() + HOST_RETRY_MS;
        }
        if (g_status.devices) next_host_retry = now + HOST_RETRY_MS;

        if (ready && !session && (int32_t)(now - retry_at) >= 0) {
            session = sony_session_open();
            if (session && cleanup_release) {
                session = sony_release();
                cleanup_release = !session;
            }
            camera_set_ready(session, sony_last_rc());
            applog("session %s (rc=0x%04x)", session ? "open" : "FAILED", sony_last_rc());
            if (!session) {
                sony_session_close();
                retry_at = ptp_millis() + 2000;
            }
        }

        if (session && !camera_tick()) {
            cleanup_release = true;
            session = false;
            sony_session_close();
            retry_at = ptp_millis() + 1000;
        }
        g_status.state = session ? 3 : ready ? 2 : (g_status.devices ? 1 : 0);
    }
}

static uint8_t on_message(uint8_t type, const uint8_t *payload, uint8_t len, int16_t rssi, int8_t snr) {
    (void)rssi;
    (void)snr;
    if (type == MSG_CAMERA_STATUS) return len == 0 ? ACK_OK : ACK_BAD_PAYLOAD;
    if (type != MSG_SHUTTER) return ACK_UNKNOWN_TYPE;
    if (len != CAMERA_COMMAND_LEN) return ACK_BAD_PAYLOAD;
    return camera_command(payload[0], (uint16_t)cam_read_le(payload + 1, 2));
}

static uint8_t ack_extra(uint8_t type, uint8_t *out) {
    if (type != MSG_CAMERA_STATUS && type != MSG_SHUTTER) return 0;
    camera_status_t status = camera_snapshot();
    camera_status_encode(out, &status);
    return CAMERA_STATUS_LEN;
}

static void core1_main(void) {
    sleep_ms(10);
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = USB_DP_PIN;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    bool ok = tuh_init(BOARD_TUH_RHPORT);
    applog("core1: host init %s", ok ? "ok" : "FAILED");
    app_loop();
}

static void led_set(bool on) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#else
    (void)on;
#endif
}

static bool led_pattern(uint8_t state, bool shooting, uint32_t t) {
    if (shooting) return (t / 50) % 2;
    switch (state) {
        case 0:  return (t % 1000) < 500;
        case 1:  return (t % 400) < 200;
        case 2:  return (t % 200) < 100;
        default: return (t % 1000) > 100;
    }
}

int main(void) {
    set_sys_clock_khz(120000, true);   /* PIO USB needs a multiple of 12 MHz */
    sleep_ms(10);
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
    applog_init();
    camera_control_init();
    applog("boot: Pico 2 LoRa camera (heartbeat every %d ms)", HEARTBEAT_MS);
    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    tud_init(0);   /* native USB = CDC log console */
    int radio = lora_setup(on_message, NULL, ack_extra);
    applog("radio init %s (%d)", radio ? "FAILED" : "ok", radio);
    char line[APPLOG_LINE];
    uint32_t last_hb = 0, ticks = 0;
    for (;;) {
        tud_task();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (!radio) link_poll();
        camera_status_t status = camera_snapshot();
        bool shooting = status.phase == CAM_SHOOTING || status.phase == CAM_EXPOSING ||
                        status.phase == CAM_RELEASING || status.phase == CAM_STOPPING;
        led_set(led_pattern(g_status.state, shooting, now));

        if (now - last_hb >= HEARTBEAT_MS) {
            last_hb = now;
            applog("hb %lu | core1=%lu USB=%u dev=%u %04x:%04x | mode=%u phase=%u result=%u shots=%lu rc=%04x",
                   (unsigned long)++ticks, (unsigned long)g_status.core1_loops, g_status.state,
                   g_status.devices, g_status.vid, g_status.pid, status.mode, status.phase, status.result,
                   (unsigned long)status.shots, status.ptp_rc);
        }

        /* Print whenever the Mac has configured the CDC port; a terminal does not need to assert DTR. */
        while (tud_mounted() && applog_peek() && tud_cdc_write_available() >= APPLOG_LINE) {
            if (!applog_pop(line)) break;
            tud_cdc_write_str(line);
        }
        tud_cdc_write_flush();
    }
}
