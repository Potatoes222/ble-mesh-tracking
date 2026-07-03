#include "bmt_config.h"
#include "bmt_mac_cache.h"
#include "bmt_node_table.h"
#include "bmt_scan_list.h"
#include "bmt_types.h"
#include "bmt_zone.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

#include "driver/uart.h"
#include "esp_task_wdt.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "ble_mesh_example_init.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static const char *TAG = "BMT_GW";

static uint16_t g_bmt_net_key_idx = 0x0000;
static uint16_t g_bmt_app_key_idx = 0x0000;

static const uint8_t g_bmt_net_key[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};
static const uint8_t g_bmt_app_key[16] = {
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
};
static uint8_t g_bmt_gw_uuid[ESP_BLE_MESH_OCTET16_LEN] = {
    0x47, 0x41, 0x54, 0x45, 0x57, 0x41, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

static EventGroupHandle_t       g_bmt_wifi_evgrp;
static const int                BMT_WIFI_CONNECTED_BIT = BIT0;
static esp_mqtt_client_handle_t g_bmt_mqtt_client      = NULL;
static bool                     g_bmt_mqtt_connected   = false;

static QueueHandle_t g_bmt_mqtt_queue     = NULL;
static uint32_t      g_bmt_mqtt_enqueued  = 0;
static uint32_t      g_bmt_mqtt_dropped   = 0;
static uint32_t      g_bmt_mqtt_published = 0;

static esp_ble_mesh_cfg_srv_t bmt_cfg_server = {
    .net_transmit     = ESP_BLE_MESH_TRANSMIT(7, 10),
    .relay            = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(7, 10),
    .beacon           = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state     = ESP_BLE_MESH_FRIEND_ENABLED,
    .default_ttl      = 7,
};

static esp_ble_mesh_client_t bmt_cfg_client;
static esp_ble_mesh_client_t bmt_vnd_client;

static esp_ble_mesh_model_op_t bmt_vnd_ops[] = {
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_TAG_STATUS, sizeof(bmt_tag_report_t)),
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_NODE_HEALTH, sizeof(bmt_node_health_t)),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t bmt_vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(BMT_CID_ESP, BMT_VND_MODEL_ID, bmt_vnd_ops, NULL, &bmt_vnd_client),
};

static esp_ble_mesh_model_t bmt_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&bmt_cfg_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&bmt_cfg_client),
};

static esp_ble_mesh_elem_t bmt_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, bmt_root_models, bmt_vnd_models),
};

static esp_ble_mesh_comp_t bmt_composition = {
    .cid           = BMT_CID_ESP,
    .element_count = ARRAY_SIZE(bmt_elements),
    .elements      = bmt_elements,
};

static esp_ble_mesh_prov_t bmt_provision = {
    .uuid               = g_bmt_gw_uuid,
    .prov_unicast_addr  = 0x0001,
    .prov_start_address = 0x0002,
};

static void bmt_print_status(void) {
    printf("\n=========== GATEWAY COMMANDS ===========\n");
    printf("1 -> LIST PROVISIONED NODES\n");
    printf("2 -> LIST TRACKED TAGS + ZONES\n");
    printf("3 -> MQTT QUEUE STATS\n");
    printf("s -> SCAN BEACONS (%ds)\n", BMT_SCAN_DURATION_MS / 1000);
    printf("p -> PROVISION SCAN LIST\n");
    printf("a -> AUTO PROVISION MODE\n");
    printf("4 -> SHOW STATUS\n");
    printf("0 -> CLEAR NVS (forget all nodes)\n");
    printf("Provision mode: %s\n",
           bmt_scan_list_get_mode() == BMT_PROV_MODE_AUTO ? "AUTO" : "MANUAL");
    printf("=========================================\n");
}

