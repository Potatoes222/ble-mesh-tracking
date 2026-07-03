#include "bmt_mesh.h"
#include "bmt_mac_cache.h"
#include "bmt_mqtt.h"
#include "bmt_node_table.h"
#include "bmt_scan_list.h"
#include "bmt_thingsboard.h"

static const char *TAG = "BMT_MESH";

static uint16_t s_net_key_idx = 0x0000;
static uint16_t s_app_key_idx = 0x0000;

const uint8_t g_bmt_net_key[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};
const uint8_t g_bmt_app_key[16] = {
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
};
static uint8_t s_gw_uuid[ESP_BLE_MESH_OCTET16_LEN] = {
    0x47, 0x41, 0x54, 0x45, 0x57, 0x41, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

static esp_ble_mesh_cfg_srv_t s_cfg_server = {
    .net_transmit     = ESP_BLE_MESH_TRANSMIT(7, 10),
    .relay            = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(7, 10),
    .beacon           = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state     = ESP_BLE_MESH_FRIEND_ENABLED,
    .default_ttl      = 7,
};

static esp_ble_mesh_client_t s_cfg_client;
static esp_ble_mesh_client_t s_vnd_client;

static esp_ble_mesh_model_op_t s_vnd_ops[] = {
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_TAG_STATUS, sizeof(bmt_tag_report_t)),
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_NODE_HEALTH, sizeof(bmt_node_health_t)),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t s_vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(BMT_CID_ESP, BMT_VND_MODEL_ID, s_vnd_ops, NULL, &s_vnd_client),
};

static esp_ble_mesh_model_t s_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_cfg_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&s_cfg_client),
};

static esp_ble_mesh_elem_t s_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, s_root_models, s_vnd_models),
};

static esp_ble_mesh_comp_t s_composition = {
    .cid           = BMT_CID_ESP,
    .element_count = ARRAY_SIZE(s_elements),
    .elements      = s_elements,
};

static esp_ble_mesh_prov_t s_provision = {
    .uuid               = s_gw_uuid,
    .prov_unicast_addr  = 0x0001,
    .prov_start_address = 0x0002,
};

