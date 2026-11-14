#include <string.h>
#include "sx127x.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "log.h"

#define TAG "sx127x"

/* Register map (LoRa mode) */
enum {
    REG_FIFO = 0x00, REG_OPMODE = 0x01, REG_FRF_MSB = 0x06, REG_FRF_MID = 0x07, REG_FRF_LSB = 0x08,
    REG_PA_CONFIG = 0x09, REG_OCP = 0x0B, REG_LNA = 0x0C, REG_FIFO_ADDR_PTR = 0x0D,
    REG_FIFO_TX_BASE = 0x0E, REG_FIFO_RX_BASE = 0x0F, REG_FIFO_RX_CURRENT = 0x10,
    REG_IRQ_MASK = 0x11, REG_IRQ_FLAGS = 0x12, REG_RX_NB_BYTES = 0x13, REG_PKT_SNR = 0x19,
    REG_PKT_RSSI = 0x1A, REG_MODEM_CFG1 = 0x1D, REG_MODEM_CFG2 = 0x1E, REG_PREAMBLE_MSB = 0x20,
    REG_PREAMBLE_LSB = 0x21, REG_PAYLOAD_LEN = 0x22, REG_MAX_PAYLOAD = 0x23, REG_MODEM_CFG3 = 0x26,
    REG_DETECT_OPT = 0x31, REG_DETECT_THRESH = 0x37, REG_SYNC_WORD = 0x39, REG_DIO_MAP1 = 0x40,
    REG_VERSION = 0x42, REG_PA_DAC = 0x4D
};
enum { MODE_SLEEP = 0, MODE_STDBY = 1, MODE_TX = 3, MODE_RX_CONT = 5 };
enum { IRQ_CRC_ERR = 0x20, IRQ_RX_DONE = 0x40, IRQ_TX_DONE = 0x08 };
#define LONG_RANGE_MODE 0x80

static sx127x_cfg_t C;
static uint8_t lf_bit;          /* 0x08 for the low-frequency port (< 525 MHz), else 0 */
static int rssi_offset;         /* 164 (LF port) or 157 (HF port) */
static bool ready;

static const uint32_t BW_HZ[10] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000};

static inline void cs_low(void)  { gpio_put(C.pin_cs, 0); }
static inline void cs_high(void) { gpio_put(C.pin_cs, 1); }

static uint8_t rd(uint8_t reg) {
    uint8_t tx = reg & 0x7F, v = 0;
    cs_low();
    spi_write_blocking(C.spi, &tx, 1);
    spi_read_blocking(C.spi, 0x00, &v, 1);
    cs_high();
    return v;
}

static void wr(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = {(uint8_t)(reg | 0x80), val};
    cs_low();
    spi_write_blocking(C.spi, tx, 2);
    cs_high();
}

static void rd_burst(uint8_t reg, uint8_t *dst, uint8_t n) {
    uint8_t tx = reg & 0x7F;
    cs_low();
    spi_write_blocking(C.spi, &tx, 1);
    spi_read_blocking(C.spi, 0x00, dst, n);
    cs_high();
}

static void wr_burst(uint8_t reg, const uint8_t *src, uint8_t n) {
    uint8_t tx = reg | 0x80;
    cs_low();
    spi_write_blocking(C.spi, &tx, 1);
    spi_write_blocking(C.spi, src, n);
    cs_high();
}

static void set_mode(uint8_t mode) { wr(REG_OPMODE, LONG_RANGE_MODE | lf_bit | mode); }

