#include "bmt_scan_core.h"

#include "bmt_health.h"
#include "bmt_mesh.h"
#include "bmt_scan.h"
#include "bmt_tag_table.h"
#include "bmt_uart.h"

static const char *TAG = "BMT_CORE";

static bmt_scan_core_cfg_t s_cfg;

uint8_t bmt_scan_core_scanner_id(void) {
    return s_cfg.scanner_id;
}
const char *bmt_scan_core_wifi_ssid(void) {
    return s_cfg.wifi_ssid;
}
const char *bmt_scan_core_wifi_pass(void) {
    return s_cfg.wifi_pass;
}
const char *bmt_scan_core_ota_url(void) {
    return s_cfg.ota_url;
}

esp_err_t bmt_scan_core_init(const bmt_scan_core_cfg_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    ESP_LOGI(TAG, "Scanner ID 0x%02X, OTA URL %s", s_cfg.scanner_id, s_cfg.ota_url);

    bmt_tag_table_reset();
    ESP_ERROR_CHECK(bmt_mesh_init());
    ESP_ERROR_CHECK(bmt_scan_start());
    ESP_ERROR_CHECK(bmt_health_init());
    ESP_ERROR_CHECK(bmt_uart_init());
    return ESP_OK;
}
