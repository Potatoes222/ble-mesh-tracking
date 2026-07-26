#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Khoi tao mach do pin on-board (P0.14 enable + P0.31 ADC qua bo chia ap)
 * cho pin cam vao BAT+/BAT- (LIR2032/LiPo). Xem ghi chu bmt_battery.c. */
int bmt_battery_init(void);

/* Doc dien ap pin that tai BAT+ (da nhan lai ti so chia ap), don vi mV.
 * Tra ve <0 neu loi. */
int bmt_battery_read_mv(void);

/* Uoc luong % pin con lai tu dien ap pin, tra bang duong cong Li-ion
 * (noi suy, xem BATT_CURVE trong bmt_battery.c). 0-100. */
uint8_t bmt_battery_percent(int mv);

/* true neu chip sac dang sac (P0.17 LOW). */
bool bmt_battery_is_charging(void);

/* % pin cache tu lan doc dinh ky gan nhat — dung cho beacon nhet vao payload
 * moi goi ADV (khong doc ADC moi lan phat -> nhe hon). Duoc cap nhat boi timer
 * 30s trong bmt_battery.c va 1 lan dong bo luc init. */
uint8_t bmt_battery_last_percent(void);