static void bmt_log_mqtt_stats(void) {
    UBaseType_t in_queue = g_bmt_mqtt_queue ? uxQueueMessagesWaiting(g_bmt_mqtt_queue) : 0;
    printf("\n========== MQTT QUEUE STATS ==========\n");
    printf("Connected       : %s\n", g_bmt_mqtt_connected ? "YES" : "NO");
    printf("Queue size      : %u / %d\n", (unsigned)in_queue, BMT_MQTT_QUEUE_SIZE);
    printf("Total enqueued  : %" PRIu32 "\n", g_bmt_mqtt_enqueued);
    printf("Total published : %" PRIu32 "\n", g_bmt_mqtt_published);
    printf("Total dropped   : %" PRIu32 " (queue full)\n", g_bmt_mqtt_dropped);
    if (g_bmt_mqtt_enqueued > 0) {
        float drop_rate = (float)g_bmt_mqtt_dropped * 100.0f / g_bmt_mqtt_enqueued;
        printf("Drop rate       : %.2f%%\n", drop_rate);
    }
    printf("======================================\n");
}

static void bmt_wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
        xEventGroupClearBits(g_bmt_wifi_evgrp, BMT_WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(g_bmt_wifi_evgrp, BMT_WIFI_CONNECTED_BIT);
    }
}

static void bmt_wifi_init(void) {
    g_bmt_wifi_evgrp = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &bmt_wifi_event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &bmt_wifi_event_handler, NULL, &got_ip));
    wifi_config_t wifi_cfg = {.sta = {.threshold.authmode = WIFI_AUTH_WPA2_PSK}};
    strncpy((char *)wifi_cfg.sta.ssid, BMT_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, BMT_WIFI_PASS, sizeof(wifi_cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(g_bmt_wifi_evgrp, BMT_WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

static void bmt_mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *data) {
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        g_bmt_mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT connected to ThingsBoard");
        break;
    case MQTT_EVENT_DISCONNECTED:
        g_bmt_mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    default:
        break;
    }
}

extern const uint8_t bmt_ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t bmt_ca_pem_end[] asm("_binary_ca_pem_end");

static void bmt_mqtt_init(void) {
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri                  = BMT_TB_HOST,
        .broker.verification.certificate     = (const char *)bmt_ca_pem_start,
        .broker.verification.certificate_len = (size_t)(bmt_ca_pem_end - bmt_ca_pem_start),
        .broker.verification.common_name     = BMT_TB_CN,
        .credentials.username                = BMT_TB_GATEWAY_TOKEN,
        .network.timeout_ms                  = BMT_MQTT_PUBLISH_TIMEOUT_MS,
        .network.reconnect_timeout_ms        = 5000,
    };
    g_bmt_mqtt_client = esp_mqtt_client_init(&cfg);
    if (!g_bmt_mqtt_client) {
        ESP_LOGE(TAG, "MQTT init failed");
        return;
    }
    esp_mqtt_client_register_event(g_bmt_mqtt_client, ESP_EVENT_ANY_ID, bmt_mqtt_event_handler,
                                   NULL);
    esp_mqtt_client_start(g_bmt_mqtt_client);
    ESP_LOGI(TAG, "MQTTS client started -> %s (verify CN=%s)", BMT_TB_HOST, BMT_TB_CN);
}

static void bmt_tb_connect_device(const char *device_name, const char *device_type) {
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char json[128];
    if (device_type && device_type[0])
        snprintf(json, sizeof(json), "{\"device\":\"%s\",\"type\":\"%s\"}", device_name,
                 device_type);
    else
        snprintf(json, sizeof(json), "{\"device\":\"%s\"}", device_name);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/connect", json, 0, 1, 0);
    ESP_LOGI(TAG, "TB CONNECT: %s (type=%s)", device_name, device_type ? device_type : "default");
}

static void bmt_tb_disconnect_device(const char *device_name) {
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char json[64];
    snprintf(json, sizeof(json), "{\"device\":\"%s\"}", device_name);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/disconnect", json, 0, 1, 0);
    ESP_LOGI(TAG, "TB DISCONNECT: %s", device_name);
}

