#pragma once

#include <stdint.h>

/* ============================================================================
 * BMT_CONFIG — USER CONFIG cho Relay, ĐỔI byte cuối UUID CHO TỪNG RELAY
 * ============================================================================ */

/* UUID prefix "RELAY" (0x52,0x45,0x4C,0x41,0x59) + byte 15 = relay ID (0x01, 0x02...) */
static const uint8_t BMT_RELAY_UUID[16] = {
    0x52, 0x45, 0x4C, 0x41, 0x59, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};

#define BMT_WIFI_SSID            "YOUR_WIFI_SSID"
#define BMT_WIFI_PASS            "YOUR_WIFI_PASSWORD"

/* [SECURITY — quyết định có chủ đích] HTTP thuần, không TLS — server OTA chỉ
 * chạy trong LAN nội bộ, chấp nhận rủi ro MITM từ kẻ tấn công ĐÃ ở trong LAN
 * (chi tiết xem Gateway/main/main.c). */
#define BMT_OTA_RELAY_URL        "http://192.168.2.23:8080/Relay.bin"
#define BMT_OTA_WIFI_TIMEOUT_MS  30000

#define BMT_RELAY_NVS_NAMESPACE  "bmt_relay"
