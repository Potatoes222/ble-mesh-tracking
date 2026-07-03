#include <stdio.h>

#include "esp_bt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_mesh_example_init.h"

#include "bmt_config.h"
#include "bmt_mesh.h"
#include "bmt_mqtt.h"
#include "bmt_node_table.h"
#include "bmt_thingsboard.h"
#include "bmt_uart.h"
#include "bmt_wifi.h"
#include "bmt_zone.h"

static const char *TAG = "BMT_GW";

static void nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void set_tx_power_max(void) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P20);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P20);
    ESP_LOGI(TAG, "BLE TX power: +20 dBm (ESP32-S3)");
#else
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    ESP_LOGI(TAG, "BLE TX power: +9 dBm (ESP32 classic)");
#endif
}

static void wdt_setup(void) {
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = BMT_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_reconfigure(&cfg);
}

static void wdt_feed_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);
    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void print_banner(void) {
    printf("\n================ BMT GATEWAY ================\n");
    bmt_print_hex_key("NetKey: ", g_bmt_net_key, 16);
    bmt_print_hex_key("AppKey: ", g_bmt_app_key, 16);
    printf("TB     : %s\n", BMT_TB_HOST);
    printf("Device : %s\n", BMT_DEV_NAME_GATEWAY);
    printf("\nZONE MAPPING:\n");
    printf("  scanner 0x01 -> %s\n", bmt_zone_name(0x01));
    printf("  scanner 0x02 -> %s\n", bmt_zone_name(0x02));
    printf("  scanner 0x03 -> %s\n", bmt_zone_name(0x03));
    printf("\nZONE PARAMS:\n");
    printf("  Hysteresis     : %d dBm\n", BMT_ZONE_HYSTERESIS_DBM);
    printf("  Scanner valid  : %d ms\n", BMT_SCANNER_VALID_MS);
    printf("  Out-of-range   : %d ms\n", BMT_TAG_OUT_OF_RANGE_MS);
    printf("\nMQTT QUEUE:\n");
    printf("  Size           : %d slots\n", BMT_MQTT_QUEUE_SIZE);
    printf("=============================================\n");
}

void app_main(void) {
    ESP_LOGI(TAG, "=== BMT Gateway Starting ===");

    nvs_init();
    bmt_node_table_load();
    ESP_ERROR_CHECK(bmt_mqtt_queue_create());

    bmt_wifi_init();
    bmt_mqtt_init();
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_ERROR_CHECK(bluetooth_init());
    set_tx_power_max();

    ESP_ERROR_CHECK(bmt_mesh_init());
    ESP_ERROR_CHECK(bmt_uart_init());

    print_banner();
    bmt_uart_print_status();
    bmt_node_table_print();
    bmt_tb_pub_gateway_online();

    wdt_setup();

    bmt_mqtt_start_worker();
    bmt_mesh_start_relay_ping();
    bmt_zone_start_timeout_task();
    xTaskCreate(wdt_feed_task, "bmt_wdt_feed", 2048, NULL, 2, NULL);

    ESP_LOGI(TAG, "=== BMT Gateway READY ===");
}