static void bmt_tb_set_role(const char *device_name, const char *role) {
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char json[128];
    snprintf(json, sizeof(json), "{\"%s\":{\"role\":\"%s\"}}", device_name, role);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/attributes", json, 0, 1, 0);
    ESP_LOGI(TAG, "TB ATTR [%s]: role=%s", device_name, role);
}

static void bmt_mqtt_pub_gateway_online(void) {
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/devices/me/telemetry", "{\"status\":\"ONLINE\"}",
                            0, 1, 0);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/devices/me/attributes",
                            "{\"role\":\"" BMT_ROLE_GATEWAY "\"}", 0, 1, 0);
    ESP_LOGI(TAG, "TB Gateway ONLINE (role=" BMT_ROLE_GATEWAY ")");
}

static void bmt_mqtt_pub_node_status(uint16_t addr, const char *role, bool online) {
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char dev[32], json[192];
    if (role && strcmp(role, BMT_ROLE_SCAN) == 0)
        snprintf(dev, sizeof(dev), BMT_DEV_NAME_SCAN_FMT, addr);
    else if (role && strcmp(role, BMT_ROLE_RELAY) == 0)
        snprintf(dev, sizeof(dev), BMT_DEV_NAME_RELAY_FMT, addr);
    else
        snprintf(dev, sizeof(dev), BMT_DEV_NAME_NODE_FMT, addr);

    if (online) {
        bmt_tb_connect_device(dev, BMT_PROFILE_NODE);
        for (int retry = 0; retry < 3; retry++) {
            vTaskDelay(pdMS_TO_TICKS(200));
            bmt_tb_set_role(dev, role);
        }
    }
    snprintf(json, sizeof(json), "{\"%s\":[{\"status\":\"%s\",\"addr\":\"0x%04x\"}]}", dev,
             online ? "ONLINE" : "OFFLINE", addr);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/telemetry", json, 0, 1, 0);
    ESP_LOGI(TAG, "TB TELEMETRY [%s]: %s", dev, json);
    if (!online) bmt_tb_disconnect_device(dev);
}

static void bmt_mqtt_pub_tag(bmt_tag_report_t *r) {
    if (!r) return;

    bmt_zone_ingest_report(r->scanner_id, r->tag_id, r->tag_type, r->rssi);

    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;

    char dev[32], json[192], scanner_str[16];
    snprintf(dev, sizeof(dev), BMT_DEV_NAME_TAG_FMT, r->tag_id);
    snprintf(scanner_str, sizeof(scanner_str), "scan_%02u", (unsigned)r->scanner_id);

    static uint16_t s_seen_tags[BMT_MAX_SEEN_TAGS] = {0};
    static int      s_seen_count                   = 0;
    bool            first_seen                     = true;
    for (int i = 0; i < s_seen_count; i++)
        if (s_seen_tags[i] == r->tag_id) {
            first_seen = false;
            break;
        }
    if (first_seen && s_seen_count < BMT_MAX_SEEN_TAGS) {
        s_seen_tags[s_seen_count++] = r->tag_id;
        bmt_tb_connect_device(dev, BMT_PROFILE_TAG);
        for (int retry = 0; retry < 3; retry++) {
            vTaskDelay(pdMS_TO_TICKS(200));
            bmt_tb_set_role(dev, BMT_ROLE_TAG);
        }
    }

    snprintf(json, sizeof(json), "{\"%s\":[{\"scanner_id\":\"%s\",\"rssi\":%d}]}", dev, scanner_str,
             r->rssi);

    int msg_id = esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/telemetry", json, 0, 0, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "MQTT publish failed for %s", dev);
    } else {
        g_bmt_mqtt_published++;
        ESP_LOGI(TAG, "TB [%s] scanner=%s rssi=%d dBm", dev, scanner_str, r->rssi);
    }
}

