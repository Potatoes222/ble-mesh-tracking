#include "bmt_health.h"
#include "bmt_mesh.h"
#include "bmt_scan_core.h"
#include "bmt_types.h"

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

#define BMT_HEALTH_INITIAL_DELAY_MS  15000
#define BMT_HEALTH_PUBLISH_PERIOD_MS 30000

static const char *TAG = "BMT_HEALTH";

#if SOC_TEMP_SENSOR_SUPPORTED
static temperature_sensor_handle_t s_temp_sensor = NULL;
#endif
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_adc_cali   = NULL;
static bool                      s_adc_ok     = false;

static void temp_init(void) {
#if SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_temp_sensor) != ESP_OK) {
        ESP_LOGW(TAG, "Temp sensor install failed");
        return;
    }
    temperature_sensor_enable(s_temp_sensor);
    ESP_LOGI(TAG, "Temperature sensor initialized");
#else
    ESP_LOGI(TAG, "Temperature sensor not supported on this target");
#endif
}

static int8_t read_temp_c(void) {
#if SOC_TEMP_SENSOR_SUPPORTED
    if (!s_temp_sensor) return 0;
    float t = 0.0f;
    if (temperature_sensor_get_celsius(s_temp_sensor, &t) == ESP_OK) return (int8_t)t;
#endif
    return 0;
}

static void adc_init(void) {
    adc_oneshot_unit_init_cfg_t init = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&init, &s_adc_handle) != ESP_OK) {
        ESP_LOGW(TAG, "ADC unit init failed");
        return;
    }
    adc_oneshot_chan_cfg_t chan = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_adc_ok = (adc_cali_create_scheme_curve_fitting(&cfg, &s_adc_cali) == ESP_OK);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_adc_ok = (adc_cali_create_scheme_line_fitting(&cfg, &s_adc_cali) == ESP_OK);
#endif
    if (s_adc_ok)
        ESP_LOGI(TAG, "ADC calibrated for VDD measurement");
    else
        ESP_LOGW(TAG, "ADC calibration failed, using estimate");
}

static uint16_t read_vdd_mv(void) {
    if (!s_adc_handle || !s_adc_ok) return 3300;
    int raw = 0, mv = 0;
    if (adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw) == ESP_OK)
        adc_cali_raw_to_voltage(s_adc_cali, raw, &mv);
    return (uint16_t)mv;
}

static void health_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BMT_HEALTH_INITIAL_DELAY_MS));

    while (1) {
        if (!bmt_mesh_is_ready()) {
            vTaskDelay(pdMS_TO_TICKS(BMT_HEALTH_PUBLISH_PERIOD_MS));
            continue;
        }

        uint32_t          uptime_s = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        bmt_node_health_t h        = {
                   .scanner_id   = bmt_scan_core_scanner_id(),
                   .chip_temp_c  = read_temp_c(),
                   .vdd_mv       = read_vdd_mv(),
                   .free_heap_kb = (uint16_t)(esp_get_free_heap_size() / 1024),
                   .uptime_min   = (uint16_t)(uptime_s / 60),
        };

        esp_err_t err = bmt_mesh_publish_node_health(&h);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "[Health] temp=%dC vdd=%umV heap=%uKB up=%umin", h.chip_temp_c, h.vdd_mv,
                     h.free_heap_kb, h.uptime_min);
        } else {
            ESP_LOGW(TAG, "[Health] Publish failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(BMT_HEALTH_PUBLISH_PERIOD_MS));
    }
}

esp_err_t bmt_health_init(void) {
    temp_init();
    adc_init();

    BaseType_t ok = xTaskCreate(health_task, "bmt_health", 3072, NULL, 2, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
