#pragma once

#include <stdint.h>

#include "esp_err.h"

/* NVS namespace dùng chung cho toàn bộ config/state của Scanner (scanner_id,
 * cờ OTA pending...) — các module khác include header này để lấy namespace
 * thay vì tự định nghĩa lại chuỗi, tránh gõ sai/lệch namespace giữa các file */
#define BMT_SCAN_NVS_NAMESPACE   "bmt_scan"

/* ============================================================================
 * BMT_SCAN_CORE — config + orchestrator
 * ----------------------------------------------------------------------------
 * Khác với thiết kế "mỗi scanner 1 project, scanner_id hardcode compile-time"
 * — ở đây scanner_id lưu NVS, đổi được runtime qua UART lệnh 'i' mà không
 * cần build lại firmware. Nhờ vậy CHỈ CẦN 1 file .bin dùng chung cho mọi
 * scanner (BMT_OTA_URL không đổi theo từng node), OTA-beacon broadcast 1
 * lần là tất cả scanner cùng cập nhật — đơn giản hơn nhiều so với việc phải
 * build + host riêng 1 file .bin cho từng scanner.
 * ============================================================================ */

/* Gọi 1 lần đầu app_main() — orchestrate toàn bộ init theo đúng thứ tự:
 * NVS scanner_id → auth (HMAC key) → bluetooth → mesh → scan (GAP) → uart */
esp_err_t bmt_scan_core_init(void);

uint8_t bmt_scan_core_scanner_id(void);

/* Đổi scanner_id lúc runtime (UART lệnh 'i') — lưu NVS, áp dụng ngay cho
 * biến in-memory, nhưng UUID mesh chỉ cập nhật sau khi reset (đã có UUID cũ
 * trong NVS mesh stack) — caller (bmt_uart.c) tự nhắc người dùng bấm 'r' */
void bmt_scan_core_set_scanner_id(uint8_t id);
