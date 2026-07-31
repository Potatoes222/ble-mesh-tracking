#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#pragma pack(1)
typedef struct
{
	uint8_t uuid[16];    /* 16B: system UUID (AB000000-...) */
	uint16_t major;      /*  2B: PERSON=0x0001, ASSET=0x0002 */
	uint16_t minor;      /*  2B: tag ID within the system */
	int8_t tx_power;     /*  1B: measured power at 1 m (calibrated) */
	uint8_t sequence;    /*  1B: 0-255, wraps -> Scanner computes loss rate */
	uint16_t mac16;      /*  2B: truncated HMAC-SHA256 (replaces CRC16) */
} bmt_tag_adv_payload_t; /* = 24 bytes */
#pragma pack()

/* Initialise Bluetooth, register the GAP callback, create the 500 ms
 * sequence timer, and start advertising. MUST be called after
 * bmt_auth_init(). */
esp_err_t bmt_beacon_start(void);

/* Getters used by UART status. */
uint8_t bmt_beacon_sequence(void);
uint16_t bmt_beacon_last_mac16(void);
bool bmt_beacon_is_active(void);
