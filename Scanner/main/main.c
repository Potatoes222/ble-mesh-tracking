/* ============================================================================
 * BMT (BLE Mesh Tracking) — SCAN NODE firmware  [v3.0-modular]
 * ----------------------------------------------------------------------------
 * v3.0-modular — tách toàn bộ logic thành module riêng (theo cùng ranh giới
 * bmt_scan_core mà bạn cộng tác đã dùng), main.c chỉ còn init NVS flash rồi
 * giao cho bmt_scan_core_init() điều phối:
 *
 *   bmt_types.h      — struct/opcode dùng chung
 *   bmt_auth.c/h     — HMAC (thay CRC16 của bản gốc) cho Tag ADV + OTA-beacon
 *   bmt_tag_table.c/h— Kalman filter, distance, CRUD bảng tag, anti-replay
 *   bmt_mesh.c/h     — provisioning, models, callbacks, publish
 *   bmt_ota.c/h      — WiFi OTA task, NVS pending flag, báo cáo kết quả
 *   bmt_scan.c/h     — radio GAP time-division, detect OTA-beacon, parse ADV
 *   bmt_uart.c/h     — UART command task
 *   bmt_scan_core.c/h— scanner_id (NVS runtime) + orchestrator init
 *
 * Thiết kế vẫn giữ: scanner_id runtime qua NVS/UART (KHÔNG hardcode compile-
 * time như hướng "1 project riêng mỗi scanner") → chỉ cần 1 file .bin dùng
 * chung cho mọi scanner.
 * ============================================================================ */

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bmt_scan_core.h"

static const char *TAG = "BMT_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== BMT Scan Node v3.0-modular Ready ===");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(bmt_scan_core_init());
}
