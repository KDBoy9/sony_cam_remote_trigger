#include "lora_setup.h"
#include "lora_config.h"
#include "sx127x.h"
#include "log.h"

int lora_setup(uint8_t (*on_message)(uint8_t, const uint8_t *, uint8_t, int16_t, int8_t), void (*yield)(void), uint8_t (*ack_extra)(uint8_t, uint8_t *)) {
    sx127x_cfg_t cfg = {
        .spi = LORA_SPI, .pin_sck = LORA_PIN_SCK, .pin_mosi = LORA_PIN_MOSI, .pin_miso = LORA_PIN_MISO,
        .pin_cs = LORA_PIN_CS, .pin_rst = LORA_PIN_RST, .pin_dio0 = LORA_PIN_DIO0,
        .freq_hz = LORA_FREQ_HZ, .bw_idx = LORA_BW_IDX, .sf = LORA_SF, .cr = LORA_CR,
        .preamble = LORA_PREAMBLE, .sync_word = LORA_SYNC_WORD, .tx_dbm = LORA_TX_DBM,
    };
    int err = sx127x_init(&cfg);
    if (err) return err;
    link_cfg_t lc = {.net_id = LORA_NET_ID, .retries = 3, .ack_margin_ms = 150, .yield = yield, .on_message = on_message, .ack_extra = ack_extra};
    return link_init(&lc);
}
