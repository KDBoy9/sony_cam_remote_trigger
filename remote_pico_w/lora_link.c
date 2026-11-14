#include <stdlib.h>
#include <string.h>
#include "lora_link.h"
#include "lora_config.h"
#include "sx127x.h"
#include "log.h"

#define TAG "link"
#define ACK_TURNAROUND_MS 5
#define RETRY_BACKOFF_MS  25
#define DUP_WINDOW_MS     60000   /* covers all command retries even at high spreading factors */
#define DUTY_BUCKETS      60            /* one-minute buckets = rolling hour */

static link_cfg_t L;
static link_stats_t S;
static uint8_t g_seq;

static struct { bool valid; uint8_t seq, type, status; uint32_t t_ms; } last_rx;
static struct { bool waiting; bool got; uint8_t seq, type, status; int16_t rssi; int8_t snr_q4; uint8_t extra_len, extra[PROTO_MAX_PAYLOAD - PROTO_ACK_PAYLOAD_LEN]; } W;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

/* ---- duty cycle (rolling hour, one-minute buckets) ------------------------------------------- */
static uint32_t bucket_ms[DUTY_BUCKETS];
static uint32_t bucket_min[DUTY_BUCKETS];

static void duty_add(uint32_t air_ms) {
    uint32_t minute = now_ms() / 60000u;
    uint32_t i = minute % DUTY_BUCKETS;
    if (bucket_min[i] != minute) { bucket_min[i] = minute; bucket_ms[i] = 0; }
    bucket_ms[i] += air_ms;
}

uint32_t link_duty_used_ms(void) {
    uint32_t minute = now_ms() / 60000u, sum = 0;
    for (int i = 0; i < DUTY_BUCKETS; i++)
        if (minute - bucket_min[i] < DUTY_BUCKETS) sum += bucket_ms[i];
    return sum;
}

static bool duty_allows(uint32_t air_ms) {
    uint32_t limit_ms = (uint32_t)LORA_DUTY_PERCENT * 36000u;   /* percent of one hour, in ms */
    return link_duty_used_ms() + air_ms <= limit_ms;
}

/* ---- helpers --------------------------------------------------------------------------------- */
void link_fmt_snr(char *out, unsigned n, int8_t q4) {
    int v = q4 * 25;                                  /* hundredths of a dB */
    snprintf(out, n, "%s%d.%02d", v < 0 ? "-" : "", abs(v) / 100, abs(v) % 100);
}

const char *link_err_str(int e) {
    switch (e) {
        case LINK_OK: return "ok";
        case LINK_ERR_NO_ACK: return "no ACK";
        case LINK_ERR_RADIO: return "radio error";
        case LINK_ERR_DUTY: return "duty-cycle limit";
        case LINK_ERR_ARG: return "bad argument";
        case LINK_ERR_ENCODE: return "encode error";
        default: return "?";
    }
}

const link_stats_t *link_stats(void) { return &S; }

int link_init(const link_cfg_t *cfg) {
    L = *cfg;
    memset(&S, 0, sizeof S);
    memset(&last_rx, 0, sizeof last_rx);
    memset(&W, 0, sizeof W);
    memset(bucket_ms, 0, sizeof bucket_ms);
    memset(bucket_min, 0, sizeof bucket_min);
    g_seq = 0;
    LOGI(TAG, "init: net_id=0x%02X retries=%u ack_margin=%lu ms duty limit=%d%%",
         L.net_id, L.retries, (unsigned long)L.ack_margin_ms, LORA_DUTY_PERCENT);
    return LINK_OK;
}

static int tx_frame(const proto_frame_t *f, uint32_t *air_ms_out) {
    uint8_t buf[PROTO_MAX_FRAME];
    size_t n = proto_encode(f, buf, sizeof buf);
    if (!n) { LOGE(TAG, "encode failed"); return LINK_ERR_ENCODE; }
    uint32_t air_ms = (sx127x_airtime_us((uint8_t)n) + 999) / 1000;
    if (air_ms_out) *air_ms_out = air_ms;
    if (!duty_allows(air_ms)) {
        S.duty_blocked++;
        LOGW(TAG, "TX blocked by duty-cycle limit (%lu ms used in the last hour)", (unsigned long)link_duty_used_ms());
        return LINK_ERR_DUTY;
    }
    log_hex(TAG, "tx frame", buf, n);
    int rc = sx127x_transmit(buf, (uint8_t)n, air_ms * 2 + 100);
    if (rc) { S.tx_errors++; LOGE(TAG, "radio TX failed (%d)", rc); return LINK_ERR_RADIO; }
    duty_add(air_ms);
    S.tx_frames++;
    S.tx_airtime_ms += air_ms;
    return LINK_OK;
}

