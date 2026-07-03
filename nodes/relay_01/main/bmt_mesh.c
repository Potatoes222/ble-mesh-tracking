#include "bmt_mesh.h"

static const char *TAG = "BMT_MESH";

static uint8_t s_uuid[16] = {
    0x52, 0x45, 0x4C, 0x41, 0x59, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, BMT_RELAY_ID,
};

static uint16_t s_node_addr = 0x0000;
static uint16_t s_net_idx   = 0xFFFF;
static uint16_t s_app_idx   = 0xFFFF;

static EventGroupHandle_t s_evgrp;

static const uint8_t s_health_test_ids[] = {ESP_BLE_MESH_HEALTH_STANDARD_TEST};

static esp_ble_mesh_health_srv_t s_health_server = {
    .health_test =
        {
            .id_count   = 1,
            .test_ids   = s_health_test_ids,
            .company_id = BMT_CID_ESP,
        },
};

ESP_BLE_MESH_HEALTH_PUB_DEFINE(s_health_pub, 0, ROLE_NODE);

static esp_ble_mesh_cfg_srv_t s_cfg_server = {
    .net_transmit =
        ESP_BLE_MESH_TRANSMIT(BMT_RELAY_NET_TRANSMIT_COUNT, BMT_RELAY_NET_TRANSMIT_INT_MS),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit =
        ESP_BLE_MESH_TRANSMIT(BMT_RELAY_NET_TRANSMIT_COUNT, BMT_RELAY_NET_TRANSMIT_INT_MS),
    .beacon       = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy   = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
    .default_ttl  = BMT_RELAY_DEFAULT_TTL,
};

static esp_ble_mesh_model_t s_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_cfg_server),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&s_health_server, &s_health_pub),
};

static esp_ble_mesh_elem_t s_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, s_root_models, ESP_BLE_MESH_MODEL_NONE),
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

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BMT_RELAY_TX_POWER);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, BMT_RELAY_TX_POWER);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, BMT_RELAY_TX_POWER);
    return ESP_OK;
}

static void prov_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        s_net_idx   = param->node_prov_complete.net_idx;
        s_node_addr = param->node_prov_complete.addr;
        ESP_LOGI(TAG, "Provision complete addr=0x%04x net_idx=0x%04x", s_node_addr, s_net_idx);
        xEventGroupSetBits(s_evgrp, BMT_PROV_COMPLETE_BIT);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "Node reset -> unprovisioned");
        s_node_addr = 0x0000;
        s_net_idx   = 0xFFFF;
        s_app_idx   = 0xFFFF;
        xEventGroupClearBits(s_evgrp, BMT_PROV_COMPLETE_BIT);
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
    case ESP_BLE_MESH_MODEL_OP_RELAY_SET:
        ESP_LOGI(TAG, "Relay state changed by config client");
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
        ESP_LOGI(TAG, "Model subscription added");
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET:
        ESP_LOGI(TAG, "Model publication set");
        break;
    default:
        break;
    }
}

static void health_server_cb(esp_ble_mesh_health_server_cb_event_t  event,
                             esp_ble_mesh_health_server_cb_param_t *param) {
    (void)param;
    ESP_LOGI(TAG, "Health event: %d", event);
}

static void print_hex_line(const char *label, const uint8_t *data, int len) {
    printf("%s", label);
    for (int i = 0; i < len; i++) {
        printf("%02X", data[i]);
        if (i != len - 1) printf(":");
    }
    printf("\n");
}

void bmt_mesh_print_status(const char *title) {
    printf("\n========== %s ==========\n", title);
    print_hex_line("UUID      : ", s_uuid, 16);
    printf("Node addr : 0x%04X %s\n", s_node_addr,
           s_node_addr == 0x0000 ? "(UNPROVISIONED)" : "(PROVISIONED & RELAYING)");
    printf("Net idx   : 0x%04X\n", s_net_idx);
    printf("App idx   : 0x%04X\n", s_app_idx);
    printf("Relay     : %s\n",
           s_cfg_server.relay == ESP_BLE_MESH_RELAY_ENABLED ? "ENABLED" : "DISABLED");
    printf("Retransmit: count=%d interval=%dms\n",
           ESP_BLE_MESH_GET_TRANSMIT_COUNT(s_cfg_server.relay_retransmit),
           ESP_BLE_MESH_GET_TRANSMIT_INTERVAL(s_cfg_server.relay_retransmit));
    printf("TTL       : %u\n", s_cfg_server.default_ttl);
    printf("========================================\n");
}

static void monitor_task(void *arg) {
    (void)arg;
    xEventGroupWaitBits(s_evgrp, BMT_PROV_COMPLETE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(5000));

    bmt_mesh_print_status("RELAY READY");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(BMT_MONITOR_INTERVAL_MS));

        uint32_t uptime_s = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        uint32_t h        = uptime_s / 3600;
        uint32_t m        = (uptime_s % 3600) / 60;
        uint32_t sec      = uptime_s % 60;

        printf("\n========== RELAY HEALTH ==========\n");
        printf("Status    : %s\n",
               s_node_addr != 0x0000 ? "PROVISIONED & RELAYING" : "UNPROVISIONED");
        printf("Node addr : 0x%04X\n", s_node_addr);
        printf("Uptime    : %02" PRIu32 "h %02" PRIu32 "m %02" PRIu32 "s\n", h, m, sec);
        printf("Free heap : %lu bytes\n", (unsigned long)esp_get_free_heap_size());
        printf("==================================\n");
    }
}

void bmt_mesh_start_monitor(void) {
    xTaskCreate(monitor_task, "bmt_mesh_mon", 2048, NULL, 3, NULL);
}

void bmt_mesh_reset(void) {
    esp_ble_mesh_node_local_reset();
    s_node_addr = 0x0000;
    s_net_idx   = 0xFFFF;
    s_app_idx   = 0xFFFF;
}

uint16_t bmt_mesh_get_node_addr(void) {
    return s_node_addr;
}

esp_err_t bmt_mesh_init(void) {
    s_evgrp = xEventGroupCreate();
    if (!s_evgrp) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(bluetooth_init());

    esp_ble_mesh_register_prov_callback(prov_cb);
    esp_ble_mesh_register_config_server_callback(cfg_server_cb);
    esp_ble_mesh_register_health_server_callback(health_server_cb);

    ESP_ERROR_CHECK(esp_ble_mesh_init(&s_provision, &s_composition));

    if (esp_ble_mesh_node_is_provisioned()) {
        ESP_LOGI(TAG, "Already provisioned (restored from NVS)");
        s_node_addr = 0x0001;
        s_app_idx   = 0x0000;
        xEventGroupSetBits(s_evgrp, BMT_PROV_COMPLETE_BIT);
    } else {
        ESP_ERROR_CHECK(
            esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        ESP_LOGI(TAG, "UUID: RELAY (52:45:4C:41:59:...:%02X)", s_uuid[15]);
        ESP_LOGI(TAG, "Waiting for Gateway to provision...");
    }

    return ESP_OK;
}
