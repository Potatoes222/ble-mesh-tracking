#include "bmt_thingsboard.h"
#include "bmt_config.h"
#include "bmt_mqtt.h"
#include "bmt_node_table.h"
#include "bmt_zone.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BMT_TB";

static void set_role_with_retry(const char *device_name, const char *role) {
    for (int i = 0; i < 3; i++) {
        vTaskDelay(pdMS_TO_TICKS(200));
        bmt_tb_set_role(device_name, role);
    }
}

static void device_name_for(uint16_t addr, const char *role, char *dev, size_t sz) {
    if (role && strcmp(role, BMT_ROLE_SCAN) == 0)
        snprintf(dev, sz, BMT_DEV_NAME_SCAN_FMT, addr);
    else if (role && strcmp(role, BMT_ROLE_RELAY) == 0)
        snprintf(dev, sz, BMT_DEV_NAME_RELAY_FMT, addr);
    else
        snprintf(dev, sz, BMT_DEV_NAME_NODE_FMT, addr);
}

void bmt_tb_connect_device(const char *device_name, const char *device_type) {
    if (!bmt_mqtt_is_connected()) return;
    char json[128];
    if (device_type && device_type[0])
        snprintf(json, sizeof(json), "{\"device\":\"%s\",\"type\":\"%s\"}", device_name,
                 device_type);
    else
        snprintf(json, sizeof(json), "{\"device\":\"%s\"}", device_name);
    bmt_mqtt_publish("v1/gateway/connect", json, 1);
    ESP_LOGI(TAG, "CONNECT: %s (type=%s)", device_name, device_type ? device_type : "default");
}

void bmt_tb_disconnect_device(const char *device_name) {
    if (!bmt_mqtt_is_connected()) return;
    char json[64];
    snprintf(json, sizeof(json), "{\"device\":\"%s\"}", device_name);
    bmt_mqtt_publish("v1/gateway/disconnect", json, 1);
    ESP_LOGI(TAG, "DISCONNECT: %s", device_name);
}

void bmt_tb_set_role(const char *device_name, const char *role) {
    if (!bmt_mqtt_is_connected()) return;
    char json[128];
    snprintf(json, sizeof(json), "{\"%s\":{\"role\":\"%s\"}}", device_name, role);
    bmt_mqtt_publish("v1/gateway/attributes", json, 1);
    ESP_LOGI(TAG, "ATTR [%s]: role=%s", device_name, role);
}

void bmt_tb_pub_gateway_online(void) {
    if (!bmt_mqtt_is_connected()) return;
    bmt_mqtt_publish("v1/devices/me/telemetry", "{\"status\":\"ONLINE\"}", 1);
    bmt_mqtt_publish("v1/devices/me/attributes", "{\"role\":\"" BMT_ROLE_GATEWAY "\"}", 1);
    ESP_LOGI(TAG, "Gateway ONLINE (role=" BMT_ROLE_GATEWAY ")");
}

void bmt_tb_pub_node_status(uint16_t addr, const char *role, bool online) {
    if (!bmt_mqtt_is_connected()) return;
    char dev[32], json[192];
    device_name_for(addr, role, dev, sizeof(dev));

    if (online) {
        bmt_tb_connect_device(dev, BMT_PROFILE_NODE);
        set_role_with_retry(dev, role);
    }
    snprintf(json, sizeof(json), "{\"%s\":[{\"status\":\"%s\",\"addr\":\"0x%04x\"}]}", dev,
             online ? "ONLINE" : "OFFLINE", addr);
    bmt_mqtt_publish("v1/gateway/telemetry", json, 1);
    ESP_LOGI(TAG, "TELEMETRY [%s]: %s", dev, json);

    if (!online) bmt_tb_disconnect_device(dev);
}

void bmt_tb_pub_tag_report(const bmt_tag_report_t *r) {
    if (!r) return;

    bmt_zone_ingest_report(r->scanner_id, r->tag_id, r->tag_type, r->rssi);

    if (!bmt_mqtt_is_connected()) return;

    char dev[32], json[192], scanner_str[16];
    snprintf(dev, sizeof(dev), BMT_DEV_NAME_TAG_FMT, r->tag_id);
    snprintf(scanner_str, sizeof(scanner_str), "scan_%02u", (unsigned)r->scanner_id);

    static uint16_t s_seen[BMT_MAX_SEEN_TAGS] = {0};
    static int      s_seen_count              = 0;
    bool            first_seen                = true;
    for (int i = 0; i < s_seen_count; i++)
        if (s_seen[i] == r->tag_id) {
            first_seen = false;
            break;
        }
    if (first_seen && s_seen_count < BMT_MAX_SEEN_TAGS) {
        s_seen[s_seen_count++] = r->tag_id;
        bmt_tb_connect_device(dev, BMT_PROFILE_TAG);
        set_role_with_retry(dev, BMT_ROLE_TAG);
    }

    snprintf(json, sizeof(json), "{\"%s\":[{\"scanner_id\":\"%s\",\"rssi\":%d}]}", dev, scanner_str,
             r->rssi);

    int msg_id = bmt_mqtt_publish("v1/gateway/telemetry", json, 0);
    if (msg_id < 0)
        ESP_LOGW(TAG, "publish failed for %s", dev);
    else
        ESP_LOGI(TAG, "[%s] scanner=%s rssi=%d dBm", dev, scanner_str, r->rssi);
}

void bmt_tb_pub_node_health(uint16_t src, const bmt_node_health_t *h) {
    if (!bmt_mqtt_is_connected() || !h) return;

    char        dev[32], json[256];
    int         nidx = bmt_node_table_find(src);
    bmt_node_t *nn   = bmt_node_table_get(nidx);
    if (nn && nn->is_scan)
        snprintf(dev, sizeof(dev), BMT_DEV_NAME_SCAN_FMT, src);
    else if (nn && nn->is_relay)
        snprintf(dev, sizeof(dev), BMT_DEV_NAME_RELAY_FMT, src);
    else
        snprintf(dev, sizeof(dev), BMT_DEV_NAME_NODE_FMT, src);

    snprintf(json, sizeof(json),
             "{\"%s\":[{\"chip_temp\":%d,\"vdd_mv\":%u,"
             "\"free_heap_kb\":%u,\"uptime_min\":%u}]}",
             dev, h->chip_temp_c, h->vdd_mv, h->free_heap_kb, h->uptime_min);
    bmt_mqtt_publish("v1/gateway/telemetry", json, 0);
    ESP_LOGI(TAG, "HEALTH [%s] temp=%dC vdd=%umV", dev, h->chip_temp_c, h->vdd_mv);
}
