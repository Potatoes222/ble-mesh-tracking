#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Tao timer sequence 500ms, bat dau advertise.
 * PHAI goi SAU khi bt_enable() da thanh cong va bmt_auth_init() da chay. */
int bmt_beacon_start(void);

/* Getter (debug/status) */
uint8_t bmt_beacon_sequence(void);
uint16_t bmt_beacon_last_mac16(void);
bool bmt_beacon_is_active(void);
