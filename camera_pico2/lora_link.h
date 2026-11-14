#pragma once
/* Link layer: framing, ACK + retry, duplicate suppression, duty-cycle limiter, statistics, logging. */
#include <stdbool.h>
#include <stdint.h>
#include "lora_proto.h"

enum { LINK_OK = 0, LINK_ERR_NO_ACK = -1, LINK_ERR_RADIO = -2, LINK_ERR_DUTY = -3, LINK_ERR_ARG = -4, LINK_ERR_ENCODE = -5 };

typedef struct {
    uint8_t net_id;
    uint8_t retries;           /* extra attempts after the first when an ACK is wanted */
    uint32_t ack_margin_ms;    /* slack on top of the ACK airtime: covers the peer's processing time */
    uint8_t (*ack_extra)(uint8_t type, uint8_t *out); /* max PROTO_MAX_PAYLOAD minus ACK prefix */
    void (*yield)(void);       /* called while waiting for an ACK (keeps Wi-Fi alive on the Pico W) */
    /* Called once per new frame. Returns an ACK_* status. Keep it quick: the ACK goes out after it returns. */
    uint8_t (*on_message)(uint8_t type, const uint8_t *payload, uint8_t len, int16_t rssi_dbm, int8_t snr_q4);
} link_cfg_t;

typedef struct {
    bool acked;
    uint8_t extra_len;
    uint8_t extra[PROTO_MAX_PAYLOAD - PROTO_ACK_PAYLOAD_LEN];
    uint8_t tries;
    uint8_t ack_status;        /* ACK_* from the peer */
    int16_t ack_rssi_dbm;      /* how well the PEER heard our frame */
    int8_t ack_snr_q4;
    uint32_t rtt_ms;           /* from the start of the successful attempt to the ACK */
    uint32_t airtime_ms;       /* airtime of one attempt */
} link_result_t;

typedef struct {
    uint32_t tx_frames, tx_errors, tx_airtime_ms, duty_blocked;
    uint32_t rx_frames, rx_bad, rx_crc_err, rx_dups, acks_sent;
    uint32_t ack_timeouts, send_ok, send_fail;
    int16_t last_rssi_dbm;
    int8_t last_snr_q4;
} link_stats_t;

int link_init(const link_cfg_t *cfg);
/* Sends one frame. With want_ack it blocks until acknowledged or out of retries. res may be NULL. */
int link_send(uint8_t type, const uint8_t *payload, uint8_t len, bool want_ack, link_result_t *res);
void link_poll(void);                       /* call often: receives, dispatches, sends ACKs */
const link_stats_t *link_stats(void);
void link_log_stats(void);
uint32_t link_duty_used_ms(void);           /* airtime used over the last rolling hour */
const char *link_err_str(int err);
void link_fmt_snr(char *out, unsigned n, int8_t snr_q4);   /* "9.25", "-3.50" */
