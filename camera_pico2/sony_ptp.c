/* Sony PTP session + shutter sequence, reproduced from a Wireshark/usbmon capture of a
 * working Linux client talking to an A7C II (PC Remote mode). Observed order:
 *
 *   OpenSession(1)            tid 0
 *   GetDeviceInfo             0x1001
 *   SDIO_Connect(1,0,0)       0x9201   (returns an 8-byte data phase)
 *   SDIO_Connect(2,0,0)
 *   SDIO_GetExtDeviceInfo(0x12C, 1)    0x9202 (returns ~968 bytes)
 *   SDIO_Connect(3,0,0)
 *   SetControlDeviceA(0xD25A) = 0x01   0x9205, 1-byte data
 *   GetStorageIDs             0x1004   (camera answered 0x2013; ignored)
 *   SetControlDeviceB(0xD2C1) = 2      S1 down (half press)
 *   SetControlDeviceB(0xD2C2) = 2      S2 down (shutter)
 *   GetAllExtDevicePropInfo   0x9209 repeatedly for ~1 s (10 KB reply each)
 *   SetControlDeviceB(0xD2C2) = 1      S2 up
 *   SetControlDeviceB(0xD2C1) = 1      S1 up
 *   CloseSession              0x1003
 */
#include "sony_ptp.h"
#include "ptp.h"
#include "ptp_transport.h"

#define OP_GET_DEVICE_INFO     0x1001
#define OP_GET_STORAGE_IDS     0x1004
#define OP_SDIO_CONNECT        0x9201
#define OP_SDIO_GET_EXT_INFO   0x9202
#define OP_SDIO_CTRL_A         0x9205
#define OP_SDIO_CTRL_B         0x9207
#define OP_SDIO_GET_ALL_PROPS  0x9209

#define PROP_PC_REMOTE_ENABLE  0xD25A
#define PROP_S1                0xD2C1
#define PROP_S2                0xD2C2
#define PROP_REQUEST_ONE_SHOT  0xD2C7
#define KEY_UP   1
#define KEY_DOWN 2

static uint16_t g_rc;
uint16_t sony_last_rc(void) { return g_rc; }

static bool op(uint16_t code, const uint32_t *p, uint8_t n, const uint8_t *out, uint32_t outlen) {
    uint16_t rc = 0;
    if (!ptp_transact(code, p, n, out, outlen, NULL, NULL, &rc, 3000)) { g_rc = 0xFFFF; return false; }
    g_rc = rc;
    return rc == PTP_RC_OK;
}

void sony_session_close(void) {
    uint16_t rc;
    ptp_transact(PTP_OP_CLOSE_SESSION, NULL, 0, NULL, 0, NULL, NULL, &rc, 1000);
}

bool sony_session_open(void) {
    uint16_t rc = 0;
    uint32_t sid = 1;

    ptp_usb_reset_tid();
    if (!ptp_transact(PTP_OP_OPEN_SESSION, &sid, 1, NULL, 0, NULL, NULL, &rc, 3000)) { g_rc = 0xFFFF; return false; }
    g_rc = rc;
    if (rc != PTP_RC_OK && rc != PTP_RC_SESSION_ALREADY_OPEN) return false;

    if (!op(OP_GET_DEVICE_INFO, NULL, 0, NULL, 0)) return false;

    const uint32_t c1[3] = {1, 0, 0}, c2[3] = {2, 0, 0}, c3[3] = {3, 0, 0};
    if (!op(OP_SDIO_CONNECT, c1, 3, NULL, 0)) return false;
    if (!op(OP_SDIO_CONNECT, c2, 3, NULL, 0)) return false;

    uint32_t ext[2] = {0x012C, 1};          /* what the capture shows for the A7C II */
    if (!op(OP_SDIO_GET_EXT_INFO, ext, 2, NULL, 0)) {
        ext[0] = 0x00C8;                    /* older protocol version, in case 0x12C is refused */
        if (!op(OP_SDIO_GET_EXT_INFO, ext, 2, NULL, 0)) return false;
    }
    if (!op(OP_SDIO_CONNECT, c3, 3, NULL, 0)) return false;

    uint32_t prop = PROP_PC_REMOTE_ENABLE;
    const uint8_t one = 1;
    if (!op(OP_SDIO_CTRL_A, &prop, 1, &one, 1)) return false;

    (void)op(OP_GET_STORAGE_IDS, NULL, 0, NULL, 0);   /* camera returns 0x2013; harmless */
    return true;
}

bool sony_ctrl(uint16_t prop, uint16_t value) {
    uint32_t p = prop;
    uint8_t d[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return op(OP_SDIO_CTRL_B, &p, 1, d, 2);
}

bool sony_shoot(void) {
    bool ok = sony_ctrl(PROP_S1, KEY_DOWN);
    ok = ok && sony_ctrl(PROP_S2, KEY_DOWN);

    /* The reference client keeps polling the property list while the camera focuses and
     * exposes. Each reply is ~10 KB and is discarded here. */
    uint32_t t0 = ptp_millis();
    while (ok && (ptp_millis() - t0) < SONY_HOLD_MS) {
        ok = op(OP_SDIO_GET_ALL_PROPS, NULL, 0, NULL, 0);
    }


    uint16_t failure = g_rc;
    bool released = sony_release();
    if (!ok && released) g_rc = failure;
    return ok && released;
}

bool sony_bulb_start(void) {
    /* libgphoto2 Sony bulb sequence. The camera must already be in BULB. */
    return sony_ctrl(PROP_S1, KEY_UP) && sony_ctrl(PROP_REQUEST_ONE_SHOT, KEY_DOWN);
}

bool sony_release(void) {
    bool full_up = sony_ctrl(PROP_S2, KEY_UP);
    uint16_t full_rc = g_rc;
    bool half_up = sony_ctrl(PROP_S1, KEY_UP);
    if (!full_up) g_rc = full_rc;
    return full_up && half_up;
}
