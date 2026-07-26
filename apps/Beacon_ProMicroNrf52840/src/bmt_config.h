#pragma once

#include <stdint.h>

/* System UUID — GIONG NHAU tren tat ca tag (ESP32 + nRF52840 + iPhone)
 * Scanner check 4 byte dau (AB 00 00 00) de nhan ra "tag cua he thong" */
static const uint8_t BMT_SYSTEM_UUID[16] = {
    0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
#define BMT_TAG_MINOR 0x0001   /* tag ID — doi cho tung thiet bi */
#define BMT_TAG_TX_POWER (-53) /* calibrate: do RSSI tai 1m roi dien */
/* (Da bo BMT_TAG_MAJOR PERSON/ASSET — payload gio dung 2B do cho % pin) */

/* [POWER] ADV interval random trong range nay de tranh collision nhieu tag.
 * Tang tu 450-550ms len ~900-1100ms de GIAM TAI pin: radio phat thua hon 1
 * nua -> giam dong trung binh + giam so lan sut ap qua pin/diode. Doi lai
 * Scanner cap nhat vi tri thua hon (~1s/lan) — chap nhan duoc voi tag deo.
 * (Muon dinh vi nhanh hon thi ha xuong, nhung ton pin hon.) */
#define BMT_ADV_INTERVAL_MIN_MS 900
#define BMT_ADV_INTERVAL_MAX_MS 1100

/* [TODO power-opt] Cong suat phat radio thuc te (khac voi BMT_TAG_TX_POWER
 * o tren — do la gia tri "khai bao" trong payload de Scanner tinh khoang
 * cach). Ben Zephyr/nRF52 chinh qua Kconfig (CONFIG_BT_CTLR_TX_PWR_*) chu
 * khong goi ham runtime nhu ESP-IDF — se lam o buoc toi uu nang luong. */
