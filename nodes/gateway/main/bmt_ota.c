#include "bmt_ota.h"

static const char *TAG = "BMT_OTA";

static volatile bool s_running = false;

static void self_update_task(void *arg) {
    (void)arg;
    printf("\n[OTA] ===== Gateway self-update =====\n");
    printf("[OTA] URL: %s\n", BMT_OTA_GATEWAY_URL);

    esp_http_client_config_t http_cfg = {
        .url               = BMT_OTA_GATEWAY_URL,
        .timeout_ms        = BMT_OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = {.http_config = &http_cfg};
    esp_err_t              err     = esp_https_ota(&ota_cfg);

    if (err == ESP_OK) {
        printf("[OTA] ===== SUCCESS — rebooting =====\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    printf("[OTA] esp_https_ota FAILED: %s\n", esp_err_to_name(err));
    s_running = false;
    vTaskDelete(NULL);
}

typedef struct {
    uint8_t node_type;
    bool    is_scan_filter;
} distribute_arg_t;

static void distribute_task(void *arg) {
    distribute_arg_t *d         = (distribute_arg_t *)arg;
    uint8_t           node_type = d->node_type;
    bool              is_scan   = d->is_scan_filter;
    const char       *type_str  = is_scan ? "SCANNER" : "RELAY";

    int count = 0;
    for (int i = 0; i < bmt_node_table_capacity(); i++) {
        bmt_node_t *n = bmt_node_table_get(i);
        if (!n || !n->used || !n->config_done) continue;
        if (is_scan && !n->is_scan) continue;
        if (!is_scan && !n->is_relay) continue;
        count++;
    }
    if (count == 0) {
        printf("\n[OTA] No configured %s nodes\n", type_str);
        s_running = false;
        vTaskDelete(NULL);
        return;
    }
    printf("\n[OTA] ===== Trigger OTA for %d %s node(s) =====\n", count, type_str);

    int idx = 0;
    for (int i = 0; i < bmt_node_table_capacity(); i++) {
        bmt_node_t *n = bmt_node_table_get(i);
        if (!n || !n->used || !n->config_done) continue;
        if (is_scan && !n->is_scan) continue;
        if (!is_scan && !n->is_relay) continue;

        idx++;
        printf("\n[OTA] -- %s %d/%d: 0x%04x (%s) --\n", type_str, idx, count, n->addr, n->name);

        for (int retry = 0; retry < 5; retry++) {
            esp_err_t e = bmt_mesh_publish_ota_trigger(n->addr, node_type);
            printf("[OTA] TRIGGER -> 0x%04x [%d/5]: %s\n", n->addr, retry + 1,
                   e == ESP_OK ? "OK" : esp_err_to_name(e));
            if (e == ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        printf("[OTA] Wait %ds for node to OTA + reboot...\n", BMT_OTA_INTER_NODE_DELAY_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(BMT_OTA_INTER_NODE_DELAY_MS));
    }
    printf("\n[OTA] ===== %s OTA distribution complete =====\n", type_str);
    s_running = false;
    vTaskDelete(NULL);
}

static esp_err_t start_distribute(bool is_scan_filter, uint8_t node_type) {
    if (s_running) {
        ESP_LOGW(TAG, "OTA already running");
        return ESP_ERR_INVALID_STATE;
    }
    static distribute_arg_t s_arg;
    s_arg.is_scan_filter = is_scan_filter;
    s_arg.node_type      = node_type;
    s_running            = true;
    BaseType_t ok        = xTaskCreate(distribute_task, "bmt_ota_dst", 4096, &s_arg, 4, NULL);
    if (ok != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t bmt_ota_gateway_self_update(void) {
    if (s_running) {
        ESP_LOGW(TAG, "OTA already running");
        return ESP_ERR_INVALID_STATE;
    }
    s_running     = true;
    BaseType_t ok = xTaskCreate(self_update_task, "bmt_ota_self", 8192, NULL, 4, NULL);
    if (ok != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t bmt_ota_trigger_all_scanners(void) {
    return start_distribute(true, BMT_NODE_TYPE_SCANNER);
}

esp_err_t bmt_ota_trigger_all_relays(void) {
    return start_distribute(false, BMT_NODE_TYPE_RELAY);
}

bool bmt_ota_is_running(void) {
    return s_running;
}
