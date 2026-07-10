#include "bmt_thingsboard.h"

#include <stdio.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_config.h"
#include "bmt_mqtt.h"
#include "bmt_node_table.h"
#include "bmt_zone.h"

#define BMT_MAX_SEEN_TAGS  16

/* [DASHBOARD] "type" phai trung ten 1 Device Profile da tao san trong TB UI
 * (BMT_PROFILE_TAG/BMT_PROFILE_NODE) — TB tu gan profile cho sub-device luc
 * auto-provision, giup Entity Alias trong dashboard loc duoc theo profile. */
static void tb_connect_device(const char *dev, const char *profile)
{
    if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) return;
    char json[96];
    snprintf(json, sizeof(json), "{\"device\":\"%s\",\"type\":\"%s\"}", dev, profile);
    esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/gateway/connect", json, 0, 1, 0);
}

static void tb_disconnect_device(const char *dev)
{
    if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) return;
    char json[64]; snprintf(json, sizeof(json), "{\"device\":\"%s\"}", dev);
    esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/gateway/disconnect", json, 0, 1, 0);
}

static void tb_set_role(const char *dev, const char *role)
{
    if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) return;
    char json[128]; snprintf(json, sizeof(json), "{\"%s\":{\"role\":\"%s\"}}", dev, role);
    esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/gateway/attributes", json, 0, 1, 0);
}

void bmt_tb_pub_gateway_online(void)
{
    if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) return;
    esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/devices/me/telemetry",
                            "{\"status\":\"ONLINE\"}", 0, 1, 0);
    esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/devices/me/attributes",
                            "{\"role\":\"" BMT_ROLE_GATEWAY "\"}", 0, 1, 0);
    ESP_LOGI("BMT_TB", "TB Gateway ONLINE");
}

void bmt_tb_pub_node_status(uint16_t addr, const char *role, bool online)
{
    if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) return;
    char dev[32], json[192];
    snprintf(dev, sizeof(dev), BMT_DEV_NAME_NODE_FMT, addr);
    if (online) { tb_connect_device(dev, BMT_PROFILE_NODE); tb_set_role(dev, role); }
    snprintf(json, sizeof(json), "{\"%s\":[{\"status\":\"%s\",\"addr\":\"0x%04x\"}]}",
             dev, online ? "ONLINE" : "OFFLINE", addr);
    esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/gateway/telemetry", json, 0, 1, 0);
    if (!online) tb_disconnect_device(dev);
}

void bmt_tb_pub_tag_report(const bmt_tag_report_t *r, const uint8_t *scanner_mac)
{
    if (!r) return;

    /* [v6.5-relay-only] Vẫn cập nhật bảng track + tính zone cục bộ NHƯ CŨ —
     * nhưng CHỈ để phục vụ UART '2' debug tại chỗ (bmt_zone_log_tracked()).
     * KHÔNG dùng current_zone_id bên dưới để publish lên ThingsBoard nữa. */
    bmt_tag_track_t *t = bmt_zone_track_get_or_add(r->tag_id, r->tag_type);
    if (!t) return;
    if (r->scanner_id >= 1 && r->scanner_id <= BMT_MAX_SCANNERS) {
        int sidx = r->scanner_id - 1;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        t->rssi_by_scanner[sidx]  = r->rssi;
        t->ts_by_scanner[sidx]    = now;
        t->valid_by_scanner[sidx] = true;
        t->last_any_report_ms     = now;

        uint8_t new_zone = bmt_zone_evaluate(t);
        if (new_zone != t->current_zone_id) {
            ESP_LOGI("BMT_TB", "[debug local] Tag 0x%04x ZONE: %s -> %s",
                     r->tag_id, bmt_zone_name(t->current_zone_id), bmt_zone_name(new_zone));
            t->current_zone_id = new_zone; t->last_zone_change_ms = now;
        }
    }

    if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) return;

    char dev[32], json[384];
    snprintf(dev, sizeof(dev), BMT_DEV_NAME_TAG_FMT, r->tag_id);

    static uint16_t s_seen_tags[BMT_MAX_SEEN_TAGS] = {0};
    static int s_seen_count = 0;
    bool first_seen = true;
    for (int i = 0; i < s_seen_count; i++)
        if (s_seen_tags[i] == r->tag_id) { first_seen = false; break; }
    if (first_seen && s_seen_count < BMT_MAX_SEEN_TAGS) {
        s_seen_tags[s_seen_count++] = r->tag_id;
        tb_connect_device(dev, BMT_PROFILE_TAG); tb_set_role(dev, BMT_ROLE_TAG);
    }

    /* [v6.5-relay-only] "scanner_id" gửi lên giờ là MAC THẬT (12 hex char) của
     * scanner — không phải số 1-8 tự đặt qua UART nữa. Rule chain bên
     * ThingsBoard tự ánh xạ MAC -> zone (xem ZONE_MAP), KHÔNG gửi zone/zone_id
     * tính sẵn — Gateway chỉ relay dữ liệu thô. Fallback về "id_0x%02x" nếu
     * chưa tra ra MAC (scanner chưa provision xong lúc gửi report này). */
    char scanner_key[16];
    if (scanner_mac) {
        snprintf(scanner_key, sizeof(scanner_key), "%02x%02x%02x%02x%02x%02x",
                 scanner_mac[0], scanner_mac[1], scanner_mac[2],
                 scanner_mac[3], scanner_mac[4], scanner_mac[5]);
    } else {
        snprintf(scanner_key, sizeof(scanner_key), "id_0x%02x", r->scanner_id);
        ESP_LOGW("BMT_TB", "Khong tra ra MAC cho scanner_id=0x%02x, dung fallback", r->scanner_id);
    }

    snprintf(json, sizeof(json),
             "{\"%s\":[{\"scanner_id\":\"%s\",\"type\":\"%s\","
             "\"rssi\":%d,\"distance\":%.2f,\"loss\":%u}]}",
             dev, scanner_key, r->tag_type==0x01?"PERSON":"ASSET",
             r->rssi, r->distance_dm/10.0f, r->loss_pct);

    int msg_id = esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/gateway/telemetry", json, 0, 0, 0);
    if (msg_id >= 0) {
        bmt_mqtt_note_published();
        ESP_LOGI("BMT_TB", "TB [%s] scanner=%s rssi=%d", dev, scanner_key, r->rssi);
    }
}

