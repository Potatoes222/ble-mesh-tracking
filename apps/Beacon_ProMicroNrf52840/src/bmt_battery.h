#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Khoi tao mach do pin. Ban ProMicro/nice!nano v2: pin noi THANG vao chan
 * VDDH, doc qua dau vao NOI BO VDDHDIV5 cua SAADC — khong co chia ap ngoai,
 * khong can chan GPIO enable nao (khac han ban XIAO). Xem bmt_battery.c. */
int bmt_battery_init(void);

/* Doc dien ap pin that tai VDDH (da nhan lai he so 5 cua VDDHDIV5), mV.
 * Tra ve <0 neu loi. */
int bmt_battery_read_mv(void);

/* Uoc luong % pin con lai tu dien ap pin, tra bang duong cong Li-ion
 * (noi suy, xem BATT_CURVE trong bmt_battery.c). 0-100. */
uint8_t bmt_battery_percent(int mv);

/* Board nay KHONG dua chan bao trang thai sac ra GPIO nao doc duoc ->
 * luon tra false. Giu ham de API giong ban XIAO (bmt_beacon dung chung). */
bool bmt_battery_is_charging(void);

/* % pin cache tu lan doc dinh ky gan nhat — dung cho beacon nhet vao payload
 * moi goi ADV (khong doc ADC moi lan phat -> nhe hon). Duoc cap nhat boi timer
 * 30s trong bmt_battery.c va 1 lan dong bo luc init. */
uint8_t bmt_battery_last_percent(void);
