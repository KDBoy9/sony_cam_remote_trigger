#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "app_config.h"
#include "dhcpserver.h"
#include "lora_config.h"
#include "lora_setup.h"
#include "sx127x.h"
#include "web_server.h"

static const char *ack_feedback(uint8_t ack) {
    switch (ack) {
        case ACK_OK: return "Command accepted by Pico 2";
        case ACK_NOT_READY: return "Camera USB session is unavailable";
        case ACK_BUSY: return "Camera already has an active operation";
        default: return "Pico 2 rejected the command";
    }
}

static void exchange(bool command) {
    uint8_t payload[CAMERA_COMMAND_LEN] = {web.action};
    cam_write_le(payload + 1, web.seconds, 2);
    link_result_t result = {0};
    int rc = link_send(command ? MSG_SHUTTER : MSG_CAMERA_STATUS,
                       command ? payload : NULL, command ? sizeof payload : 0, true, &result);
    bool valid = rc == LINK_OK && result.acked && result.extra_len == CAMERA_STATUS_LEN;
    if (valid) {
        web.camera = camera_status_decode(result.extra);
        web.received_at = to_ms_since_boot(get_absolute_time());
        web.has_status = true;
    }
    if (command) {
        if (valid) web.feedback = ack_feedback(result.ack_status);
        else if (rc == LINK_ERR_DUTY && result.tries == 1) web.feedback = "Radio airtime limit reached";
        else {
            web.has_status = false;
            web.feedback = "Command outcome unknown. Check the camera status before retrying.";
        }
        web.pending = false;
    }
}

int main(void) {
    stdio_init_all();
    if (cyw43_arch_init()) {
        puts("Wi-Fi init failed. Check the Pico W board target.");
        return 1;
    }

    int radio = lora_setup(NULL, cyw43_arch_poll, NULL);
    if (radio) {
        printf("Radio init failed (%d)\n", radio);
        web.feedback = "Pico W radio init failed. Check wiring and reset.";
    }

    /* Keep normal status polling under half the configured airtime budget.
     * Leave the rest for commands, retries and ACKs. */
    uint32_t ack_us = sx127x_airtime_us(PROTO_HDR_LEN + PROTO_CRC_LEN +
                                       PROTO_ACK_PAYLOAD_LEN + CAMERA_STATUS_LEN);
    uint32_t poll_ms = LORA_DUTY_PERCENT ? (ack_us + 5 * LORA_DUTY_PERCENT - 1) /
                                         (5 * LORA_DUTY_PERCENT) : 1000;
    if (poll_ms < 1000) poll_ms = 1000;
    web.stale_after = poll_ms * 3 + 1000;

    static dhcp_server_t dhcp;
    ip_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);
    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASSWORD[0] ? AP_PASSWORD : NULL,
                             AP_PASSWORD[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
    dhcp_server_init(&dhcp, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    if (web_server_start()) printf("Wi-Fi %s | http://192.168.4.1/\n", AP_SSID);
    else puts("HTTP server init failed.");

    uint32_t next_poll = to_ms_since_boot(get_absolute_time());
    for (;;) {
        cyw43_arch_poll();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (!radio && (web.pending || (int32_t)(now - next_poll) >= 0)) {
            bool command = web.pending;
            exchange(command);
            next_poll = to_ms_since_boot(get_absolute_time()) + poll_ms;
        }
        sleep_us(500);
    }
}
