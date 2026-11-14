#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One complete PTP transaction: command, optional data-out, optional data-in, response.
 * in/in_len may be NULL to discard any data phase; otherwise *in_len is the capacity on
 * entry and the number of bytes kept on return. Returns false on a transport error or
 * timeout; the PTP response code is stored in *resp_code. */
bool ptp_transact(uint16_t op, const uint32_t *params, uint8_t nparams,
                  const uint8_t *out, uint32_t out_len,
                  uint8_t *in, uint32_t *in_len,
                  uint16_t *resp_code, uint32_t timeout_ms);

bool ptp_usb_ready(void);
uint32_t ptp_usb_object_events(void);   /* count of camera 'new image' events since boot */        /* camera enumerated and PTP interface configured */
void ptp_usb_reset_tid(void);    /* next OpenSession uses transaction id 0, like libgphoto2 */
void ptp_usb_arm_events(void);   /* start draining the interrupt endpoint (camera events) */
void ptp_delay_ms(uint32_t ms);  /* delay that keeps the USB host stack running */
uint32_t ptp_millis(void);