static void scan_config_task(void *arg) {
    uint16_t addr = (uint16_t)(uint32_t)arg;
    ESP_LOGI(TAG, "[SCN_CFG] Configuring 0x%04x...", addr);
    vTaskDelay(pdMS_TO_TICKS(2000));

    {
        esp_ble_mesh_client_common_param_t  c = {0};
        esp_ble_mesh_cfg_client_set_state_t s = {0};
        c.opcode                              = ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD;
        c.model                               = &s_root_models[1];
        c.ctx.net_idx                         = s_net_key_idx;
        c.ctx.app_idx                         = 0xFFFF;
        c.ctx.addr                            = addr;
        c.ctx.send_ttl                        = 7;
        c.msg_timeout                         = 8000;
        s.app_key_add.net_idx                 = s_net_key_idx;
        s.app_key_add.app_idx                 = s_app_key_idx;
        memcpy(s.app_key_add.app_key, g_bmt_app_key, 16);
        esp_err_t e = esp_ble_mesh_config_client_set_state(&c, &s);
        ESP_LOGI(TAG, "[SCN_CFG] APP_KEY_ADD 0x%04x: %s", addr,
                 e == ESP_OK ? "OK" : esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    {
        esp_ble_mesh_client_common_param_t  c = {0};
        esp_ble_mesh_cfg_client_set_state_t s = {0};
        c.opcode                              = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
        c.model                               = &s_root_models[1];
        c.ctx.net_idx                         = s_net_key_idx;
        c.ctx.app_idx                         = 0xFFFF;
        c.ctx.addr                            = addr;
        c.ctx.send_ttl                        = 7;
        c.msg_timeout                         = 5000;
        s.model_app_bind.element_addr         = addr;
        s.model_app_bind.model_app_idx        = s_app_key_idx;
        s.model_app_bind.model_id             = BMT_VND_MODEL_ID;
        s.model_app_bind.company_id           = BMT_CID_ESP;
        esp_err_t e                           = esp_ble_mesh_config_client_set_state(&c, &s);
        ESP_LOGI(TAG, "[SCN_CFG] MODEL_APP_BIND 0x%04x: %s", addr,
                 e == ESP_OK ? "OK" : esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    int idx = bmt_node_table_find(addr);
    if (idx >= 0) {
        bmt_node_table_get(idx)->config_done = true;
        bmt_node_table_save();
    }
    ESP_LOGI(TAG, "[SCN_CFG] 0x%04x done", addr);
    bmt_node_table_print();
    vTaskDelete(NULL);
}

static void prov_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner registered");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner scan enabled");
        break;

    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT: {
        const uint8_t *uuid      = param->provisioner_recv_unprov_adv_pkt.dev_uuid;
        const uint8_t *mac       = param->provisioner_recv_unprov_adv_pkt.addr;
        uint8_t        addr_type = param->provisioner_recv_unprov_adv_pkt.addr_type;
        uint16_t       oob_info  = param->provisioner_recv_unprov_adv_pkt.oob_info;

        bmt_mac_cache_store(uuid, mac);

        if (bmt_scan_list_get_mode() == BMT_PROV_MODE_MANUAL) {
            if (!bmt_scan_list_is_scanning()) break;
            if (bmt_scan_list_add(uuid, mac, addr_type, oob_info)) {
                printf("[SCAN] %-7s MAC:", bmt_uuid_type_str(uuid));
                for (int b = 0; b < 6; b++)
                    printf("%02X%s", mac[b], b < 5 ? ":" : "");
                printf("\n");
            }
            break;
        }

        if (bmt_node_table_uuid_provisioned(uuid)) break;
        ESP_LOGI(TAG, "Found unprovisioned [%s]", bmt_uuid_type_str(uuid));
        esp_ble_mesh_unprov_dev_add_t dev = {0};
        memcpy(dev.uuid, uuid, 16);
        memcpy(dev.addr, mac, 6);
        dev.addr_type = addr_type;
        dev.oob_info  = oob_info;
        dev.bearer    = ESP_BLE_MESH_PROV_ADV;
        esp_ble_mesh_provisioner_add_unprov_dev(&dev, ADD_DEV_FLUSHABLE_DEV_FLAG |
                                                          ADD_DEV_START_PROV_NOW_FLAG);
        break;
    }

    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT: {
        uint16_t       addr = param->provisioner_prov_complete.unicast_addr;
        const uint8_t *uuid = param->provisioner_prov_complete.device_uuid;
        ESP_LOGI(TAG, "Provision complete addr=0x%04x type=%s", addr, bmt_uuid_type_str(uuid));

        uint8_t mac[6] = {0};
        bmt_mac_cache_get(uuid, mac);

        int idx = bmt_node_table_add(addr, uuid, mac, NULL);
        if (idx < 0) {
            ESP_LOGW(TAG, "Node table full");
            break;
        }
        bmt_node_t *n = bmt_node_table_get(idx);

        if (bmt_uuid_is_relay(uuid)) {
            n->is_relay     = true;
            n->is_scan      = false;
            n->online       = false;
            n->last_seen_ms = 0;
            n->config_done  = true;
            snprintf(n->name, sizeof(n->name), "Relay_0x%04x", addr);
            ESP_LOGI(TAG, "Node 0x%04x = RELAY", addr);
            bmt_node_table_save();
            bmt_node_table_print();
            break;
        }

        if (bmt_uuid_is_scan(uuid)) {
            n->is_scan     = true;
            n->is_relay    = false;
            n->config_done = false;
            snprintf(n->name, sizeof(n->name), "Scan_0x%04x", addr);
            ESP_LOGI(TAG, "Node 0x%04x = SCAN, launching config task...", addr);
            bmt_node_table_save();
            bmt_node_table_print();
            xTaskCreate(scan_config_task, "scan_cfg", 3072, (void *)(uint32_t)addr, 5, NULL);
            break;
        }

        ESP_LOGW(TAG, "Unknown node type");
        bmt_node_table_save();
        bmt_node_table_print();
        break;
    }

    default:
        break;
    }
}

static void cfg_client_cb(esp_ble_mesh_cfg_client_cb_event_t  event,
                          esp_ble_mesh_cfg_client_cb_param_t *param) {
    if (!param || !param->params) return;
    uint16_t    addr = param->params->ctx.addr;
    bmt_node_t *n    = bmt_node_table_get(bmt_node_table_find(addr));

    if (event == ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT) {
        uint32_t opcode = param->params->opcode;
        if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD)
            ESP_LOGI(TAG, "[CFG] APP_KEY_ADD ACK from 0x%04x", addr);
        if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            ESP_LOGI(TAG, "[CFG] MODEL_APP_BIND ACK from 0x%04x", addr);
            if (n && n->is_scan) {
                ESP_LOGI(TAG, "=== Scan node 0x%04x READY ===", addr);
                bmt_tb_pub_node_status(addr, BMT_ROLE_SCAN, true);
            }
        }
    }

    if (event == ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT) {
        uint32_t opcode = param->params->opcode;
        ESP_LOGW(TAG, "[CFG] TIMEOUT opcode=0x%04" PRIx32 " addr=0x%04x", opcode, addr);
    }

    if (event == ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT) {
        if (n && n->is_relay) {
            n->last_seen_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (!n->online) {
                n->online = true;
                ESP_LOGI(TAG, "Relay 0x%04x ONLINE", addr);
                bmt_tb_pub_node_status(addr, BMT_ROLE_RELAY, true);
            }
        }
    }
}

static void cfg_server_cb(esp_ble_mesh_cfg_server_cb_event_t  event,
                          esp_ble_mesh_cfg_server_cb_param_t *param) {
    (void)param;
    ESP_LOGI(TAG, "Config server event: %d", event);
}

