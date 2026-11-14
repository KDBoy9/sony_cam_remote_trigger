#pragma once
#include <stdint.h>
#include "lora_link.h"
/* Builds the radio config from lora_config.h, brings up the radio and the link layer.
 * Returns 0 on success, or a negative error from sx127x_init. */
int lora_setup(uint8_t (*on_message)(uint8_t, const uint8_t *, uint8_t, int16_t, int8_t), void (*yield)(void), uint8_t (*ack_extra)(uint8_t, uint8_t *));
