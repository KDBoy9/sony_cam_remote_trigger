/* PTP (USB Still Image class) host driver for TinyUSB + blocking transaction layer.
 * Runs on core 1 together with tuh_task(). Never call these from a TinyUSB callback.
 *
 * Written for the TinyUSB bundled with pico-sdk 2.3.x, where the class driver callbacks are
 *   bool init(void);  bool deinit(void);
 *   bool open(rhport, dev_addr, itf_desc, uint16_t max_len);   (returns true if it claimed the interface)
 * Set PTP_NEW_TUSB_API to 0 only for an older TinyUSB (void init, uint16_t open). */
#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "ptp.h"
#include "ptp_transport.h"
#include "applog.h"
#include "status.h"

#ifndef PTP_NEW_TUSB_API
#define PTP_NEW_TUSB_API 1
#endif

#define PTP_USB_CLASS 0x06   /* Still Image */

static struct {
    bool mounted, ready;
    uint8_t dev_addr, itf_num, ep_in, ep_out, ep_int;
    uint32_t tid;
    volatile bool done;
    volatile xfer_result_t result;
    volatile uint32_t xferred;
    uint32_t events;
    uint16_t last_event;
    uint16_t mps_in;          /* bulk-IN max packet size (64 at full speed) */
    uint32_t obj_events;      /* camera 'ObjectAdded' (0xC201) events seen: a new image exists */
} s;

static uint8_t tx_buf[64] __attribute__((aligned(4)));
static uint8_t rx_buf[512] __attribute__((aligned(4)));
static uint8_t int_buf[64] __attribute__((aligned(4)));
static uint8_t sink[64];

/* ---- device-level diagnostics (TinyUSB weak callbacks) ------------------- */
void tuh_mount_cb(uint8_t daddr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(daddr, &vid, &pid);
    g_status.devices++;
    g_status.vid = vid;
    g_status.pid = pid;
    tusb_speed_t spd = tuh_speed_get(daddr);   /* what the PIO USB host decided at attach: full or low speed */
    applog("device mounted: addr %u VID:PID %04x:%04x, host sees it as %s speed", daddr, vid, pid,
           spd == TUSB_SPEED_FULL ? "FULL" : spd == TUSB_SPEED_LOW ? "LOW" : spd == TUSB_SPEED_HIGH ? "HIGH" : "unknown");
}

void tuh_umount_cb(uint8_t daddr) {
    if (g_status.devices) g_status.devices--;
    g_status.vid = g_status.pid = 0;
    applog("device unmounted: addr %u", daddr);
}

/* ---- helpers ------------------------------------------------------------ */
static bool xfer_start(uint8_t ep, uint8_t *buf, uint16_t len) {
    if (!usbh_edpt_claim(s.dev_addr, ep)) return false;
    if (!usbh_edpt_xfer(s.dev_addr, ep, buf, len)) {
        usbh_edpt_release(s.dev_addr, ep);
        return false;
    }
    return true;
}

/* ---- TinyUSB class driver ----------------------------------------------- */
#if PTP_NEW_TUSB_API
#define OPEN_FAIL false
static bool ptp_init(void) { memset(&s, 0, sizeof s); return true; }
static bool ptp_deinit(void) { return true; }
static bool ptp_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *itf, uint16_t max_len) {
#else
#define OPEN_FAIL 0
static void ptp_init(void) { memset(&s, 0, sizeof s); }
static uint16_t ptp_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *itf, uint16_t max_len) {
#endif
    (void)rhport;
    applog("interface offered: class %02x/%02x/%02x, %u endpoints", itf->bInterfaceClass,
           itf->bInterfaceSubClass, itf->bInterfaceProtocol, itf->bNumEndpoints);
    TU_VERIFY(itf->bInterfaceClass == PTP_USB_CLASS && itf->bInterfaceSubClass == 1, OPEN_FAIL);
    TU_VERIFY(!s.mounted, OPEN_FAIL);
    uint16_t drv_len = (uint16_t)(sizeof(tusb_desc_interface_t) + itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
    TU_VERIFY(max_len >= drv_len, OPEN_FAIL);

    uint8_t const *p = tu_desc_next(itf);
    for (uint8_t i = 0; i < itf->bNumEndpoints; i++) {
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
        TU_VERIFY(ep->bDescriptorType == TUSB_DESC_ENDPOINT, OPEN_FAIL);
        TU_VERIFY(tuh_edpt_open(dev_addr, ep), OPEN_FAIL);
        if (ep->bmAttributes.xfer == TUSB_XFER_BULK) {
            if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) { s.ep_in = ep->bEndpointAddress; s.mps_in = tu_edpt_packet_size(ep); }
            else s.ep_out = ep->bEndpointAddress;
        } else if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
            s.ep_int = ep->bEndpointAddress;
        }
        p = tu_desc_next(p);
    }
    s.dev_addr = dev_addr;
    s.itf_num = itf->bInterfaceNumber;
    s.mounted = true;
#if PTP_NEW_TUSB_API
    return true;
#else
    return drv_len;
#endif
}

static bool ptp_set_config(uint8_t dev_addr, uint8_t itf_num) {
    s.ready = (s.ep_in && s.ep_out);
    usbh_driver_set_config_complete(dev_addr, itf_num);
    return true;
}

static bool ptp_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)dev_addr;
    if (ep_addr == s.ep_int && s.ep_int) {          /* camera event: count it and keep listening */
        if (result == XFER_RESULT_SUCCESS) {
            s.events++;
            if (xferred_bytes >= 8) {
                uint16_t code = ptp_rd16(int_buf + 6);
                uint32_t p1 = (xferred_bytes >= 16) ? ptp_rd32(int_buf + 12) : 0;
                s.last_event = code;
                if (code == 0xC201) s.obj_events++;                 /* ObjectAdded: the camera stored a new image */
                if (code != 0xC203) applog("camera event 0x%04X p1=0x%08lX", code, (unsigned long)p1);
            }
            if (s.ready) xfer_start(s.ep_int, int_buf, sizeof int_buf);
        }
        return true;
    }
    s.result = result;
    s.xferred = xferred_bytes;
    s.done = true;
    return true;
}

