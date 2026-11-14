#pragma once
/* Over-the-air frame format. Designed so the payload can grow (several bytes now, camera settings later).
 *
 *   [0] magic 0xCA   [1] net_id   [2] version<<4 | flags   [3] type   [4] seq   [5] payload length N
 *   [6 .. 6+N-1] payload          [6+N, 7+N] CRC-16/CCITT-FALSE (little endian) over bytes 0 .. 5+N
 *
 * flags: bit0 = sender wants an ACK, bit1 = this frame IS an ACK (type and seq echo the frame it answers).
 * ACK prefix (4 bytes): status, RSSI of the frame it answers (int16 LE, dBm), SNR (int8, 0.25 dB units), then optional application data. */
#include <stddef.h>
#include <stdint.h>

#define PROTO_MAGIC       0xCA
#define PROTO_VERSION     2
#define PROTO_HDR_LEN     6
#define PROTO_CRC_LEN     2
#define PROTO_MAX_PAYLOAD 48
#define PROTO_MAX_FRAME   (PROTO_HDR_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN)

enum { PROTO_FLAG_ACK_REQ = 0x01, PROTO_FLAG_IS_ACK = 0x02 };

enum {
    MSG_PING    = 0x01,   /* payload: anything, ignored */
    MSG_BYTE    = 0x02,   /* payload: 1 byte (the first link test) */
    MSG_PARAMS  = 0x03,   /* payload: repeated { id:u8, value:u16 LE } */
    MSG_SHUTTER = 0x04,   /* camera command, see camera_wire.h */
    MSG_CAMERA_STATUS = 0x05  /* empty request, status in ACK */
};

/* Parameter ids for MSG_PARAMS. For now the test page sends A, B, C. Later: shutter, aperture, ISO, timer. */
enum { PARAM_A = 1, PARAM_B = 2, PARAM_C = 3, PARAM_TIMER_S = 4 };
#define PROTO_PARAM_LEN 3

/* ACK status codes */
enum { ACK_OK = 0, ACK_UNKNOWN_TYPE = 1, ACK_BAD_PAYLOAD = 2, ACK_BUSY = 3, ACK_NOT_READY = 4 };
#define PROTO_ACK_PAYLOAD_LEN 4

typedef struct {
    uint8_t net_id, flags, type, seq, len;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} proto_frame_t;

enum { PROTO_OK = 0, PROTO_ERR_SHORT = -1, PROTO_ERR_MAGIC = -2, PROTO_ERR_VERSION = -3,
       PROTO_ERR_LEN = -4, PROTO_ERR_CRC = -5, PROTO_ERR_NET = -6 };

uint16_t proto_crc16(const uint8_t *data, size_t n);
size_t proto_encode(const proto_frame_t *f, uint8_t *out, size_t cap);          /* frame length, 0 on error */
int proto_decode(const uint8_t *in, size_t n, proto_frame_t *f, uint8_t net_id); /* PROTO_OK or PROTO_ERR_* */
const char *proto_err_str(int err);
const char *proto_type_str(uint8_t type);
/* Append one { id, value } pair to buf at pos; returns the new position, or 0 if it does not fit. */
size_t proto_param_put(uint8_t *buf, size_t cap, size_t pos, uint8_t id, uint16_t value);
