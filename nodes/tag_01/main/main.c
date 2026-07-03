#include "bmt_beacon.h"
#include "bmt_config.h"
#include "bmt_uart.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "BMT_TAG";

static void nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== BLE Tag Starting ===");
    ESP_LOGI(TAG, "Major=0x%04X (%s)  Minor=0x%04X", BMT_TAG_MAJOR,
             BMT_TAG_MAJOR == 0x0001 ? "PERSON" : "ASSET", BMT_TAG_MINOR);

    nvs_init();
    ESP_ERROR_CHECK(bmt_beacon_init());
    ESP_ERROR_CHECK(bmt_uart_init());

    ESP_LOGI(TAG, "=== Tag READY (1=status) ===");
}
