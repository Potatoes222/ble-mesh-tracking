#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_config.h"
#include "bmt_mesh.h"
#include "bmt_node_table.h"
#include "bmt_types.h"

#define BMT_OTA_HTTP_TIMEOUT_MS 120000

esp_err_t bmt_ota_gateway_self_update(void);
esp_err_t bmt_ota_trigger_all_scanners(void);
esp_err_t bmt_ota_trigger_all_relays(void);
bool      bmt_ota_is_running(void);
