#include "bmt_scan.h"

static const char *TAG = "BMT_SCAN";

static const uint8_t s_uuid_prefix[4] = {0xAB, 0x00, 0x00, 0x00};

static volatile bmt_radio_phase_t s_phase = BMT_PHASE_GAP_SCAN;

static esp_ble_scan_params_t s_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = BMT_SCAN_INTERVAL_UNITS,
    .scan_window        = BMT_SCAN_WINDOW_UNITS,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
};

static bool parse_adv(uint8_t *adv_data, uint8_t adv_len, bmt_parsed_t *out) {
    if (!adv_data || adv_len < 4 || !out) return false;
    int pos = 0;

    while (pos < adv_len) {
        uint8_t field_len = adv_data[pos];
        if (field_len == 0 || pos + field_len >= adv_len) break;
        uint8_t field_type = adv_data[pos + 1];

        if (field_type != 0xFF || field_len < 3) {
            pos += field_len + 1;
            continue;
        }

        uint16_t cid = (uint16_t)adv_data[pos + 2] | ((uint16_t)adv_data[pos + 3] << 8);

        if (cid == BMT_CID_ESP && field_len >= (1 + 2 + 24)) {
            bmt_adv_esp_t *p = (bmt_adv_esp_t *)(adv_data + pos + 4);
            if (memcmp(p->uuid, s_uuid_prefix, 4) != 0) goto next_field;
            uint16_t expected = bmt_crc16_ccitt((uint8_t *)p, sizeof(*p) - sizeof(p->crc16));
            if (expected != p->crc16) {
                ESP_LOGD(TAG, "CRC fail");
                goto next_field;
            }
            out->tag_type =
                (p->major == BMT_TAG_MAJOR_PERSON) ? BMT_TAG_TYPE_PERSON : BMT_TAG_TYPE_ASSET;
            out->tag_id   = p->minor;
            out->tx_power = p->tx_power;
            out->sequence = p->sequence;
            return true;
        }

        if (cid == BMT_CID_APPLE && field_len >= 26 && adv_data[pos + 4] == 0x02 &&
            adv_data[pos + 5] == 0x15) {
            uint16_t major = ((uint16_t)adv_data[pos + 22] << 8) | adv_data[pos + 23];
            uint16_t minor = ((uint16_t)adv_data[pos + 24] << 8) | adv_data[pos + 25];
            if (major != BMT_TAG_MAJOR_PERSON && major != BMT_TAG_MAJOR_ASSET) goto next_field;
            out->tag_type =
                (major == BMT_TAG_MAJOR_PERSON) ? BMT_TAG_TYPE_PERSON : BMT_TAG_TYPE_ASSET;
            out->tag_id   = minor | 0x8000;
            out->tx_power = BMT_PHONE_TX_POWER_1M;
            out->sequence = 0;
            return true;
        }

    next_field:
        pos += field_len + 1;
    }
    return false;
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan params set OK");
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
            ESP_LOGE(TAG, "Scan start FAILED");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
        if (s_phase != BMT_PHASE_GAP_SCAN) break;
        if (!bmt_mesh_is_provisioned()) break;

        bmt_parsed_t p;
        if (!parse_adv(param->scan_rst.ble_adv, param->scan_rst.adv_data_len, &p)) break;

        bmt_tag_hit_t hit = {
            .tag_type = p.tag_type,
            .tag_id   = p.tag_id,
            .tx_power = p.tx_power,
            .rssi     = param->scan_rst.rssi,
            .sequence = p.sequence,
        };
        memcpy(hit.mac, param->scan_rst.bda, 6);
        bmt_tag_table_update(&hit);
        break;
    }
    default:
        break;
    }
}

static void publish_all_tags(void) {
    uint8_t scanner_id = bmt_scan_core_scanner_id();
    int     published  = 0;

    for (int i = 0; i < BMT_MAX_TAGS; i++) {
        bmt_tag_snapshot_t s;
        if (!bmt_tag_table_get_snapshot(i, &s)) continue;

        uint32_t total    = s.total_received + s.total_missed;
        uint8_t  loss_pct = total ? (uint8_t)((s.total_missed * 100) / total) : 0;

        int16_t distance_dm = (int16_t)(s.distance * 10.0f);
        if (distance_dm < 0) distance_dm = 0;

        bmt_tag_report_t report = {
            .scanner_id  = scanner_id,
            .tag_type    = s.tag_type,
            .tag_id      = s.tag_id,
            .rssi        = s.rssi_int,
            .distance_dm = distance_dm,
            .loss_pct    = loss_pct,
        };

        esp_err_t err = bmt_mesh_publish_tag_report(&report);
        if (err == ESP_OK) {
            published++;
            ESP_LOGI(TAG, "[Mesh] Published tag 0x%04X RSSI=%ddBm Dist=%.1fm Loss=%u%%",
                     report.tag_id, report.rssi, distance_dm / 10.0f, loss_pct);
        } else {
            ESP_LOGW(TAG, "[Mesh] Publish tag 0x%04X failed: %s", s.tag_id, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (published == 0)
        ESP_LOGD(TAG, "[Mesh] No active tags");
    else
        ESP_LOGI(TAG, "[Mesh] Published %d tag(s)", published);
}

static void radio_task(void *arg) {
    (void)arg;
    uint8_t scanner_id = bmt_scan_core_scanner_id();

    ESP_LOGI(TAG, "Waiting for mesh provision...");
    while (!bmt_mesh_is_provisioned())
        vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Mesh provisioned, waiting to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_ble_gap_stop_scanning();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_err_t err = esp_ble_gap_set_scan_params(&s_scan_params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_scan_params failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    uint32_t phase_offset_ms = (scanner_id - 1) * 500;
    ESP_LOGI(TAG, "Phase offset for scanner 0x%02x: %ums", scanner_id, (unsigned)phase_offset_ms);
    vTaskDelay(pdMS_TO_TICKS(phase_offset_ms));

    s_phase = BMT_PHASE_MESH_PUB;

    while (1) {
        if (bmt_ota_is_running()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        s_phase = BMT_PHASE_GAP_SCAN;
        bmt_tag_table_clear_new_data();

        esp_ble_gap_start_scanning(0);
        vTaskDelay(pdMS_TO_TICKS(BMT_GAP_SCAN_DURATION_MS));

        s_phase = BMT_PHASE_MESH_PUB;
        esp_ble_gap_stop_scanning();
        vTaskDelay(pdMS_TO_TICKS(100));

        if (bmt_tag_table_has_new_data()) {
            ESP_LOGI(TAG, "[Phase 2] Publishing tag data to mesh...");
            publish_all_tags();
            bmt_tag_table_print(scanner_id);
            bmt_tag_table_clear_new_data();
        }

        vTaskDelay(pdMS_TO_TICKS(BMT_MESH_PUBLISH_DURATION_MS - 100));
    }
}

static void timeout_task(void *arg) {
    (void)arg;
    while (1) {
        bmt_tag_table_check_timeouts();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t bmt_scan_start(void) {
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));

    BaseType_t ok = xTaskCreate(radio_task, "bmt_radio", 3072, NULL, 6, NULL);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    ok = xTaskCreate(timeout_task, "bmt_timeout", 2048, NULL, 3, NULL);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bmt_radio_phase_t bmt_scan_get_phase(void) {
    return s_phase;
}
