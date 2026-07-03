#include "bmt_scan_core.h"
#include "bmt_health.h"
#include "bmt_mesh.h"
#include "bmt_scan.h"
#include "bmt_tag_table.h"
#include "bmt_uart.h"

#include "esp_log.h"

static const char *TAG = "BMT_CORE";

static uint8_t s_scanner_id;

uint8_t bmt_scan_core_scanner_id(void) {
    return s_scanner_id;
}

esp_err_t bmt_scan_core_init(uint8_t scanner_id) {
    s_scanner_id = scanner_id;
    ESP_LOGI(TAG, "Scanner ID 0x%02X", s_scanner_id);

    bmt_tag_table_reset();
    ESP_ERROR_CHECK(bmt_mesh_init());
    ESP_ERROR_CHECK(bmt_scan_start());
    ESP_ERROR_CHECK(bmt_health_init());
    ESP_ERROR_CHECK(bmt_uart_init());
    return ESP_OK;
}