void bmt_tb_pub_ota_result(uint16_t addr, uint8_t status)
{
    if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) return;
    if (bmt_node_table_find(addr) < 0) return;
    char dev[32], json[128];
    snprintf(dev, sizeof(dev), BMT_DEV_NAME_NODE_FMT, addr);
    snprintf(json, sizeof(json), "{\"%s\":[{\"ota_result\":\"%s\"}]}",
             dev, status == 0 ? "SUCCESS" : "FAILED");
    esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/gateway/telemetry", json, 0, 1, 0);
}

static void zone_timeout_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        for (int i = 0; i < bmt_zone_track_capacity(); i++) {
            bmt_tag_track_t *t = bmt_zone_track_get(i);
            if (!t || !t->active) continue;
            if ((now - t->last_any_report_ms) <= BMT_TAG_OUT_OF_RANGE_MS) continue;
            if (t->current_zone_id == BMT_ZONE_UNKNOWN) continue;
            ESP_LOGW("BMT_TB", "Tag 0x%04x OUT OF RANGE", t->tag_id);
            t->current_zone_id = BMT_ZONE_UNKNOWN;
            t->last_zone_change_ms = now;
            for (int j = 0; j < BMT_MAX_SCANNERS; j++) t->valid_by_scanner[j] = false;
            if (!bmt_mqtt_is_connected() || !bmt_mqtt_get_client()) continue;
            /* [v6.5-relay-only] Ngoại lệ có chủ đích duy nhất: đây KHÔNG phải
             * thuật toán định vị (không so sánh/chọn scanner gì cả), chỉ là
             * phát hiện "im lặng quá lâu" — rule chain bên TB xử lý theo từng
             * message tới, không có cơ chế lịch định kỳ để tự phát hiện việc
             * KHÔNG có message nào tới nữa. Nên Gateway set thẳng attribute
             * current_zone/current_zone_num = out_of_range, đúng tên key mà
             * dashboard/rule chain đang dùng, bỏ qua nhánh tính toán RSSI. */
            char dev[32], json[192];
            snprintf(dev, sizeof(dev), BMT_DEV_NAME_TAG_FMT, t->tag_id);
            snprintf(json, sizeof(json),
                     "{\"%s\":{\"current_zone\":\"out_of_range\",\"current_zone_num\":0}}",
                     dev);
            esp_mqtt_client_publish(bmt_mqtt_get_client(), "v1/gateway/attributes", json, 0, 0, 0);
        }
    }
}

void bmt_tb_start_zone_timeout_task(void)
{
    xTaskCreate(zone_timeout_task, "bmt_zone_timer", 3072, NULL, 3, NULL);
}
