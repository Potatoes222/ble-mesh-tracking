#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "soc/soc_caps.h"

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_mesh.h"
#include "bmt_scan_core.h"
#include "bmt_types.h"

#define BMT_HEALTH_INITIAL_DELAY_MS  15000
#define BMT_HEALTH_PUBLISH_PERIOD_MS 30000

esp_err_t bmt_health_init(void);
