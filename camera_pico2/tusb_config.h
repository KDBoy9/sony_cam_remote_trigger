#pragma once
/* TinyUSB config: native USB (roothub 0) = CDC device for logs, PIO USB (roothub 1) = host.
 * Same split as Pico-PIO-USB's host_hid_to_device_cdc example. */
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU            OPT_MCU_RP2040      /* also used for RP2350 */
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_PICO
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

/* roothub 0: native USB, device */
#define CFG_TUD_ENABLED         1
#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUD_CDC             1
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  1024
#define CFG_TUD_CDC_EP_BUFSIZE  64
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* roothub 1: PIO USB, host */
#define CFG_TUH_ENABLED         1
#define CFG_TUH_RPI_PIO_USB     1
#define BOARD_TUH_RHPORT        1
#define CFG_TUH_DEVICE_MAX      1
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_API_EDPT_XFER   1
#define CFG_TUH_HUB             0
#define CFG_TUH_CDC             0
#define CFG_TUH_HID             0
#define CFG_TUH_MSC             0
#define CFG_TUH_VENDOR          0

/* Older TinyUSB releases want the root-hub mode macros instead; they are harmless otherwise. */
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)
#define CFG_TUSB_RHPORT1_MODE   (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)