#include "bmt_ota.h"

static const char *TAG = "BMT_OTA";

static volatile bool      s_running    = false;
static EventGroupHandle_t s_wifi_evgrp = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_evgrp, BMT_OTA_WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_wifi_evgrp, BMT_OTA_WIFI_CONNECTED_BIT);
    }
}

static void ota_task(void *arg) {
    (void)arg;

    esp_log_level_set("*", ESP_LOG_NONE);
    printf("\n[OTA] ===== WiFi OTA triggered =====\n");
    printf("[OTA] URL: %s\n", BMT_OTA_URL);

    s_wifi_evgrp  = xEventGroupCreate();
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("[OTA] netif init failed: %s\n", esp_err_to_name(err));
        goto ota_fail;
    }
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    err                     = esp_wifi_init(&wcfg);
    if (err != ESP_OK) {
        printf("[OTA] wifi init failed: %s\n", esp_err_to_name(err));
        goto ota_fail;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
                                        NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
                                        NULL);

    wifi_config_t wifi_cfg = {.sta = {.threshold.authmode = WIFI_AUTH_WPA2_PSK}};
    strncpy((char *)wifi_cfg.sta.ssid, BMT_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, BMT_WIFI_PASS, sizeof(wifi_cfg.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();

    printf("[OTA] Connecting WiFi (max %ds)...\n", BMT_OTA_WIFI_TIMEOUT_MS / 1000);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_evgrp, BMT_OTA_WIFI_CONNECTED_BIT, pdFALSE,
                                           pdFALSE, pdMS_TO_TICKS(BMT_OTA_WIFI_TIMEOUT_MS));
    if (!(bits & BMT_OTA_WIFI_CONNECTED_BIT)) {
        printf("[OTA] WiFi connect timeout\n");
        goto ota_fail_wifi;
    }

    printf("[OTA] WiFi connected — downloading firmware...\n");
    esp_http_client_config_t http_cfg = {
        .url               = BMT_OTA_URL,
        .timeout_ms        = BMT_OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = {.http_config = &http_cfg};
    err                            = esp_https_ota(&ota_cfg);

    if (err == ESP_OK) {
        printf("[OTA] ===== SUCCESS — rebooting =====\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    printf("[OTA] esp_https_ota FAILED: %s\n", esp_err_to_name(err));

ota_fail_wifi:
    esp_wifi_stop();
    esp_wifi_deinit();
ota_fail:
    printf("[OTA] failed — back to BLE-only\n");
    s_running = false;
    vTaskDelete(NULL);
}

esp_err_t bmt_ota_start(void) {
    if (s_running) {
        ESP_LOGW(TAG, "OTA already running");
        return ESP_ERR_INVALID_STATE;
    }
    s_running     = true;
    BaseType_t ok = xTaskCreate(ota_task, "bmt_ota", 8192, NULL, 4, NULL);
    if (ok != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool bmt_ota_is_running(void) {
    return s_running;
}
