/* ============================================================================
 * BMT (BLE Mesh Tracking) — RELAY firmware  [v2.0-modular]
 * ----------------------------------------------------------------------------
 * v2.0-modular — tách thành module riêng:
 *   bmt_types.h    — struct/opcode dùng chung
 *   bmt_config.h   — định danh Relay (UUID/relay ID), WiFi, OTA URL
 *   bmt_mesh.c/h   — provisioning, models (health + vendor), callbacks, publish
 *   bmt_ota.c/h    — WiFi OTA task, NVS pending flag, báo cáo kết quả
 *   bmt_uart.c/h   — UART command task + monitor task định kỳ
 *
 * Role  : Passive forwarder (mở rộng vùng phủ sóng BLE Mesh)
 * Board : ESP32 DevKitC WROOM-32
 * IDF   : v6.0
 * ============================================================================ */

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_bt_main.h"

#include "bmt_config.h"
#include "bmt_mesh.h"
#include "bmt_ota.h"
#include "bmt_uart.h"

static const char *TAG = "BMT_MAIN";

/* ============================================================================
 * BLUETOOTH INIT — platform-level, không thuộc module cụ thể nào
 * ============================================================================ */
static esp_err_t bluetooth_init(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { ESP_LOGE(TAG, "BT controller init failed"); return ret; }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) { ESP_LOGE(TAG, "BT controller enable failed"); return ret; }

    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&cfg);
    if (ret) { ESP_LOGE(TAG, "Bluedroid init failed"); return ret; }

    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(TAG, "Bluedroid enable failed"); return ret; }

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== BMT Relay v2.0-modular Starting ===");
    ESP_LOGI(TAG, "Relay ID: 0x%02X", BMT_RELAY_UUID[15]);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "BT init failed: %s", esp_err_to_name(err)); return; }

    err = bmt_mesh_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Mesh init failed: %s", esp_err_to_name(err)); return; }

    bmt_uart_init();
    bmt_ota_start_pending_report_task();
    bmt_ota_start_auto_check();

    ESP_LOGI(TAG, "=== BMT Relay READY (r=reset, 1=status) ===");
    printf("Relay node Ready\n");
}
