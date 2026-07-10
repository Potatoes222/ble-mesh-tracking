/* ============================================================================
 * BMT (BLE Mesh Tracking) — GATEWAY firmware  [v5.0-modular-nimble]
 * ----------------------------------------------------------------------------
 * v5.0-modular-nimble — tách thành module riêng + chuyển BLE host stack từ
 * Bluedroid sang NimBLE (xem sdkconfig: CONFIG_BT_NIMBLE_ENABLED=y):
 *   bmt_types.h        — struct/opcode dùng chung với Scanner/Relay
 *   bmt_config.h       — WiFi, ThingsBoard, OTA URL (đổi theo triển khai)
 *   bmt_mac_cache.c/h  — cache tạm UUID→MAC lúc quét chưa provision
 *   bmt_node_table.c/h — bảng node đã provision, NVS save/load
 *   bmt_scan_list.c/h  — chế độ scan/provision thủ công (UART s/p/a/m)
 *   bmt_zone.c/h       — tag tracking + đánh giá zone (hysteresis)
 *   bmt_mesh.c/h       — provisioning, models, callbacks, NetKey/AppKey random
 *   bmt_ota.c/h        — WiFi OTA (gateway/relay/scanner), beacon NimBLE+HMAC
 *   bmt_wifi.c/h       — WiFi STA
 *   bmt_mqtt.c/h       — MQTT client + RPC routing + hàng đợi tag report
 *   bmt_thingsboard.c/h— publish telemetry ThingsBoard
 *   bmt_watchdog.c/h   — tự reset toàn mesh nếu không có dữ liệu (rút relay...)
 *   bmt_uart.c/h       — UART command task
 *
 * [⚠️ NimBLE — CẦN KIỂM CHỨNG TRÊN PHẦN CỨNG THẬT] esp_ble_mesh_* API không
 * đổi giữa 2 host stack (component "example_init" đã có sẵn bluetooth_init()
 * cho cả Bluedroid/NimBLE, chọn theo sdkconfig). Riêng bmt_ota.c: cơ chế OTA-
 * beacon (raw BLE ADV broadcast để tất cả Scanner OTA đồng thời) được viết lại
 * bằng NimBLE host API (ble_gap_adv_set_data/ble_gap_adv_start) — CHƯA test
 * thật, có khả năng xung đột với advertising nội bộ của mesh stack. Nếu vậy,
 * bmt_ota.c tự động fallback sang mesh unicast từng scanner (chậm hơn nhưng
 * vẫn đảm bảo cập nhật được — xem bmt_ota_trigger_all_scanners()).
 * ============================================================================ */

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
#include "bmt_ota.h"
#include "bmt_thingsboard.h"
#include "bmt_uart.h"
#include "bmt_watchdog.h"
#include "bmt_wifi.h"
#include "bmt_zone.h"

static const char *TAG = "BMT_GW";

#define BMT_WDT_TIMEOUT_S  60

static void wdt_feed_task(void *arg)
{
    (void)arg; esp_task_wdt_add(NULL);
    while (1) { esp_task_wdt_reset(); vTaskDelay(pdMS_TO_TICKS(10000)); }
}

void app_main(void)
{
    esp_err_t err;
    ESP_LOGI(TAG, "=== BMT Gateway v5.0-modular-nimble Starting ===");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* [v6.8] Nhanh nay XOA SACH toan bo NVS (node table + mesh key) — truoc
         * day chay im lang nen khong the biet vi sao cu boot len la bang trong
         * du da save. Bao that to de lan sau nhin log la thay ngay. */
        ESP_LOGE(TAG, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        ESP_LOGE(TAG, "!!! NVS init: %s — PHAI XOA TOAN BO NVS", esp_err_to_name(err));
        ESP_LOGE(TAG, "!!! => MAT node table + NetKey/AppKey (se sinh key moi)");
        ESP_LOGE(TAG, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        ESP_ERROR_CHECK(nvs_flash_erase()); err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    bmt_node_table_load();

    /* [v4.9-security] Sinh NetKey/AppKey random nếu NVS mesh chưa có key nào.
     * PHẢI gọi trước bmt_mesh_init() vì hàm đó dùng key này để add vào mesh stack. */
    bmt_mesh_generate_keys_if_needed();

    /* [SECURITY] Import key HMAC cho OTA-beacon — gọi sớm vì OTA có thể được
     * trigger bất kỳ lúc nào qua UART ('u') hoặc RPC từ ThingsBoard */
    bmt_ota_beacon_key_init();
    bmt_ota_start_key_rotation();

    bmt_mqtt_start_worker();

    bmt_wifi_init();
    bmt_mqtt_init();
    vTaskDelay(pdMS_TO_TICKS(2000));

    err = bluetooth_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "BT init failed"); return; }

    err = bmt_mesh_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Mesh init failed"); return; }

    printf("\n================ BMT GATEWAY v5.0-modular-nimble ================\n");
    bmt_mesh_print_keys();
    printf("TB     : %s\n", BMT_TB_HOST);
    printf("Device : %s\n", BMT_DEV_NAME_GATEWAY);
    printf("\nZONE MAPPING:\n");
    printf("  scanner 0x01 -> %s\n", bmt_zone_name(0x01));
    printf("  scanner 0x02 -> %s\n", bmt_zone_name(0x02));
    printf("  scanner 0x03 -> %s\n", bmt_zone_name(0x03));
    printf("\nOTA CONFIG:\n");
    printf("  Scanner : %s\n", BMT_OTA_SCANNER_URL);
    printf("  Relay   : %s\n", BMT_OTA_RELAY_URL);
    printf("  Gateway : %s\n", BMT_OTA_GATEWAY_URL);
    printf("===================================================\n");

    bmt_node_table_print();
    bmt_tb_pub_gateway_online();

    bmt_uart_start();

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = BMT_WDT_TIMEOUT_S * 1000, .idle_core_mask = 0, .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_cfg);

    bmt_mesh_start_node_ping();
    bmt_tb_start_zone_timeout_task();
    bmt_watchdog_start();
    bmt_ota_start_auto_check();
    xTaskCreate(wdt_feed_task, "bmt_wdt_feed", 2048, NULL, 2, NULL);

    ESP_LOGI(TAG, "=== BMT Gateway v5.0-modular-nimble READY ===");
    printf("Gateway Ready\n");
}
