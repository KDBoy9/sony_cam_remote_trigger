#pragma once
#include <stdint.h>
/* Shared status block: core 1 (USB host + camera) writes, core 0 (LED + log console) reads. */
typedef struct {
    volatile uint32_t core1_loops;   /* increments every pass of the core-1 loop: proves core 1 is alive */
    volatile uint32_t bus_resets;    /* periodic host bus-reset pulses issued while nothing is attached */
    volatile uint8_t  state;         /* 0 nothing attached, 1 device attached (not PTP), 2 PTP ready, 3 session open */
    volatile uint8_t  devices;       /* devices currently mounted on the PIO USB host */
    volatile uint16_t vid, pid;      /* of the last mounted device */
} app_status_t;
extern app_status_t g_status;
