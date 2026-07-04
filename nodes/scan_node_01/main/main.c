#include "bmt_config.h"
#include "bmt_scan_core.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "BMT_SCAN";

static void nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== BMT Scan Node Starting ===");
    nvs_init();

    const bmt_scan_core_cfg_t cfg = {
        .scanner_id = BMT_SCANNER_ID,
        .wifi_ssid  = BMT_WIFI_SSID,
        .wifi_pass  = BMT_WIFI_PASS,
        .ota_url    = BMT_OTA_URL,
    };
    ESP_ERROR_CHECK(bmt_scan_core_init(&cfg));

    ESP_LOGI(TAG, "=== BMT Scan Node READY (r=reset, 1=status) ===");
}