int sx127x_init(const sx127x_cfg_t *cfg) {
    if (!cfg || cfg->sf < 7 || cfg->sf > 12 || cfg->cr < 5 || cfg->cr > 8 || cfg->bw_idx > 9) return SX_ERR_ARG;
    C = *cfg;
    ready = false;
    lf_bit = (C.freq_hz < 525000000UL) ? 0x08 : 0x00;
    rssi_offset = lf_bit ? 164 : 157;

    spi_init(C.spi, 1000 * 1000);   /* 1 MHz: forgiving for jumper wires */
    spi_set_format(C.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(C.pin_sck, GPIO_FUNC_SPI);
    gpio_set_function(C.pin_mosi, GPIO_FUNC_SPI);
    gpio_set_function(C.pin_miso, GPIO_FUNC_SPI);
    gpio_init(C.pin_cs);  gpio_set_dir(C.pin_cs, GPIO_OUT);  gpio_put(C.pin_cs, 1);
    gpio_init(C.pin_rst); gpio_set_dir(C.pin_rst, GPIO_OUT); gpio_put(C.pin_rst, 1);
    gpio_init(C.pin_dio0); gpio_set_dir(C.pin_dio0, GPIO_IN);

    LOGI(TAG, "reset (RST=GP%u), SPI SCK=GP%u MOSI=GP%u MISO=GP%u CS=GP%u DIO0=GP%u",
         C.pin_rst, C.pin_sck, C.pin_mosi, C.pin_miso, C.pin_cs, C.pin_dio0);
    gpio_put(C.pin_rst, 0);
    sleep_ms(2);
    gpio_put(C.pin_rst, 1);
    sleep_ms(10);

    uint8_t ver = rd(REG_VERSION);
    LOGI(TAG, "RegVersion = 0x%02X (SX1276/77/78 report 0x12)", ver);
    if (ver != 0x12) {
        LOGE(TAG, "radio not found. Check, in this order: 3.3 V on the module (not 5 V), GND, then MISO/MOSI not swapped,");
        LOGE(TAG, "SCK, NSS (CS), RESET wiring. 0x00 usually means MISO floating or no power; 0xFF means MISO stuck high.");
        return SX_ERR_CHIP;
    }

    wr(REG_OPMODE, 0x00);                      /* FSK sleep, so LoRa bit may be changed */
    sleep_ms(5);
    set_mode(MODE_SLEEP);                      /* LoRa sleep */
    sleep_ms(5);
    uint8_t om = rd(REG_OPMODE);
    if ((om & LONG_RANGE_MODE) == 0) {
        LOGE(TAG, "could not enter LoRa mode (RegOpMode=0x%02X)", om);
        return SX_ERR_CHIP;
    }

    uint64_t frf = ((uint64_t)C.freq_hz << 19) / 32000000ULL;
    wr(REG_FRF_MSB, (uint8_t)(frf >> 16));
    wr(REG_FRF_MID, (uint8_t)(frf >> 8));
    wr(REG_FRF_LSB, (uint8_t)frf);

    int p = C.tx_dbm;
    if (p < 2) p = 2;
    if (p > 17) p = 17;
    wr(REG_PA_CONFIG, (uint8_t)(0x80 | (p - 2)));   /* PA_BOOST, Pout = 2 + OutputPower dBm */
    wr(REG_PA_DAC, 0x84);                            /* normal (not +20 dBm) */
    wr(REG_OCP, 0x2B);                               /* over-current protection on, 100 mA */

    wr(REG_LNA, rd(REG_LNA) | 0x03);                 /* max LNA gain/boost */
    wr(REG_MODEM_CFG1, (uint8_t)((C.bw_idx << 4) | ((C.cr - 4) << 1)));   /* explicit header */
    wr(REG_MODEM_CFG2, (uint8_t)((C.sf << 4) | 0x04));                    /* payload CRC on */
    double tsym_ms = 1000.0 * (double)(1u << C.sf) / (double)BW_HZ[C.bw_idx];
    wr(REG_MODEM_CFG3, (uint8_t)(0x04 | (tsym_ms > 16.0 ? 0x08 : 0x00)));  /* AGC auto + low-data-rate optimise */
    wr(REG_DETECT_OPT, (uint8_t)((rd(REG_DETECT_OPT) & 0xF8) | 0x03));    /* SF7..12 */
    wr(REG_DETECT_THRESH, 0x0A);
    wr(REG_PREAMBLE_MSB, (uint8_t)(C.preamble >> 8));
    wr(REG_PREAMBLE_LSB, (uint8_t)C.preamble);
    wr(REG_SYNC_WORD, C.sync_word);
    wr(REG_MAX_PAYLOAD, 0xFF);
    wr(REG_FIFO_TX_BASE, 0x00);
    wr(REG_FIFO_RX_BASE, 0x00);
    wr(REG_IRQ_MASK, 0x00);
    set_mode(MODE_STDBY);

    LOGI(TAG, "configured: %lu Hz, BW %lu Hz, SF%u, CR 4/%u, preamble %u, sync 0x%02X, +%d dBm, LDRO %s",
         (unsigned long)C.freq_hz, (unsigned long)BW_HZ[C.bw_idx], C.sf, C.cr, C.preamble, C.sync_word, p,
         tsym_ms > 16.0 ? "on" : "off");
    LOGI(TAG, "airtime of a 10-byte frame: %lu us", (unsigned long)sx127x_airtime_us(10));
    ready = true;
    return sx127x_start_rx();
}

int sx127x_start_rx(void) {
    wr(REG_DIO_MAP1, 0x00);          /* DIO0 = RxDone */
    wr(REG_IRQ_FLAGS, 0xFF);
    wr(REG_FIFO_ADDR_PTR, 0x00);
    set_mode(MODE_RX_CONT);
    return SX_OK;
}

int sx127x_transmit(const uint8_t *buf, uint8_t len, uint32_t timeout_ms) {
    if (!ready || !buf || len == 0) return SX_ERR_ARG;
    if (rd(REG_IRQ_FLAGS) & IRQ_RX_DONE) LOGW(TAG, "an unread received packet was discarded to transmit");
    set_mode(MODE_STDBY);
    wr(REG_FIFO_ADDR_PTR, 0x00);
    wr_burst(REG_FIFO, buf, len);
    wr(REG_PAYLOAD_LEN, len);
    wr(REG_DIO_MAP1, 0x40);          /* DIO0 = TxDone */
    wr(REG_IRQ_FLAGS, 0xFF);
    set_mode(MODE_TX);

    absolute_time_t t0 = get_absolute_time();
    int rc = SX_OK;
    while (!(rd(REG_IRQ_FLAGS) & IRQ_TX_DONE)) {
        if (to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0) > timeout_ms) { rc = SX_ERR_TIMEOUT; break; }
        sleep_us(200);
    }
    wr(REG_IRQ_FLAGS, 0xFF);
    if (rc) LOGE(TAG, "TX timeout after %lu ms (RegOpMode=0x%02X)", (unsigned long)timeout_ms, rd(REG_OPMODE));
    sx127x_start_rx();
    return rc;
}