static void send_ack(const proto_frame_t *rx, uint8_t status, const sx127x_rx_info_t *info) {
    proto_frame_t a = {.net_id = L.net_id, .flags = PROTO_FLAG_IS_ACK, .type = rx->type, .seq = rx->seq, .len = PROTO_ACK_PAYLOAD_LEN};
    a.payload[0] = status;
    a.payload[1] = (uint8_t)info->rssi_dbm;
    a.payload[2] = (uint8_t)((uint16_t)info->rssi_dbm >> 8);
    a.payload[3] = (uint8_t)info->snr_q4;
    if (L.ack_extra) a.len += L.ack_extra(rx->type, a.payload + a.len);
    sleep_ms(ACK_TURNAROUND_MS);                     /* let the sender's radio switch to receive */
    if (tx_frame(&a, NULL) == LINK_OK) {
        S.acks_sent++;
        LOGD(TAG, "ACK sent for %s seq=%u status=%u", proto_type_str(rx->type), rx->seq, status);
    }
}

static void process_rx(const uint8_t *buf, int n, const sx127x_rx_info_t *info) {
    log_hex(TAG, "rx frame", buf, (size_t)n);
    proto_frame_t f;
    int rc = proto_decode(buf, (size_t)n, &f, L.net_id);
    if (rc != PROTO_OK) {
        S.rx_bad++;
        if (rc == PROTO_ERR_MAGIC || rc == PROTO_ERR_NET) LOGD(TAG, "ignored frame: %s", proto_err_str(rc));
        else LOGW(TAG, "dropped frame: %s (%d bytes, rssi %d dBm)", proto_err_str(rc), n, info->rssi_dbm);
        return;
    }
    S.rx_frames++;
    S.last_rssi_dbm = info->rssi_dbm;
    S.last_snr_q4 = info->snr_q4;
    char snr[12];
    link_fmt_snr(snr, sizeof snr, info->snr_q4);

    if (f.flags & PROTO_FLAG_IS_ACK) {
        if (W.waiting && f.seq == W.seq && f.type == W.type && f.len >= PROTO_ACK_PAYLOAD_LEN) {
            W.got = true;
            W.status = f.payload[0];
            W.rssi = (int16_t)(f.payload[1] | (f.payload[2] << 8));
            W.snr_q4 = (int8_t)f.payload[3];
            W.extra_len = f.len - PROTO_ACK_PAYLOAD_LEN;
            memcpy(W.extra, f.payload + PROTO_ACK_PAYLOAD_LEN, W.extra_len);
            char peer_snr[12];
            link_fmt_snr(peer_snr, sizeof peer_snr, W.snr_q4);
            LOGI(TAG, "RX ACK %s seq=%u status=%u (our frame at the peer: rssi=%d dBm snr=%s dB; this ACK here: rssi=%d dBm snr=%s dB)",
                 proto_type_str(f.type), f.seq, W.status, W.rssi, peer_snr, info->rssi_dbm, snr);
        } else {
            LOGW(TAG, "unexpected ACK %s seq=%u (not waiting for it)", proto_type_str(f.type), f.seq);
        }
        return;
    }

    LOGI(TAG, "RX %s seq=%u len=%u rssi=%d dBm snr=%s dB%s", proto_type_str(f.type), f.seq, f.len, info->rssi_dbm, snr,
         (f.flags & PROTO_FLAG_ACK_REQ) ? " (ACK requested)" : "");

    uint8_t status;
    bool dup = last_rx.valid && last_rx.seq == f.seq && last_rx.type == f.type && (now_ms() - last_rx.t_ms) < DUP_WINDOW_MS;
    if (dup) {
        S.rx_dups++;
        status = last_rx.status;
        LOGW(TAG, "duplicate %s seq=%u (our earlier ACK was probably lost): re-ACKing, not delivering again",
             proto_type_str(f.type), f.seq);
    } else {
        status = L.on_message ? L.on_message(f.type, f.payload, f.len, info->rssi_dbm, info->snr_q4) : ACK_OK;
        last_rx.valid = true;
        last_rx.seq = f.seq;
        last_rx.type = f.type;
        last_rx.status = status;
        last_rx.t_ms = now_ms();
    }
    if (f.flags & PROTO_FLAG_ACK_REQ) send_ack(&f, status, info);
}

void link_poll(void) {
    uint8_t buf[PROTO_MAX_FRAME + 8];
    sx127x_rx_info_t info = {0};
    int n = sx127x_poll_rx(buf, sizeof buf, &info);
    if (n > 0) process_rx(buf, n, &info);
    else if (n == -1) { S.rx_crc_err++; LOGW(TAG, "radio CRC error (rssi %d dBm): corrupted frame or noise", info.rssi_dbm); }
    else if (n == -2) { S.rx_bad++; LOGW(TAG, "oversized frame ignored"); }
}

