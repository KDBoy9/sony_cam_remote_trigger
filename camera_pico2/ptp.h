#pragma once
/* Portable PTP (ISO 15740) container helpers. Little-endian on the wire. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { PTP_TYPE_CMD = 1, PTP_TYPE_DATA = 2, PTP_TYPE_RESP = 3, PTP_TYPE_EVENT = 4 };
enum { PTP_RC_OK = 0x2001, PTP_RC_SESSION_ALREADY_OPEN = 0x201E };
enum { PTP_OP_OPEN_SESSION = 0x1002, PTP_OP_CLOSE_SESSION = 0x1003 };
#define PTP_HDR_LEN 12u

static inline void ptp_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void ptp_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline uint16_t ptp_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t ptp_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Command container: header + up to 5 uint32 parameters. Returns bytes written. */
static inline uint32_t ptp_put_cmd(uint8_t *b, uint16_t op, uint32_t tid, const uint32_t *par, uint8_t n) {
    ptp_wr32(b, PTP_HDR_LEN + 4u * n);
    ptp_wr16(b + 4, PTP_TYPE_CMD);
    ptp_wr16(b + 6, op);
    ptp_wr32(b + 8, tid);
    for (uint8_t i = 0; i < n; i++) ptp_wr32(b + PTP_HDR_LEN + 4u * i, par[i]);
    return PTP_HDR_LEN + 4u * n;
}

/* Data container: header + payload. Returns bytes written. */
static inline uint32_t ptp_put_data(uint8_t *b, uint16_t op, uint32_t tid, const uint8_t *payload, uint32_t len) {
    ptp_wr32(b, PTP_HDR_LEN + len);
    ptp_wr16(b + 4, PTP_TYPE_DATA);
    ptp_wr16(b + 6, op);
    ptp_wr32(b + 8, tid);
    if (len) memcpy(b + PTP_HDR_LEN, payload, len);
    return PTP_HDR_LEN + len;
}
