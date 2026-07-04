#pragma once

#include <stdbool.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "bmt_scan_core.h"

#define BMT_OTA_WIFI_CONNECTED_BIT BIT0
#define BMT_OTA_HTTP_TIMEOUT_MS    120000
#define BMT_OTA_WIFI_TIMEOUT_MS    30000

esp_err_t bmt_ota_start(void);
bool      bmt_ota_is_running(void);