static void bmt_mqtt_worker_task(void *arg) {
    (void)arg;
    bmt_tag_report_t report;
    ESP_LOGI(TAG, "MQTT worker task started (queue size=%d)", BMT_MQTT_QUEUE_SIZE);
    while (1) {
        if (xQueueReceive(g_bmt_mqtt_queue, &report, portMAX_DELAY) == pdTRUE)
            bmt_mqtt_pub_tag(&report);
    }
}

static void bmt_uart_init(void) {
    const uart_config_t cfg = {
        .baud_rate  = BMT_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BMT_UART_NUM, BMT_UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BMT_UART_NUM, &cfg));
}

static void bmt_uart_cmd_task(void *arg) {
    (void)arg;
    uint8_t ch;
    bmt_print_status();

    while (1) {
        int len = uart_read_bytes(BMT_UART_NUM, &ch, 1, pdMS_TO_TICKS(200));
        if (len <= 0 || ch == '\r' || ch == '\n') continue;

        switch (ch) {
        case '1':
            bmt_node_table_print();
            break;
        case '2':
            bmt_zone_print_all();
            break;
        case '3':
            bmt_log_mqtt_stats();
            break;
        case 's':
            printf("\n[UART] Starting MANUAL SCAN...\n");
            bmt_scan_list_do_scan();
            break;
        case 'p':
            if (bmt_scan_list_get_mode() != BMT_PROV_MODE_MANUAL)
                printf("\n[UART] Not in MANUAL mode. Press s first.\n");
            else
                bmt_scan_list_provision();
            break;
        case 'a':
            bmt_scan_list_set_mode(BMT_PROV_MODE_AUTO);
            bmt_scan_list_reset();
            printf("\n[UART] AUTO provision mode\n");
            break;
        case '4':
            bmt_print_status();
            break;
        case '0':
            printf("\n[UART] Clearing NVS + REBOOT...\n");
            {
                const esp_ble_mesh_node_t **entry = esp_ble_mesh_provisioner_get_node_table_entry();
                if (entry) {
                    int erased = 0;
                    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++) {
                        if (entry[i]) {
                            esp_ble_mesh_provisioner_delete_node_with_uuid(entry[i]->dev_uuid);
                            erased++;
                        }
                    }
                    printf("[UART] Erased %d node(s) from mesh stack\n", erased);
                }
            }
            bmt_node_table_clear();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;
        default:
            printf("\n[UART] Unknown: %c\n", ch);
            bmt_print_status();
            break;
        }
    }
}

