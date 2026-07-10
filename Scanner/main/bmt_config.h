#pragma once

/* ============================================================================
 * BMT_CONFIG — USER CONFIG cho Scanner (dùng chung cho mọi scanner vật lý,
 * vì scanner_id là runtime qua NVS/UART — xem bmt_scan_core.h)
 * ----------------------------------------------------------------------------
 * [SECURITY — quyết định có chủ đích] HTTP thuần, không TLS — server OTA chỉ
 * chạy trong LAN nội bộ, chấp nhận rủi ro MITM từ kẻ tấn công ĐÃ ở trong LAN
 * (chi tiết xem Gateway/main/main.c). Không gắn crt_bundle_attach vì không
 * có tác dụng gì với http://, tránh gây hiểu nhầm là đang dùng TLS.
 * ============================================================================ */
#define BMT_WIFI_SSID           "YOUR_WIFI_SSID"
#define BMT_WIFI_PASS           "YOUR_WIFI_PASSWORD"
#define BMT_OTA_SCANNER_URL     "http://192.168.2.23:8080/Scanner.bin"
#define BMT_OTA_WIFI_TIMEOUT_MS 30000