int link_send(uint8_t type, const uint8_t *payload, uint8_t len, bool want_ack, link_result_t *res) {
    link_result_t r;
    memset(&r, 0, sizeof r);
    if (len > PROTO_MAX_PAYLOAD || (len && !payload)) return LINK_ERR_ARG;

    proto_frame_t f = {.net_id = L.net_id, .flags = want_ack ? PROTO_FLAG_ACK_REQ : 0, .type = type, .seq = ++g_seq, .len = len};
    if (len) memcpy(f.payload, payload, len);

    uint32_t ack_air_ms = (sx127x_airtime_us(PROTO_MAX_FRAME) + 999) / 1000;
    uint32_t ack_timeout_ms = ack_air_ms + ACK_TURNAROUND_MS + L.ack_margin_ms;
    /* Status polling is periodic already. Retry commands, while giving failed status queries one slot. */
    uint8_t attempts = want_ack && type != MSG_CAMERA_STATUS ? (uint8_t)(1 + L.retries) : 1;
    int rc = LINK_ERR_NO_ACK;

    for (uint8_t a = 1; a <= attempts; a++) {
        r.tries = a;
        uint32_t air_ms = 0, t0 = now_ms();
        LOGI(TAG, "TX %s seq=%u len=%u attempt %u/%u%s", proto_type_str(type), f.seq, len, a, attempts,
             want_ack ? "" : " (no ACK wanted)");
        rc = tx_frame(&f, &air_ms);
        r.airtime_ms = air_ms;
        if (rc != LINK_OK) break;
        if (!want_ack) break;

        W.waiting = true; W.got = false; W.seq = f.seq; W.type = f.type;
        uint32_t deadline = now_ms() + ack_timeout_ms;
        while (!W.got && (int32_t)(deadline - now_ms()) > 0) {
            if (L.yield) L.yield();
            link_poll();
            if (!W.got) sleep_us(300);
        }
        W.waiting = false;
        if (W.got) {
            r.acked = true;
            r.ack_status = W.status;
            r.extra_len = W.extra_len;
            memcpy(r.extra, W.extra, W.extra_len);
            r.ack_rssi_dbm = W.rssi;
            r.ack_snr_q4 = W.snr_q4;
            r.rtt_ms = now_ms() - t0;
            rc = LINK_OK;
            break;
        }
        S.ack_timeouts++;
        LOGW(TAG, "no ACK for seq=%u within %lu ms (attempt %u/%u)", f.seq, (unsigned long)ack_timeout_ms, a, attempts);
        rc = LINK_ERR_NO_ACK;
        if (a < attempts) sleep_ms(RETRY_BACKOFF_MS + (uint32_t)(a * 13u) % 17u);
    }

    if (rc == LINK_OK) {
        S.send_ok++;
        if (want_ack) LOGI(TAG, "send OK: %s seq=%u acked after %u attempt(s), rtt=%lu ms", proto_type_str(type), f.seq, r.tries, (unsigned long)r.rtt_ms);
    } else {
        S.send_fail++;
        LOGE(TAG, "send FAILED: %s seq=%u: %s after %u attempt(s)", proto_type_str(type), f.seq, link_err_str(rc), r.tries);
    }
    if (res) *res = r;
    return rc;
}

void link_log_stats(void) {
    char snr[12];
    link_fmt_snr(snr, sizeof snr, S.last_snr_q4);
    uint32_t used = link_duty_used_ms();
    LOGI(TAG, "stats: tx=%lu (err %lu, blocked %lu) acks_sent=%lu | rx=%lu (bad %lu, crc %lu, dup %lu) | send ok=%lu fail=%lu ack_timeouts=%lu",
         (unsigned long)S.tx_frames, (unsigned long)S.tx_errors, (unsigned long)S.duty_blocked, (unsigned long)S.acks_sent,
         (unsigned long)S.rx_frames, (unsigned long)S.rx_bad, (unsigned long)S.rx_crc_err, (unsigned long)S.rx_dups,
         (unsigned long)S.send_ok, (unsigned long)S.send_fail, (unsigned long)S.ack_timeouts);
    LOGI(TAG, "stats: last rssi=%d dBm snr=%s dB | airtime used last hour: %lu ms (%lu.%02lu%% of the %d%% limit window)",
         S.last_rssi_dbm, snr, (unsigned long)used, (unsigned long)(used / 36000u), (unsigned long)((used % 36000u) / 360u),
         LORA_DUTY_PERCENT);
}
