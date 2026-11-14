#pragma once
/* Radio + pin configuration shared by BOTH boards (identical wiring on the Pico W and the Pico 2).
 *
 * India, 433 MHz: G.S.R. 347(E) (9 May 2022) exempts 433.05-434.79 MHz at <= 10 mW e.r.p. with a
 * duty cycle of <= 10 %. Keep the whole 125 kHz channel inside that band and keep the power low.
 * (10 mW e.r.p. is about 12 dBm EIRP; 7 dBm into a ~2 dBi antenna is comfortably inside.) */
#define LORA_FREQ_HZ      433500000UL   /* channel centre */
#define LORA_BW_IDX       7             /* 7 = 125 kHz (see sx127x.h) */
#define LORA_SF           7             /* 7..12. Higher = more range, much longer airtime */
#define LORA_CR           5             /* coding rate 4/5 */
#define LORA_PREAMBLE     8
#define LORA_SYNC_WORD    0x12          /* private network (0x34 is LoRaWAN public) */
#define LORA_TX_DBM       7             /* conducted power at the module's antenna pin, 2..17 */
#define LORA_NET_ID       0x42          /* frames from other networks are ignored */

/* Duty-cycle limiter: refuse to transmit past this share of airtime over a rolling hour. */
#define LORA_DUTY_PERCENT 10

/* Wiring: SPI0 + two GPIOs. Same numbers on both Picos. */
#define LORA_SPI          spi0
#define LORA_PIN_MISO     16   /* Pico pin 21 */
#define LORA_PIN_CS       17   /* Pico pin 22 */
#define LORA_PIN_SCK      18   /* Pico pin 24 */
#define LORA_PIN_MOSI     19   /* Pico pin 25 */
#define LORA_PIN_RST      20   /* Pico pin 26 */
#define LORA_PIN_DIO0     21   /* Pico pin 27 */
