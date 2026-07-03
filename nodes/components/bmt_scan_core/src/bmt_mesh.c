#include "bmt_mesh.h"
#include "bmt_scan_core.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define BMT_GW_UNICAST_ADDR 0x0001

static const char *TAG = "BMT_MESH";

static uint8_t s_uuid[16] = {
    0x53, 0x43, 0x41, 0x4E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint16_t s_node_addr = 0x0000;
static uint16_t s_net_idx   = 0xFFFF;
static uint16_t s_app_idx   = 0xFFFF;

static EventGroupHandle_t s_evgrp;

static esp_ble_mesh_cfg_srv_t s_cfg_server = {
    .net_transmit     = ESP_BLE_MESH_TRANSMIT(7, 10),
    .relay            = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(7, 10),
    .beacon           = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state     = ESP_BLE_MESH_FRIEND_ENABLED,
    .default_ttl      = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(s_vnd_pub, sizeof(bmt_tag_report_t) + 4, ROLE_NODE);

static esp_ble_mesh_model_op_t s_vnd_ops[] = {
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_TAG_STATUS, sizeof(bmt_tag_report_t)),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t s_vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(BMT_CID_ESP, BMT_VND_MODEL_ID, s_vnd_ops, &s_vnd_pub, NULL),
};

static esp_ble_mesh_model_t s_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_cfg_server),
};

static esp_ble_mesh_elem_t s_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, s_root_models, s_vnd_models),
};

static esp_ble_mesh_comp_t s_composition = {
    .cid           = BMT_CID_ESP,
    .element_count = ARRAY_SIZE(s_elements),
    .elements      = s_elements,
};

static esp_ble_mesh_prov_t s_provision = {.uuid = s_uuid};

static esp_err_t bluetooth_init(void) {
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    return ESP_OK;
}

static void prov_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        s_net_idx   = param->node_prov_complete.net_idx;
        s_node_addr = param->node_prov_complete.addr;
        ESP_LOGI(TAG, "Provision complete addr=0x%04x net_idx=0x%04x", s_node_addr, s_net_idx);
        xEventGroupSetBits(s_evgrp, BIT0);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "Node reset -> unprovisioned");
        s_node_addr = 0x0000;
        s_net_idx   = 0xFFFF;
        s_app_idx   = 0xFFFF;
        xEventGroupClearBits(s_evgrp, BIT0);
        esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
        break;

    default:
        break;
    }
}

static void cfg_server_cb(esp_ble_mesh_cfg_server_cb_event_t  event,
                          esp_ble_mesh_cfg_server_cb_param_t *param) {
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) return;

    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        s_app_idx = param->value.state_change.appkey_add.app_idx;
        ESP_LOGI(TAG, "AppKey received idx=0x%04x", s_app_idx);
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        ESP_LOGI(TAG, "Model AppKey bind done");
        break;
    case ESP_BLE_MESH_MODEL_OP_RELAY_SET:
        ESP_LOGI(TAG, "Relay state changed");
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET:
        ESP_LOGI(TAG, "Model publication set");
        break;
    default:
        break;
    }
}

esp_err_t bmt_mesh_init(void) {
    s_uuid[15] = bmt_scan_core_scanner_id();

    s_evgrp = xEventGroupCreate();
    if (!s_evgrp) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(bluetooth_init());

    esp_ble_mesh_register_prov_callback(prov_cb);
    esp_ble_mesh_register_config_server_callback(cfg_server_cb);

    ESP_ERROR_CHECK(esp_ble_mesh_init(&s_provision, &s_composition));

    if (esp_ble_mesh_node_is_provisioned()) {
        ESP_LOGI(TAG, "Already provisioned (restored from NVS)");
        s_node_addr = 0x0001;
        s_app_idx   = 0x0000;
        xEventGroupSetBits(s_evgrp, BIT0);
    } else {
        ESP_ERROR_CHECK(
            esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        ESP_LOGI(TAG, "UUID: SCAN (53:43:41:4E:...:%02X)", s_uuid[15]);
        ESP_LOGI(TAG, "Waiting for Gateway to provision...");
    }

    return ESP_OK;
}

bool bmt_mesh_is_provisioned(void) {
    return s_node_addr != 0x0000;
}
bool bmt_mesh_is_ready(void) {
    return s_node_addr != 0x0000 && s_app_idx != 0xFFFF;
}
uint16_t bmt_mesh_node_addr(void) {
    return s_node_addr;
}
uint16_t bmt_mesh_app_idx(void) {
    return s_app_idx;
}
uint16_t bmt_mesh_net_idx(void) {
    return s_net_idx;
}

static esp_err_t publish(uint32_t opcode, const void *data, uint16_t len) {
    if (!bmt_mesh_is_ready()) return ESP_ERR_INVALID_STATE;

    s_vnd_models[0].pub->publish_addr = BMT_GW_UNICAST_ADDR;
    s_vnd_models[0].pub->app_idx      = s_app_idx;
    s_vnd_models[0].pub->ttl          = 7;

    return esp_ble_mesh_model_publish(&s_vnd_models[0], opcode, len, (uint8_t *)data, ROLE_NODE);
}

esp_err_t bmt_mesh_publish_tag_report(const bmt_tag_report_t *r) {
    return publish(BMT_OP_VND_TAG_STATUS, r, sizeof(*r));
}

esp_err_t bmt_mesh_publish_node_health(const bmt_node_health_t *h) {
    return publish(BMT_OP_VND_NODE_HEALTH, h, sizeof(*h));
}

void bmt_mesh_reset(void) {
    esp_ble_mesh_node_local_reset();
    s_node_addr = 0x0000;
    s_net_idx   = 0xFFFF;
    s_app_idx   = 0xFFFF;
}
