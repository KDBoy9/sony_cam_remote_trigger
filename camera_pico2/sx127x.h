#pragma once
/* Minimal SX1276/77/78 driver, LoRa mode only (Ai-Thinker Ra-02 = SX1278, 433 MHz). Polling, no interrupts. */
#include <stdbool.h>
#include <stdint.h>
#include "hardware/spi.h"

/* bandwidth index for sx127x_cfg_t.bw_idx */
enum { SX_BW_7K8 = 0, SX_BW_10K4, SX_BW_15K6, SX_BW_20K8, SX_BW_31K25, SX_BW_41K7,
       SX_BW_62K5, SX_BW_125K, SX_BW_250K, SX_BW_500K };

typedef struct {
    spi_inst_t *spi;
    unsigned pin_sck, pin_mosi, pin_miso, pin_cs, pin_rst, pin_dio0;
    uint32_t freq_hz;
    uint8_t bw_idx;       /* SX_BW_* */
    uint8_t sf;           /* 7..12 */
    uint8_t cr;           /* 5..8 meaning 4/5 .. 4/8 */
    uint16_t preamble;
    uint8_t sync_word;
    int8_t tx_dbm;        /* 2..17 on the PA_BOOST pin, which is what the Ra-02 uses */
} sx127x_cfg_t;

typedef struct {
    int16_t rssi_dbm;     /* packet RSSI */
    int8_t snr_q4;        /* SNR in 0.25 dB units */
} sx127x_rx_info_t;

enum { SX_OK = 0, SX_ERR_CHIP = -1, SX_ERR_TIMEOUT = -2, SX_ERR_ARG = -3 };

int sx127x_init(const sx127x_cfg_t *cfg);                       /* 0 or SX_ERR_*; logs a wiring checklist on failure */
int sx127x_transmit(const uint8_t *buf, uint8_t len, uint32_t timeout_ms);   /* blocking; re-arms RX afterwards */
int sx127x_start_rx(void);
/* >0: packet length; 0: nothing; -1: CRC error; -2: packet larger than cap. */
int sx127x_poll_rx(uint8_t *buf, uint8_t cap, sx127x_rx_info_t *info);
uint32_t sx127x_airtime_us(uint8_t payload_len);
uint8_t sx127x_opmode(void);
bool sx127x_dio0(void);
void sx127x_dump(void);                                          /* print key registers */
