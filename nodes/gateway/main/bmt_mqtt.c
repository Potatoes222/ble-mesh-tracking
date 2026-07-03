#include "bmt_mqtt.h"
#include "bmt_config.h"
#include "bmt_thingsboard.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "BMT_MQTT";

static esp_mqtt_client_handle_t s_client    = NULL;
static bool                     s_connected = false;

static QueueHandle_t s_queue     = NULL;
static uint32_t      s_enqueued  = 0;
static uint32_t      s_dropped   = 0;
static uint32_t      s_published = 0;

extern const uint8_t bmt_ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t bmt_ca_pem_end[] asm("_binary_ca_pem_end");

static void event_handler(void *args, esp_event_base_t base, int32_t id, void *data) {
    (void)args;
    (void)base;
    (void)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected to ThingsBoard");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected");
        break;
    default:
        break;
    }
}

void bmt_mqtt_init(void) {
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri                  = BMT_TB_HOST,
        .broker.verification.certificate     = (const char *)bmt_ca_pem_start,
        .broker.verification.certificate_len = (size_t)(bmt_ca_pem_end - bmt_ca_pem_start),
        .broker.verification.common_name     = BMT_TB_CN,
        .credentials.username                = BMT_TB_GATEWAY_TOKEN,
        .network.timeout_ms                  = BMT_MQTT_PUBLISH_TIMEOUT_MS,
        .network.reconnect_timeout_ms        = 5000,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "init failed");
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTTS -> %s (verify CN=%s)", BMT_TB_HOST, BMT_TB_CN);
}

bool bmt_mqtt_is_connected(void) {
    return s_connected;
}

int bmt_mqtt_publish(const char *topic, const char *json, int qos) {
    if (!s_connected || !s_client) return -1;
    int msg_id = esp_mqtt_client_publish(s_client, topic, json, 0, qos, 0);
    if (msg_id >= 0) s_published++;
    return msg_id;
}

esp_err_t bmt_mqtt_queue_create(void) {
    s_queue = xQueueCreate(BMT_MQTT_QUEUE_SIZE, sizeof(bmt_tag_report_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Queue created (size=%d)", BMT_MQTT_QUEUE_SIZE);
    return ESP_OK;
}

bool bmt_mqtt_enqueue_tag_report(const bmt_tag_report_t *r) {
    if (!s_queue) return false;
    if (xQueueSend(s_queue, r, 0) == pdTRUE) {
        s_enqueued++;
        return true;
    }
    s_dropped++;
    ESP_LOGW(TAG, "Queue FULL — dropped (total: %" PRIu32 ")", s_dropped);
    return false;
}

static void worker_task(void *arg) {
    (void)arg;
    bmt_tag_report_t report;
    ESP_LOGI(TAG, "Worker task started");
    while (1) {
        if (xQueueReceive(s_queue, &report, portMAX_DELAY) == pdTRUE)
            bmt_tb_pub_tag_report(&report);
    }
}

void bmt_mqtt_start_worker(void) {
    xTaskCreate(worker_task, "bmt_mqtt_wkr", 4096, NULL, 5, NULL);
}

void bmt_mqtt_print_stats(void) {
    UBaseType_t in_queue = s_queue ? uxQueueMessagesWaiting(s_queue) : 0;
    printf("\n========== MQTT QUEUE STATS ==========\n");
    printf("Connected       : %s\n", s_connected ? "YES" : "NO");
    printf("Queue size      : %u / %d\n", (unsigned)in_queue, BMT_MQTT_QUEUE_SIZE);
    printf("Total enqueued  : %" PRIu32 "\n", s_enqueued);
    printf("Total published : %" PRIu32 "\n", s_published);
    printf("Total dropped   : %" PRIu32 " (queue full)\n", s_dropped);
    if (s_enqueued > 0) {
        float drop_rate = (float)s_dropped * 100.0f / s_enqueued;
        printf("Drop rate       : %.2f%%\n", drop_rate);
    }
    printf("======================================\n");
}