static void bmt_scan_config_task(void *arg) {
    uint16_t addr = (uint16_t)(uint32_t)arg;
    ESP_LOGI(TAG, "[SCN_CFG] Configuring scan node 0x%04x...", addr);
    vTaskDelay(pdMS_TO_TICKS(2000));

    {
        esp_ble_mesh_client_common_param_t  c = {0};
        esp_ble_mesh_cfg_client_set_state_t s = {0};
        c.opcode                              = ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD;
        c.model                               = &bmt_root_models[1];
        c.ctx.net_idx                         = g_bmt_net_key_idx;
        c.ctx.app_idx                         = 0xFFFF;
        c.ctx.addr                            = addr;
        c.ctx.send_ttl                        = 7;
        c.msg_timeout                         = 8000;
        s.app_key_add.net_idx                 = g_bmt_net_key_idx;
        s.app_key_add.app_idx                 = g_bmt_app_key_idx;
        memcpy(s.app_key_add.app_key, g_bmt_app_key, 16);
        esp_err_t e = esp_ble_mesh_config_client_set_state(&c, &s);
        ESP_LOGI(TAG, "[SCN_CFG] Step 1: APP_KEY_ADD to 0x%04x: %s", addr,
                 e == ESP_OK ? "OK" : esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    {
        esp_ble_mesh_client_common_param_t  c = {0};
        esp_ble_mesh_cfg_client_set_state_t s = {0};
        c.opcode                              = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
        c.model                               = &bmt_root_models[1];
        c.ctx.net_idx                         = g_bmt_net_key_idx;
        c.ctx.app_idx                         = 0xFFFF;
        c.ctx.addr                            = addr;
        c.ctx.send_ttl                        = 7;
        c.msg_timeout                         = 5000;
        s.model_app_bind.element_addr         = addr;
        s.model_app_bind.model_app_idx        = g_bmt_app_key_idx;
        s.model_app_bind.model_id             = BMT_VND_MODEL_ID;
        s.model_app_bind.company_id           = BMT_CID_ESP;
        esp_err_t e                           = esp_ble_mesh_config_client_set_state(&c, &s);
        ESP_LOGI(TAG, "[SCN_CFG] Step 2: MODEL_APP_BIND to 0x%04x: %s", addr,
                 e == ESP_OK ? "OK" : esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    int idx = bmt_node_table_find(addr);
    if (idx >= 0) {
        bmt_node_table_get(idx)->config_done = true;
        bmt_node_table_save();
    }
    ESP_LOGI(TAG, "[SCN_CFG] Scan node 0x%04x fully configured!", addr);
    bmt_node_table_print();
    vTaskDelete(NULL);
}

static void bmt_mesh_prov_cb(esp_ble_mesh_prov_cb_event_t  event,
                             esp_ble_mesh_prov_cb_param_t *param) {
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
            xTaskCreate(bmt_scan_config_task, "scan_cfg", 3072, (void *)(uint32_t)addr, 5, NULL);
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

static void bmt_mesh_cfg_client_cb(esp_ble_mesh_cfg_client_cb_event_t  event,
                                   esp_ble_mesh_cfg_client_cb_param_t *param) {
    if (!param || !param->params) return;
    uint16_t    addr = param->params->ctx.addr;
    int         idx  = bmt_node_table_find(addr);
    bmt_node_t *n    = bmt_node_table_get(idx);

    if (event == ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT) {
        uint32_t opcode = param->params->opcode;
        if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD)
            ESP_LOGI(TAG, "[CFG] APP_KEY_ADD ACK from 0x%04x", addr);
        if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            ESP_LOGI(TAG, "[CFG] MODEL_APP_BIND ACK from 0x%04x", addr);
            if (n && n->is_scan) {
                ESP_LOGI(TAG, "=== Scan node 0x%04x READY ===", addr);
                bmt_mqtt_pub_node_status(addr, BMT_ROLE_SCAN, true);
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
                bmt_mqtt_pub_node_status(addr, BMT_ROLE_RELAY, true);
            }
        }
    }
}

static void bmt_mesh_cfg_server_cb(esp_ble_mesh_cfg_server_cb_event_t  event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param) {
    (void)param;
    ESP_LOGI(TAG, "Config server event: %d", event);
}

static void bmt_mesh_vnd_client_cb(esp_ble_mesh_model_cb_event_t  event,
                                   esp_ble_mesh_model_cb_param_t *param) {
    if (event != ESP_BLE_MESH_MODEL_OPERATION_EVT) return;
    if (!param) return;

    uint32_t opcode = param->model_operation.opcode;
    uint16_t src    = param->model_operation.ctx->addr;
    uint8_t *data   = param->model_operation.msg;
    uint16_t len    = param->model_operation.length;

    if (opcode == BMT_OP_VND_NODE_HEALTH) {
        if (len < sizeof(bmt_node_health_t)) {
            ESP_LOGW(TAG, "[VND-HEALTH] Payload too short: %d < %d", len,
                     (int)sizeof(bmt_node_health_t));
            return;
        }
        bmt_node_health_t health;
        memcpy(&health, data, sizeof(health));
        ESP_LOGI(TAG, "[VND-HEALTH] src=0x%04x scanner=0x%02x temp=%dC vdd=%umV heap=%uKB up=%umin",
                 src, health.scanner_id, health.chip_temp_c, health.vdd_mv, health.free_heap_kb,
                 health.uptime_min);

        if (g_bmt_mqtt_connected && g_bmt_mqtt_client) {
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
                     dev, health.chip_temp_c, health.vdd_mv, health.free_heap_kb,
                     health.uptime_min);
            esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/telemetry", json, 0, 0, 0);
            ESP_LOGI(TAG, "TB HEALTH [%s] temp=%dC vdd=%umV", dev, health.chip_temp_c,
                     health.vdd_mv);
        }
        return;
    }

    if (opcode != BMT_OP_VND_TAG_STATUS) return;
    if (len < sizeof(bmt_tag_report_t)) {
        ESP_LOGW(TAG, "[VND] Payload too short: %d < %d", len, (int)sizeof(bmt_tag_report_t));
        return;
    }

    bmt_tag_report_t report;
    memcpy(&report, data, sizeof(report));

    ESP_LOGI(TAG, "[VND] src=0x%04x scanner=0x%02x tag=0x%04x rssi=%ddBm dist=%.2fm loss=%u%%", src,
             report.scanner_id, report.tag_id, report.rssi, report.distance_dm / 10.0f,
             report.loss_pct);

    if (g_bmt_mqtt_queue) {
        if (xQueueSend(g_bmt_mqtt_queue, &report, 0) == pdTRUE) {
            g_bmt_mqtt_enqueued++;
        } else {
            g_bmt_mqtt_dropped++;
            ESP_LOGW(TAG, "MQTT queue FULL — dropped (total: %" PRIu32 ")", g_bmt_mqtt_dropped);
        }
    }
}

static void bmt_relay_ping_task(void *arg) {
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
            common.model                               = &bmt_root_models[1];
            common.ctx.net_idx                         = g_bmt_net_key_idx;
            common.ctx.app_idx                         = 0xFFFF;
            common.ctx.addr                            = n->addr;
            common.ctx.send_ttl                        = 7;
            common.msg_timeout                         = 5000;
            esp_ble_mesh_config_client_get_state(&common, &get);

            if (n->last_seen_ms > 0 && (now - n->last_seen_ms) > BMT_RELAY_OFFLINE_TIMEOUT_MS &&
                n->online) {
                n->online = false;
                ESP_LOGW(TAG, "Relay 0x%04X OFFLINE", n->addr);
                bmt_mqtt_pub_node_status(n->addr, BMT_ROLE_RELAY, false);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(BMT_RELAY_PING_INTERVAL_MS));
    }
}

static void bmt_wdt_feed_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);
    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static esp_err_t bmt_ble_mesh_init_gateway(void) {
    esp_err_t err;

    esp_ble_mesh_register_prov_callback(bmt_mesh_prov_cb);
    esp_ble_mesh_register_config_client_callback(bmt_mesh_cfg_client_cb);
    esp_ble_mesh_register_config_server_callback(bmt_mesh_cfg_server_cb);
    esp_ble_mesh_register_custom_model_callback(bmt_mesh_vnd_client_cb);

    err = esp_ble_mesh_init(&bmt_provision, &bmt_composition);
    if (err != ESP_OK) return err;

    err = esp_ble_mesh_provisioner_set_dev_uuid_match(NULL, 0, 0, false);
    if (err != ESP_OK) return err;

    err = esp_ble_mesh_provisioner_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) return err;

    const uint8_t *exist_net = esp_ble_mesh_provisioner_get_local_net_key(g_bmt_net_key_idx);
    if (!exist_net) {
        err = esp_ble_mesh_provisioner_add_local_net_key(g_bmt_net_key, g_bmt_net_key_idx);
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "NetKey added");
    }

    const uint8_t *exist_app =
        esp_ble_mesh_provisioner_get_local_app_key(g_bmt_net_key_idx, g_bmt_app_key_idx);
    if (!exist_app) {
        err = esp_ble_mesh_provisioner_add_local_app_key(g_bmt_app_key, g_bmt_net_key_idx,
                                                         g_bmt_app_key_idx);
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "AppKey added");
    }

    bmt_vnd_models[0].keys[0] = g_bmt_app_key_idx;
    ESP_LOGI(TAG, "Gateway vendor model AppKey bound: keys[0]=0x%04x", g_bmt_app_key_idx);
    ESP_LOGI(TAG, "BLE Mesh Gateway init OK");
    return ESP_OK;
}

