#include "bmt_config.h"
#include "bmt_mesh.h"
#include "bmt_uart.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "BMT_RELAY";

static void nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== BMT Relay Node Starting ===");
    ESP_LOGI(TAG, "Relay ID: 0x%02X", BMT_RELAY_ID);

    nvs_init();
    ESP_ERROR_CHECK(bmt_mesh_init());
    bmt_mesh_start_monitor();
    ESP_ERROR_CHECK(bmt_uart_init());

    ESP_LOGI(TAG, "=== BMT Relay READY (r=reset, 1=status) ===");
}