int sx127x_poll_rx(uint8_t *buf, uint8_t cap, sx127x_rx_info_t *info) {
    uint8_t flags = rd(REG_IRQ_FLAGS);
    if (!(flags & IRQ_RX_DONE)) return 0;
    int8_t snr_q4 = (int8_t)rd(REG_PKT_SNR);
    int rssi = (int)rd(REG_PKT_RSSI) - rssi_offset;
    if (snr_q4 < 0) rssi += snr_q4 / 4;      /* below the noise floor the raw RSSI reads high */
    if (info) { info->rssi_dbm = (int16_t)rssi; info->snr_q4 = snr_q4; }

    if (flags & IRQ_CRC_ERR) {
        wr(REG_IRQ_FLAGS, 0xFF);
        return -1;
    }
    uint8_t n = rd(REG_RX_NB_BYTES);
    uint8_t cur = rd(REG_FIFO_RX_CURRENT);
    wr(REG_IRQ_FLAGS, 0xFF);
    if (n > cap) return -2;
    wr(REG_FIFO_ADDR_PTR, cur);
    rd_burst(REG_FIFO, buf, n);
    return n;
}

uint32_t sx127x_airtime_us(uint8_t pl) {
    double tsym = (double)(1u << C.sf) / (double)BW_HZ[C.bw_idx];   /* seconds */
    int de = (tsym > 0.016) ? 1 : 0;
    double t_pre = ((double)C.preamble + 4.25) * tsym;
    int num = 8 * (int)pl - 4 * (int)C.sf + 28 + 16;                 /* CRC on, explicit header */
    int den = 4 * ((int)C.sf - 2 * de);
    int n = (num > 0) ? ((num + den - 1) / den) * ((int)C.cr) : 0;   /* cr = 4 + (1..4) = C.cr - 4 + 4 */
    double t_pay = (8.0 + n) * tsym;
    return (uint32_t)((t_pre + t_pay) * 1e6);
}

uint8_t sx127x_opmode(void) { return rd(REG_OPMODE); }
bool sx127x_dio0(void) { return gpio_get(C.pin_dio0); }

void sx127x_dump(void) {
    printf("[%8lu] D %-6s OpMode=%02X IrqFlags=%02X Mcfg1=%02X Mcfg2=%02X Mcfg3=%02X Frf=%02X%02X%02X PaCfg=%02X Sync=%02X DIO0=%d\n",
           LOG_MS(), TAG, rd(REG_OPMODE), rd(REG_IRQ_FLAGS), rd(REG_MODEM_CFG1), rd(REG_MODEM_CFG2), rd(REG_MODEM_CFG3),
           rd(REG_FRF_MSB), rd(REG_FRF_MID), rd(REG_FRF_LSB), rd(REG_PA_CONFIG), rd(REG_SYNC_WORD), gpio_get(C.pin_dio0));
}