void app_main(void) {
    esp_err_t err;
    ESP_LOGI(TAG, "=== BMT Gateway Starting ===");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    bmt_node_table_load();

    g_bmt_mqtt_queue = xQueueCreate(BMT_MQTT_QUEUE_SIZE, sizeof(bmt_tag_report_t));
    if (!g_bmt_mqtt_queue) {
        ESP_LOGE(TAG, "Failed to create MQTT queue!");
        return;
    }
    ESP_LOGI(TAG, "MQTT queue created (size=%d)", BMT_MQTT_QUEUE_SIZE);

    bmt_wifi_init();
    bmt_mqtt_init();
    vTaskDelay(pdMS_TO_TICKS(2000));

    err = bluetooth_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BT init failed");
        return;
    }

#ifdef CONFIG_IDF_TARGET_ESP32S3
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P20);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P20);
    ESP_LOGI(TAG, "BLE TX power: +20 dBm (ESP32-S3 long-range)");
#else
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    ESP_LOGI(TAG, "BLE TX power: +9 dBm (ESP32 classic max)");
#endif

    err = bmt_ble_mesh_init_gateway();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mesh init failed");
        return;
    }

    bmt_uart_init();

    printf("\n================ BMT GATEWAY ================\n");
    bmt_print_hex_key("NetKey: ", g_bmt_net_key, 16);
    bmt_print_hex_key("AppKey: ", g_bmt_app_key, 16);
    printf("TB     : %s\n", BMT_TB_HOST);
    printf("Device : %s\n", BMT_DEV_NAME_GATEWAY);
    printf("\nZONE MAPPING:\n");
    printf("  scanner 0x01 -> %s\n", bmt_zone_name(0x01));
    printf("  scanner 0x02 -> %s\n", bmt_zone_name(0x02));
    printf("  scanner 0x03 -> %s\n", bmt_zone_name(0x03));
    printf("\nZONE PARAMS:\n");
    printf("  Hysteresis     : %d dBm\n", BMT_ZONE_HYSTERESIS_DBM);
    printf("  Scanner valid  : %d ms\n", BMT_SCANNER_VALID_MS);
    printf("  Out-of-range   : %d ms\n", BMT_TAG_OUT_OF_RANGE_MS);
    printf("\nMQTT QUEUE:\n");
    printf("  Size           : %d slots\n", BMT_MQTT_QUEUE_SIZE);
    printf("=============================================\n");

    bmt_print_status();
    bmt_node_table_print();
    bmt_mqtt_pub_gateway_online();

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = BMT_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_reconfigure(&wdt_cfg);

    xTaskCreate(bmt_mqtt_worker_task, "bmt_mqtt_wkr", 4096, NULL, 5, NULL);
    xTaskCreate(bmt_uart_cmd_task, "bmt_uart", 4096, NULL, 4, NULL);
    xTaskCreate(bmt_relay_ping_task, "bmt_relay_png", 4096, NULL, 3, NULL);
    xTaskCreate(bmt_wdt_feed_task, "bmt_wdt_feed", 2048, NULL, 2, NULL);
    bmt_zone_start_timeout_task();

    ESP_LOGI(TAG, "=== BMT Gateway READY ===");
}
