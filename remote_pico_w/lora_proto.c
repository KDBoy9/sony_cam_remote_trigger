#include <string.h>
#include "lora_proto.h"

uint16_t proto_crc16(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

size_t proto_encode(const proto_frame_t *f, uint8_t *out, size_t cap) {
    if (!f || !out || f->len > PROTO_MAX_PAYLOAD) return 0;
    size_t total = PROTO_HDR_LEN + f->len + PROTO_CRC_LEN;
    if (cap < total) return 0;
    out[0] = PROTO_MAGIC;
    out[1] = f->net_id;
    out[2] = (uint8_t)((PROTO_VERSION << 4) | (f->flags & 0x0F));
    out[3] = f->type;
    out[4] = f->seq;
    out[5] = f->len;
    if (f->len) memcpy(out + PROTO_HDR_LEN, f->payload, f->len);
    uint16_t crc = proto_crc16(out, PROTO_HDR_LEN + f->len);
    out[PROTO_HDR_LEN + f->len] = (uint8_t)crc;
    out[PROTO_HDR_LEN + f->len + 1] = (uint8_t)(crc >> 8);
    return total;
}

int proto_decode(const uint8_t *in, size_t n, proto_frame_t *f, uint8_t net_id) {
    if (n < PROTO_HDR_LEN + PROTO_CRC_LEN) return PROTO_ERR_SHORT;
    if (in[0] != PROTO_MAGIC) return PROTO_ERR_MAGIC;
    if ((in[2] >> 4) != PROTO_VERSION) return PROTO_ERR_VERSION;
    uint8_t len = in[5];
    if (len > PROTO_MAX_PAYLOAD || n != (size_t)(PROTO_HDR_LEN + len + PROTO_CRC_LEN)) return PROTO_ERR_LEN;
    uint16_t want = (uint16_t)(in[PROTO_HDR_LEN + len] | (in[PROTO_HDR_LEN + len + 1] << 8));
    if (proto_crc16(in, PROTO_HDR_LEN + len) != want) return PROTO_ERR_CRC;
    if (in[1] != net_id) return PROTO_ERR_NET;
    f->net_id = in[1];
    f->flags = in[2] & 0x0F;
    f->type = in[3];
    f->seq = in[4];
    f->len = len;
    if (len) memcpy(f->payload, in + PROTO_HDR_LEN, len);
    return PROTO_OK;
}

const char *proto_err_str(int e) {
    switch (e) {
        case PROTO_OK: return "ok";
        case PROTO_ERR_SHORT: return "too short";
        case PROTO_ERR_MAGIC: return "bad magic (not our protocol)";
        case PROTO_ERR_VERSION: return "bad version";
        case PROTO_ERR_LEN: return "length mismatch";
        case PROTO_ERR_CRC: return "bad CRC";
        case PROTO_ERR_NET: return "other network id";
        default: return "?";
    }
}

const char *proto_type_str(uint8_t t) {
    switch (t) {
        case MSG_PING: return "PING";
        case MSG_BYTE: return "BYTE";
        case MSG_PARAMS: return "PARAMS";
        case MSG_SHUTTER: return "SHUTTER";
        case MSG_CAMERA_STATUS: return "CAMERA_STATUS";
        default: return "UNKNOWN";
    }
}

size_t proto_param_put(uint8_t *buf, size_t cap, size_t pos, uint8_t id, uint16_t value) {
    if (pos + PROTO_PARAM_LEN > cap) return 0;
    buf[pos] = id;
    buf[pos + 1] = (uint8_t)value;
    buf[pos + 2] = (uint8_t)(value >> 8);
    return pos + PROTO_PARAM_LEN;
}