static void ptp_close(uint8_t dev_addr) {
    (void)dev_addr;
    s.mounted = s.ready = false;
    s.result = XFER_RESULT_FAILED;
    s.done = true;   /* unblock anything waiting */
}

static usbh_class_driver_t const ptp_driver = {
    .init = ptp_init,
#if PTP_NEW_TUSB_API
    .deinit = ptp_deinit,
#endif
    .open = ptp_open, .set_config = ptp_set_config,
    .xfer_cb = ptp_xfer_cb, .close = ptp_close,
};

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &ptp_driver;
}

/* ---- blocking layer (core 1 main loop only) ------------------------------ */
uint32_t ptp_millis(void) { return to_ms_since_boot(get_absolute_time()); }

void ptp_delay_ms(uint32_t ms) {
    absolute_time_t t = make_timeout_time_ms(ms);
    while (absolute_time_diff_us(get_absolute_time(), t) > 0) tuh_task();
}

static bool wait_xfer(uint32_t timeout_ms) {
    absolute_time_t t = make_timeout_time_ms(timeout_ms);
    while (!s.done) {
        tuh_task();
        if (absolute_time_diff_us(get_absolute_time(), t) < 0) {
            applog("USB transfer timeout");
            return false;
        }
    }
    return s.result == XFER_RESULT_SUCCESS;
}

static bool bulk_out(const uint8_t *buf, uint32_t len, uint32_t timeout_ms) {
    if (!s.ready) return false;
    s.done = false;
    if (!xfer_start(s.ep_out, (uint8_t *)buf, (uint16_t)len)) return false;
    return wait_xfer(timeout_ms);
}

static bool bulk_in(uint8_t *buf, uint32_t cap, uint32_t timeout_ms) {
    if (!s.ready) return false;
    s.done = false;
    if (!xfer_start(s.ep_in, buf, (uint16_t)cap)) return false;
    return wait_xfer(timeout_ms);
}

/* Read one PTP container. Payload bytes beyond cap are read and discarded.
 * A container whose length is an exact multiple of the packet size is followed by a zero-length packet; that ZLP is
 * simply skipped when it shows up at the start of the next read, so the stream can never get out of step. */
static bool read_container(uint16_t *type, uint16_t *code, uint8_t *payload, uint32_t cap,
                           uint32_t *kept_out, uint32_t timeout_ms) {
    uint32_t total = 0, got = 0, kept = 0;
    bool first = true;
    uint16_t mps = s.mps_in ? s.mps_in : 64;
    for (;;) {
        if (!bulk_in(rx_buf, sizeof rx_buf, timeout_ms)) return false;
        uint32_t n = s.xferred, off = 0;
        if (first) {
            if (n == 0) continue;                       /* ZLP that terminated the previous container */
            if (n < PTP_HDR_LEN) return false;
            total = ptp_rd32(rx_buf);
            *type = ptp_rd16(rx_buf + 4);
            *code = ptp_rd16(rx_buf + 6);
            off = PTP_HDR_LEN;
            first = false;
        }
        uint32_t take = n - off;
        if (kept + take > cap) take = (cap > kept) ? cap - kept : 0;
        if (take) memcpy(payload + kept, rx_buf + off, take);
        kept += take;
        got += n;
        if (got >= total) break;
        if (n < mps) return false;                      /* short packet before the container ended */
    }
    *kept_out = kept;
    return true;
}

bool ptp_usb_ready(void) { return s.ready; }
uint32_t ptp_usb_object_events(void) { return s.obj_events; }
void ptp_usb_reset_tid(void) { s.tid = 0; }

void ptp_usb_arm_events(void) {
    if (s.ready && s.ep_int) xfer_start(s.ep_int, int_buf, sizeof int_buf);
}

bool ptp_transact(uint16_t op, const uint32_t *params, uint8_t nparams,
                  const uint8_t *out, uint32_t out_len,
                  uint8_t *in, uint32_t *in_len,
                  uint16_t *resp_code, uint32_t timeout_ms) {
    if (!s.ready || nparams > 5 || out_len > sizeof tx_buf - PTP_HDR_LEN) return false;
    /* libgphoto2 (and the capture) use transaction id 0 for OpenSession, then 1, 2, 3 ... */
    uint32_t tid = (op == PTP_OP_OPEN_SESSION) ? 0 : ++s.tid;

    uint32_t n = ptp_put_cmd(tx_buf, op, tid, params, nparams);
    if (!bulk_out(tx_buf, n, timeout_ms)) return false;
    if (out) {
        n = ptp_put_data(tx_buf, op, tid, out, out_len);
        if (!bulk_out(tx_buf, n, timeout_ms)) return false;
    }

    bool keep = (in && in_len);
    uint8_t *dst = keep ? in : sink;
    uint32_t cap = keep ? *in_len : sizeof sink;
    uint32_t got_total = 0;

    for (;;) {
        uint16_t type = 0, code = 0;
        uint32_t kept = 0;
        if (!read_container(&type, &code, dst + got_total, cap - got_total, &kept, timeout_ms)) return false;
        if (type == PTP_TYPE_DATA) {
            got_total += kept;
        } else if (type == PTP_TYPE_RESP) {
            if (keep) *in_len = got_total;
            *resp_code = code;
            return true;
        }
    }
}