static void vnd_client_cb(esp_ble_mesh_model_cb_event_t  event,
                          esp_ble_mesh_model_cb_param_t *param) {
    if (event != ESP_BLE_MESH_MODEL_OPERATION_EVT || !param) return;

    uint32_t opcode = param->model_operation.opcode;
    uint16_t src    = param->model_operation.ctx->addr;
    uint8_t *data   = param->model_operation.msg;
    uint16_t len    = param->model_operation.length;

    if (opcode == BMT_OP_VND_NODE_HEALTH) {
        if (len < sizeof(bmt_node_health_t)) {
            ESP_LOGW(TAG, "[VND-HEALTH] short: %d < %d", len, (int)sizeof(bmt_node_health_t));
            return;
        }
        bmt_node_health_t health;
        memcpy(&health, data, sizeof(health));
        ESP_LOGI(TAG, "[VND-HEALTH] src=0x%04x temp=%dC vdd=%umV heap=%uKB up=%umin", src,
                 health.chip_temp_c, health.vdd_mv, health.free_heap_kb, health.uptime_min);
        bmt_tb_pub_node_health(src, &health);
        return;
    }

    if (opcode != BMT_OP_VND_TAG_STATUS) return;
    if (len < sizeof(bmt_tag_report_t)) {
        ESP_LOGW(TAG, "[VND] short: %d < %d", len, (int)sizeof(bmt_tag_report_t));
        return;
    }

    bmt_tag_report_t report;
    memcpy(&report, data, sizeof(report));
    ESP_LOGI(TAG, "[VND] src=0x%04x scanner=0x%02x tag=0x%04x rssi=%ddBm dist=%.2fm loss=%u%%", src,
             report.scanner_id, report.tag_id, report.rssi, report.distance_dm / 10.0f,
             report.loss_pct);

    bmt_mqtt_enqueue_tag_report(&report);
}

static void relay_ping_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(30000));

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        for (int i = 0; i < BMT_MAX_NODES; i++) {
            bmt_node_t *n = bmt_node_table_get(i);
            if (!n->used || !n->is_relay) continue;

            esp_ble_mesh_client_common_param_t  common = {0};
            esp_ble_mesh_cfg_client_get_state_t get    = {0};
            common.opcode                              = ESP_BLE_MESH_MODEL_OP_DEFAULT_TTL_GET;
            common.model                               = &s_root_models[1];
            common.ctx.net_idx                         = s_net_key_idx;
            common.ctx.app_idx                         = 0xFFFF;
            common.ctx.addr                            = n->addr;
            common.ctx.send_ttl                        = 7;
            common.msg_timeout                         = 5000;
            esp_ble_mesh_config_client_get_state(&common, &get);

            if (n->last_seen_ms > 0 && (now - n->last_seen_ms) > BMT_RELAY_OFFLINE_TIMEOUT_MS &&
                n->online) {
                n->online = false;
                ESP_LOGW(TAG, "Relay 0x%04X OFFLINE", n->addr);
                bmt_tb_pub_node_status(n->addr, BMT_ROLE_RELAY, false);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(BMT_RELAY_PING_INTERVAL_MS));
    }
}

esp_err_t bmt_mesh_init(void) {
    esp_err_t err;

    esp_ble_mesh_register_prov_callback(prov_cb);
    esp_ble_mesh_register_config_client_callback(cfg_client_cb);
    esp_ble_mesh_register_config_server_callback(cfg_server_cb);
    esp_ble_mesh_register_custom_model_callback(vnd_client_cb);

    err = esp_ble_mesh_init(&s_provision, &s_composition);
    if (err != ESP_OK) return err;

    err = esp_ble_mesh_provisioner_set_dev_uuid_match(NULL, 0, 0, false);
    if (err != ESP_OK) return err;

    err = esp_ble_mesh_provisioner_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) return err;

    if (!esp_ble_mesh_provisioner_get_local_net_key(s_net_key_idx)) {
        err = esp_ble_mesh_provisioner_add_local_net_key(g_bmt_net_key, s_net_key_idx);
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "NetKey added");
    }

    if (!esp_ble_mesh_provisioner_get_local_app_key(s_net_key_idx, s_app_key_idx)) {
        err =
            esp_ble_mesh_provisioner_add_local_app_key(g_bmt_app_key, s_net_key_idx, s_app_key_idx);
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "AppKey added");
    }

    s_vnd_models[0].keys[0] = s_app_key_idx;
    ESP_LOGI(TAG, "Gateway vendor model AppKey bound: keys[0]=0x%04x", s_app_key_idx);
    ESP_LOGI(TAG, "BLE Mesh Gateway init OK");
    return ESP_OK;
}

void bmt_mesh_start_relay_ping(void) {
    xTaskCreate(relay_ping_task, "bmt_relay_png", 4096, NULL, 3, NULL);
}
