/* ============================================================================
 * BMT (BLE Mesh Tracking) — GATEWAY firmware  [v4.8]
 * ----------------------------------------------------------------------------
 * v4.8 — ĐỔI cách OTA Scanner/Relay:
 *   CŨ: Gateway download firmware rồi phân phối chunk-by-chunk qua BLE Mesh
 *       → BLE Mesh throughput thấp, packet drop, không reliable
 *   MỚI: Gateway gửi OTA_TRIGGER (1 packet nhỏ) qua mesh
 *        → Scanner/Relay tự bật WiFi + esp_https_ota() download firmware
 *        → Gửi LẦN LƯỢT từng node, cách nhau để mỗi con OTA xong mới sang con kế
 *        → Server HTTP không bị nhiều client cùng lúc
 *   Gateway tự OTA vẫn dùng WiFi như cũ (lệnh 'g')
 *
 * v4.6 (giữ nguyên): watchdog, relay config, mesh received counter
 * ============================================================================ */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <limits.h>

#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"
#include "esp_rom_crc.h"
#include "esp_crt_bundle.h"

#include "driver/uart.h"
#include "esp_task_wdt.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_gap_ble_api.h"         /* esp_ble_gap_config_adv_data_raw/start/stop_advertising [v4.8] */

#include "ble_mesh_example_init.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define TAG "BMT_GW"

/* ============================================================================
 * USER CONFIG
 * ============================================================================ */
#define BMT_WIFI_SSID                   "Hao"
#define BMT_WIFI_PASS                   "Hao@1510"

#define BMT_TB_HOST                     "mqtt://mqtt.thingsboard.cloud:1883"
#define BMT_TB_GATEWAY_TOKEN            "cezpltdm3i3uiztok0lu"

#define BMT_DEV_NAME_GATEWAY            "bmt_gateway"
#define BMT_DEV_NAME_NODE_FMT           "bmt_node_0x%04x"
#define BMT_DEV_NAME_TAG_FMT            "bmt_tag_0x%04x"

#define BMT_ROLE_GATEWAY                "gateway"
#define BMT_ROLE_RELAY                  "relay"
#define BMT_ROLE_SCAN                   "scan"
#define BMT_ROLE_TAG                    "tag"

/* ============================================================================
 * OTA CONFIG
 * ============================================================================ */
#define BMT_OTA_SERVER_BASE             "http://192.168.2.23:8080"
#define BMT_OTA_SCANNER_URL             BMT_OTA_SERVER_BASE "/Scanner.bin"
#define BMT_OTA_RELAY_URL               BMT_OTA_SERVER_BASE "/Relay.bin"
#define BMT_OTA_GATEWAY_URL             BMT_OTA_SERVER_BASE "/Gateway.bin"
#define BMT_OTA_SPIFFS_PATH             "/spiffs/ota_fw.bin"
#define BMT_OTA_CHUNK_DATA_LEN          6
#define BMT_OTA_ACK_TIMEOUT_MS          2000
#define BMT_OTA_MAX_RETRY               3
#define BMT_OTA_INTER_CHUNK_MS          20
#define BMT_OTA_ANNOUNCE_WAIT_MS        3000

#define BMT_NODE_TYPE_SCANNER           0x01
#define BMT_NODE_TYPE_RELAY             0x02
#define BMT_NODE_TYPE_ALL               0xFF

/* ============================================================================
 * ZONE DETECTION CONFIG
 * ============================================================================ */
#define BMT_MAX_SCANNERS                8
#define BMT_MAX_TRACKED_TAGS            16
#define BMT_ZONE_HYSTERESIS_DBM         5
#define BMT_SCANNER_VALID_MS            3500
#define BMT_TAG_OUT_OF_RANGE_MS         10000
#define BMT_ZONE_UNKNOWN                0xFF

/* ============================================================================
 * MQTT QUEUE CONFIG
 * ============================================================================ */
#define BMT_MQTT_QUEUE_SIZE             64
#define BMT_MQTT_PUBLISH_TIMEOUT_MS     500

/* ============================================================================
 * BLE MESH VENDOR MODEL
 * ============================================================================ */
#define BMT_CID_ESP                     0x02E5
#define BMT_VND_MODEL_ID                0x0000

#define BMT_OP_VND_TAG_STATUS           ESP_BLE_MESH_MODEL_OP_3(0x00, BMT_CID_ESP)
#define BMT_OP_VND_OTA_ANNOUNCE         ESP_BLE_MESH_MODEL_OP_3(0x01, BMT_CID_ESP)
#define BMT_OP_VND_OTA_CHUNK            ESP_BLE_MESH_MODEL_OP_3(0x02, BMT_CID_ESP)
#define BMT_OP_VND_OTA_END              ESP_BLE_MESH_MODEL_OP_3(0x03, BMT_CID_ESP)
#define BMT_OP_VND_OTA_ACK              ESP_BLE_MESH_MODEL_OP_3(0x04, BMT_CID_ESP)
#define BMT_OP_VND_RESET_CMD            ESP_BLE_MESH_MODEL_OP_3(0x05, BMT_CID_ESP)
#define BMT_OP_VND_OTA_TRIGGER          ESP_BLE_MESH_MODEL_OP_3(0x06, BMT_CID_ESP) /* [v4.8] lệnh OTA WiFi */

/* ============================================================================
 * WATCHDOG CONFIG  [v4.6]
 * ============================================================================ */
#define BMT_WDG_TIMEOUT_MS              30000
#define BMT_WDG_RESET_TRIES             5
#define BMT_WDG_RESET_GAP_MS            1500
#define BMT_WDG_NODE_WAIT_MS            12000

/* ============================================================================
 * STRUCTS
 * ============================================================================ */
#pragma pack(1)
typedef struct {
    uint8_t  scanner_id;
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   rssi;
    int16_t  distance_dm;
    uint8_t  loss_pct;
} bmt_tag_report_t;

typedef struct {
    uint8_t  target_type;
    uint16_t total_chunks;
    uint32_t fw_size;
    uint8_t  chunk_data_len;
} bmt_ota_announce_t;

typedef struct {
    uint16_t chunk_idx;
    uint8_t  data[BMT_OTA_CHUNK_DATA_LEN];
} bmt_ota_chunk_t;

typedef struct { uint32_t fw_crc32; } bmt_ota_end_t;

typedef struct {
    uint16_t chunk_idx;
    uint8_t  status;
} bmt_ota_ack_t;
#pragma pack()

/* ============================================================================
 * TIMING / RESOURCES
 * ============================================================================ */
#define BMT_MAX_NODES                   10
#define BMT_MAX_SCAN_LIST               10
#define BMT_MAX_MAC_CACHE               16
#define BMT_MAX_SEEN_TAGS               16

#define BMT_RELAY_PING_INTERVAL_MS      20000
#define BMT_RELAY_OFFLINE_TIMEOUT_MS    60000
#define BMT_SCAN_DURATION_MS            10000
#define BMT_WDT_TIMEOUT_S               60

#define BMT_NVS_NAMESPACE               "bmt_gw"
#define BMT_NVS_KEY_NODES               "node_table"

#define BMT_UART_NUM                    UART_NUM_0
#define BMT_UART_BAUD                   115200
#define BMT_UART_RX_BUF_SIZE            2048

/* ============================================================================
 * ENUM / TYPEDEF
 * ============================================================================ */
typedef enum { BMT_PROV_MODE_AUTO=0, BMT_PROV_MODE_MANUAL=1 } bmt_gateway_prov_mode_t;

typedef struct { bool used; uint8_t uuid[16]; uint8_t mac[6]; } bmt_gateway_mac_cache_t;

typedef struct {
    bool     used, is_relay, is_scan, config_done, online;
    uint32_t last_seen_ms;
    uint16_t addr;
    uint8_t  uuid[16], mac[6];
    char     name[32];
} bmt_gateway_node_t;

typedef struct {
    bool     used;
    uint8_t  uuid[16], addr[6];
    uint8_t  addr_type;
    uint16_t oob_info;
} bmt_gateway_scan_entry_t;

typedef struct {
    bool     active;
    uint16_t tag_id;
    uint8_t  tag_type;
    int8_t   rssi_by_scanner [BMT_MAX_SCANNERS];
    uint32_t ts_by_scanner   [BMT_MAX_SCANNERS];
    bool     valid_by_scanner[BMT_MAX_SCANNERS];
    uint8_t  current_zone_id;
    uint32_t last_zone_change_ms, last_any_report_ms;
} bmt_gateway_tag_track_t;

/* ============================================================================
 * GLOBALS
 * ============================================================================ */
static bmt_gateway_mac_cache_t   g_bmt_mac_cache [BMT_MAX_MAC_CACHE];
static bmt_gateway_node_t        g_bmt_nodes     [BMT_MAX_NODES];
static bmt_gateway_scan_entry_t  g_bmt_scan_list [BMT_MAX_SCAN_LIST];
static bmt_gateway_tag_track_t   g_bmt_tag_track [BMT_MAX_TRACKED_TAGS];

static int                       g_bmt_scan_count = 0;
static bool                      g_bmt_scanning   = false;
static bmt_gateway_prov_mode_t   g_bmt_prov_mode  = BMT_PROV_MODE_AUTO;

static uint16_t g_bmt_net_key_idx = 0x0000;
static uint16_t g_bmt_app_key_idx = 0x0000;

static const uint8_t g_bmt_net_key[16] = {
    0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
    0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef
};
static const uint8_t g_bmt_app_key[16] = {
    0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,
    0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10
};

static EventGroupHandle_t       g_bmt_wifi_evgrp;
static const int                BMT_WIFI_CONNECTED_BIT = BIT0;
static esp_mqtt_client_handle_t g_bmt_mqtt_client   = NULL;
static bool                     g_bmt_mqtt_connected = false;

static QueueHandle_t            g_bmt_mqtt_queue     = NULL;
static uint32_t                 g_bmt_mqtt_enqueued  = 0;
static uint32_t                 g_bmt_mqtt_dropped   = 0;
static uint32_t                 g_bmt_mqtt_published = 0;

/* [FIX-1] Counter đếm mesh message nhận từ scanner — độc lập với MQTT.
 * Watchdog check counter này thay vì mqtt_published để tránh false-positive
 * khi MQTT down nhưng BLE Mesh vẫn hoạt động bình thường. */
static volatile uint32_t        g_bmt_mesh_received  = 0;

static EventGroupHandle_t       g_bmt_ota_evgrp   = NULL;
static const int                BMT_OTA_ACK_BIT   = BIT0;
static volatile bool            g_bmt_ota_running = false;

/* ============================================================================
 * MESH MODELS
 * ============================================================================ */
static esp_ble_mesh_cfg_srv_t bmt_cfg_server = {
    .net_transmit     = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay            = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon           = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state     = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl      = 7,
};

static esp_ble_mesh_client_t bmt_cfg_client;
static esp_ble_mesh_client_t bmt_vnd_client;
ESP_BLE_MESH_MODEL_PUB_DEFINE(bmt_vnd_pub,
                               sizeof(bmt_ota_chunk_t) + 4,
                               ROLE_PROVISIONER);
                              
static esp_ble_mesh_model_op_t bmt_vnd_ops[] = {
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_TAG_STATUS, sizeof(bmt_tag_report_t)),
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_OTA_ACK,    sizeof(bmt_ota_ack_t)),
    ESP_BLE_MESH_MODEL_OP_END,
};
static esp_ble_mesh_model_t bmt_vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(BMT_CID_ESP, BMT_VND_MODEL_ID,
                              bmt_vnd_ops, &bmt_vnd_pub, &bmt_vnd_client),
};

static esp_ble_mesh_model_t bmt_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&bmt_cfg_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&bmt_cfg_client),
};

static esp_ble_mesh_elem_t bmt_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, bmt_root_models, bmt_vnd_models),
};

static esp_ble_mesh_comp_t bmt_composition = {
    .cid = BMT_CID_ESP, .element_count = ARRAY_SIZE(bmt_elements), .elements = bmt_elements,
};

static esp_ble_mesh_prov_t bmt_provision = {
    .prov_unicast_addr = 0x0001, .prov_start_address = 0x0002,
};

/* ============================================================================
 * MAC CACHE
 * ============================================================================ */
static void bmt_mac_cache_store(const uint8_t *uuid, const uint8_t *mac)
{
    for (int i = 0; i < BMT_MAX_MAC_CACHE; i++) {
        if (g_bmt_mac_cache[i].used && memcmp(g_bmt_mac_cache[i].uuid,uuid,16)==0) {
            memcpy(g_bmt_mac_cache[i].mac, mac, 6); return;
        }
    }
    for (int i = 0; i < BMT_MAX_MAC_CACHE; i++) {
        if (!g_bmt_mac_cache[i].used) {
            g_bmt_mac_cache[i].used = true;
            memcpy(g_bmt_mac_cache[i].uuid, uuid, 16);
            memcpy(g_bmt_mac_cache[i].mac,  mac,  6);
            return;
        }
    }
}

static bool bmt_mac_cache_get(const uint8_t *uuid, uint8_t *mac_out)
{
    for (int i = 0; i < BMT_MAX_MAC_CACHE; i++) {
        if (g_bmt_mac_cache[i].used && memcmp(g_bmt_mac_cache[i].uuid,uuid,16)==0) {
            memcpy(mac_out, g_bmt_mac_cache[i].mac, 6); return true;
        }
    }
    return false;
}

/* ============================================================================
 * NVS
 * ============================================================================ */
static void bmt_nvs_save_nodes(void)
{
    nvs_handle_t h;
    if (nvs_open(BMT_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, BMT_NVS_KEY_NODES, g_bmt_nodes, sizeof(g_bmt_nodes)) == ESP_OK)
        nvs_commit(h);
    nvs_close(h);
}

static void bmt_nvs_load_nodes(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(BMT_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved node table, starting fresh"); return;
    }
    if (err != ESP_OK) return;
    size_t size = sizeof(g_bmt_nodes);
    err = nvs_get_blob(h, BMT_NVS_KEY_NODES, g_bmt_nodes, &size);
    nvs_close(h);
    if (err == ESP_OK) {
        int count = 0;
        for (int i = 0; i < BMT_MAX_NODES; i++) {
            if (!g_bmt_nodes[i].used) continue;
            count++;
            if (g_bmt_nodes[i].is_relay) {
                g_bmt_nodes[i].online = false;
                g_bmt_nodes[i].last_seen_ms = 0;
            }
        }
        ESP_LOGI(TAG, "Node table loaded (%d nodes)", count);
    }
}

static void bmt_nvs_clear_nodes(void)
{
    nvs_handle_t h;
    if (nvs_open(BMT_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, BMT_NVS_KEY_NODES);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "NVS cleared");
}

/* ============================================================================
 * HELPERS
 * ============================================================================ */
static void bmt_print_hex_key(const char *label, const uint8_t *key, int len)
{
    printf("%s", label);
    for (int i = 0; i < len; i++) { printf("%02X", key[i]); if (i!=len-1) printf(":"); }
    printf("\n");
}

static void bmt_print_uuid(const uint8_t *uuid)
{
    if (!uuid) return;
    for (int i = 0; i < 16; i++) { printf("%02X", uuid[i]); if (i!=15) printf(":"); }
}

static void bmt_print_mac(const uint8_t *mac)
{
    if (!mac) return;
    for (int i = 0; i < 6; i++) { printf("%02X", mac[i]); if (i!=5) printf(":"); }
}

static bool bmt_uuid_is_relay(const uint8_t *uuid)
{
    if (!uuid) return false;
    return (uuid[0]==0x52&&uuid[1]==0x45&&uuid[2]==0x4C&&uuid[3]==0x41&&uuid[4]==0x59);
}

static bool bmt_uuid_is_scan(const uint8_t *uuid)
{
    if (!uuid) return false;
    return (uuid[0]==0x53&&uuid[1]==0x43&&uuid[2]==0x41&&uuid[3]==0x4E);
}

static const char *bmt_uuid_type_str(const uint8_t *uuid)
{
    if (bmt_uuid_is_relay(uuid)) return "RELAY";
    if (bmt_uuid_is_scan(uuid))  return "SCAN";
    return "UNKNOWN";
}

static int bmt_find_node_index(uint16_t addr)
{
    for (int i = 0; i < BMT_MAX_NODES; i++)
        if (g_bmt_nodes[i].used && g_bmt_nodes[i].addr == addr) return i;
    return -1;
}

static bool bmt_uuid_already_provisioned(const uint8_t *uuid)
{
    for (int i = 0; i < BMT_MAX_NODES; i++)
        if (g_bmt_nodes[i].used && memcmp(g_bmt_nodes[i].uuid,uuid,16)==0) return true;
    return false;
}

static int bmt_add_node(uint16_t addr, const uint8_t *uuid,
                        const uint8_t *mac, const char *name)
{
    int idx = bmt_find_node_index(addr);
    if (idx >= 0) {
        if (uuid) memcpy(g_bmt_nodes[idx].uuid, uuid, 16);
        if (mac)  memcpy(g_bmt_nodes[idx].mac,  mac,  6);
        if (name && name[0])
            strncpy(g_bmt_nodes[idx].name, name, sizeof(g_bmt_nodes[idx].name)-1);
        return idx;
    }
    for (int i = 0; i < BMT_MAX_NODES; i++) {
        if (!g_bmt_nodes[i].used) {
            memset(&g_bmt_nodes[i], 0, sizeof(g_bmt_nodes[i]));
            g_bmt_nodes[i].used = true;
            g_bmt_nodes[i].addr = addr;
            if (uuid) memcpy(g_bmt_nodes[i].uuid, uuid, 16);
            if (mac)  memcpy(g_bmt_nodes[i].mac,  mac,  6);
            if (name && name[0])
                strncpy(g_bmt_nodes[i].name, name, sizeof(g_bmt_nodes[i].name)-1);
            else
                snprintf(g_bmt_nodes[i].name, sizeof(g_bmt_nodes[i].name), "Node_0x%04x", addr);
            return i;
        }
    }
    return -1;
}

static void bmt_log_node_table(void)
{
    printf("\n================ NODE TABLE ================\n");
    bool has_node = false;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < BMT_MAX_NODES; i++) {
        if (!g_bmt_nodes[i].used) continue;
        has_node = true;
        printf("Node %d\n", i+1);
        printf("  Address     : 0x%04X\n", g_bmt_nodes[i].addr);
        printf("  Name        : %s\n", g_bmt_nodes[i].name[0] ? g_bmt_nodes[i].name : "(unknown)");
        printf("  Type        : %s\n",
               g_bmt_nodes[i].is_relay ? "RELAY" : g_bmt_nodes[i].is_scan ? "SCAN" : "UNKNOWN");
        printf("  UUID        : "); bmt_print_uuid(g_bmt_nodes[i].uuid); printf("\n");
        printf("  MAC         : "); bmt_print_mac(g_bmt_nodes[i].mac);   printf("\n");
        printf("  Config done : %s\n", g_bmt_nodes[i].config_done ? "YES" : "NO");
        if (g_bmt_nodes[i].is_relay) {
            printf("  Status      : %s\n", g_bmt_nodes[i].online ? "ONLINE" : "OFFLINE");
            if (g_bmt_nodes[i].last_seen_ms > 0)
                printf("  LastSeen    : %" PRIu32 "s ago\n", (now-g_bmt_nodes[i].last_seen_ms)/1000);
            else printf("  LastSeen    : never\n");
        } else if (g_bmt_nodes[i].is_scan) {
            printf("  Status      : %s\n", g_bmt_nodes[i].config_done ? "ACTIVE" : "CONFIGURING...");
        }
        printf("------------------------------------------\n");
    }
    if (!has_node) { printf("  No provisioned nodes\n------------------------------------------\n"); }
}

/* ============================================================================
 * SCAN LIST
 * ============================================================================ */
static void bmt_print_scan_list(void)
{
    printf("\n========== SCAN LIST ==========\n");
    if (g_bmt_scan_count == 0) { printf("  (empty)\n================================\n"); return; }
    for (int i = 0; i < g_bmt_scan_count; i++) {
        bool already = bmt_uuid_already_provisioned(g_bmt_scan_list[i].uuid);
        printf("  [%d] %-7s UUID: ", i, bmt_uuid_type_str(g_bmt_scan_list[i].uuid));
        bmt_print_uuid(g_bmt_scan_list[i].uuid);
        printf("\n        MAC : "); bmt_print_mac(g_bmt_scan_list[i].addr);
        printf("  %s\n", already ? "[ALREADY PROVISIONED]" : "[NEW]");
    }
    printf("================================\n");
}

static void bmt_do_scan(void)
{
    g_bmt_scan_count = 0;
    memset(g_bmt_scan_list, 0, sizeof(g_bmt_scan_list));
    g_bmt_scanning  = true;
    g_bmt_prov_mode = BMT_PROV_MODE_MANUAL;
    printf("\n[SCAN] Scanning %ds...\n", BMT_SCAN_DURATION_MS/1000);
    vTaskDelay(pdMS_TO_TICKS(BMT_SCAN_DURATION_MS));
    g_bmt_scanning = false;
    printf("[SCAN] Done.\n");
    bmt_print_scan_list();
}

static void bmt_provision_scan_list(void)
{
    int count = 0;
    for (int i = 0; i < g_bmt_scan_count; i++) {
        if (bmt_uuid_already_provisioned(g_bmt_scan_list[i].uuid)) {
            printf("[PROV] Skip [%d] already provisioned\n", i); continue;
        }
        esp_ble_mesh_unprov_dev_add_t dev = {0};
        memcpy(dev.uuid, g_bmt_scan_list[i].uuid, 16);
        memcpy(dev.addr, g_bmt_scan_list[i].addr,  6);
        dev.addr_type = g_bmt_scan_list[i].addr_type;
        dev.oob_info  = g_bmt_scan_list[i].oob_info;
        dev.bearer    = ESP_BLE_MESH_PROV_ADV;
        esp_err_t err = esp_ble_mesh_provisioner_add_unprov_dev(
            &dev, ADD_DEV_FLUSHABLE_DEV_FLAG | ADD_DEV_START_PROV_NOW_FLAG);
        if (err == ESP_OK) {
            printf("[PROV] Provisioning [%d] %s...\n", i, bmt_uuid_type_str(g_bmt_scan_list[i].uuid));
            count++; vTaskDelay(pdMS_TO_TICKS(500));
        } else printf("[PROV] Failed [%d]: %s\n", i, esp_err_to_name(err));
    }
    if (count == 0) printf("[PROV] Nothing to provision.\n");
}

static void bmt_print_status(void)
{
    printf("\n=========== GATEWAY COMMANDS ===========\n");
    printf("1 -> LIST PROVISIONED NODES\n");
    printf("2 -> LIST TRACKED TAGS + ZONES\n");
    printf("3 -> MQTT / MESH STATS\n");
    printf("s -> SCAN BEACONS (%ds)\n", BMT_SCAN_DURATION_MS/1000);
    printf("p -> PROVISION SCAN LIST\n");
    printf("a -> AUTO PROVISION MODE\n");
    printf("m -> MANUAL PROVISION MODE\n");
    printf("4 -> SHOW STATUS\n");
    printf("u -> [OTA] UPDATE SCANNER/RELAY VIA MESH\n");
    printf("g -> [OTA] UPDATE GATEWAY FIRMWARE\n");
    printf("0 -> SOFT RESET (restart, giu tat ca mesh + NVS)\n");
    printf("9 -> FULL RESET (xoa tat ca, re-provision)\n");
    printf("Provision mode: %s\n",
           g_bmt_prov_mode == BMT_PROV_MODE_AUTO ? "AUTO" : "MANUAL");
    printf("=========================================\n");
}

/* ============================================================================
 * ZONE DETECTION
 * ============================================================================ */
static const char *bmt_zone_name(uint8_t scanner_id)
{
    switch (scanner_id) {
    case 0x01: return "bedroom_1";
    case 0x02: return "bedroom_2";
    case 0x03: return "toilet";
    case BMT_ZONE_UNKNOWN: return "out_of_range";
    default:   return "unknown";
    }
}

static bmt_gateway_tag_track_t *bmt_tag_track_find(uint16_t tag_id)
{
    for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++)
        if (g_bmt_tag_track[i].active && g_bmt_tag_track[i].tag_id == tag_id)
            return &g_bmt_tag_track[i];
    return NULL;
}

static bmt_gateway_tag_track_t *bmt_tag_track_get_or_add(uint16_t tag_id, uint8_t tag_type)
{
    bmt_gateway_tag_track_t *t = bmt_tag_track_find(tag_id);
    if (t) return t;
    for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++) {
        if (!g_bmt_tag_track[i].active) {
            memset(&g_bmt_tag_track[i], 0, sizeof(g_bmt_tag_track[i]));
            g_bmt_tag_track[i].active          = true;
            g_bmt_tag_track[i].tag_id          = tag_id;
            g_bmt_tag_track[i].tag_type        = tag_type;
            g_bmt_tag_track[i].current_zone_id = BMT_ZONE_UNKNOWN;
            ESP_LOGI(TAG, "New tag tracked: 0x%04x", tag_id);
            return &g_bmt_tag_track[i];
        }
    }
    return NULL;
}

static uint8_t bmt_zone_evaluate(bmt_gateway_tag_track_t *t)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    int best_rssi = INT_MIN; uint8_t best_scanner = BMT_ZONE_UNKNOWN;
    int current_rssi = INT_MIN; bool current_fresh = false;

    for (int i = 0; i < BMT_MAX_SCANNERS; i++) {
        if (!t->valid_by_scanner[i]) continue;
        if (now - t->ts_by_scanner[i] > BMT_SCANNER_VALID_MS) { t->valid_by_scanner[i]=false; continue; }
        if ((int)t->rssi_by_scanner[i] > best_rssi) { best_rssi=t->rssi_by_scanner[i]; best_scanner=i+1; }
        if ((i+1) == t->current_zone_id) { current_rssi=t->rssi_by_scanner[i]; current_fresh=true; }
    }

    if (best_scanner == BMT_ZONE_UNKNOWN) return BMT_ZONE_UNKNOWN;
    if (t->current_zone_id == BMT_ZONE_UNKNOWN || !current_fresh) return best_scanner;
    if (best_scanner != t->current_zone_id)
        if ((best_rssi - current_rssi) < BMT_ZONE_HYSTERESIS_DBM) return t->current_zone_id;
    return best_scanner;
}

static void bmt_log_tag_track(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    printf("\n========== TRACKED TAGS ==========\n");
    bool any = false;
    for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++) {
        bmt_gateway_tag_track_t *t = &g_bmt_tag_track[i];
        if (!t->active) continue;
        any = true;
        printf("Tag 0x%04x (%s)\n", t->tag_id, t->tag_type==0x01?"PERSON":"ASSET");
        printf("  Zone       : %s (0x%02x)\n", bmt_zone_name(t->current_zone_id), t->current_zone_id);
        if (t->last_zone_change_ms > 0)
            printf("  Zone since : %" PRIu32 "s ago\n", (now-t->last_zone_change_ms)/1000);
        printf("  Last report: %" PRIu32 "s ago\n", (now-t->last_any_report_ms)/1000);
        printf("  Scanners   :\n");
        for (int j = 0; j < BMT_MAX_SCANNERS; j++) {
            if (!t->valid_by_scanner[j]) continue;
            uint32_t age = now - t->ts_by_scanner[j];
            printf("    0x%02x %-12s RSSI=%4d  %s\n", j+1, bmt_zone_name(j+1),
                   t->rssi_by_scanner[j], age<=BMT_SCANNER_VALID_MS?"FRESH":"STALE");
        }
        printf("----------------------------------\n");
    }
    if (!any) printf("  No tracked tags\n----------------------------------\n");
}

/* [FIX-5] mesh_received vs mqtt_published — phân biệt tầng để debug nhanh:
 * mesh_received = 0               → vấn đề BLE Mesh / NVS key binding
 * mesh_received > 0, published = 0 → vấn đề MQTT / WiFi / ThingsBoard */
static void bmt_log_mqtt_stats(void)
{
    UBaseType_t in_q = g_bmt_mqtt_queue ? uxQueueMessagesWaiting(g_bmt_mqtt_queue) : 0;
    printf("\n========== MQTT / MESH STATS ==========\n");
    printf("MQTT Connected  : %s\n", g_bmt_mqtt_connected ? "YES" : "NO");
    printf("Queue size      : %u / %d\n", (unsigned)in_q, BMT_MQTT_QUEUE_SIZE);
    printf("Mesh received   : %" PRIu32 "  <- BLE Mesh layer\n", g_bmt_mesh_received);
    printf("Total enqueued  : %" PRIu32 "\n", g_bmt_mqtt_enqueued);
    printf("Total published : %" PRIu32 "  <- MQTT layer\n", g_bmt_mqtt_published);
    printf("Total dropped   : %" PRIu32 "\n", g_bmt_mqtt_dropped);
    if (g_bmt_mqtt_enqueued > 0)
        printf("Drop rate       : %.2f%%\n", (float)g_bmt_mqtt_dropped*100.0f/g_bmt_mqtt_enqueued);
    printf("=======================================\n");
}

/* ============================================================================
 * WIFI
 * ============================================================================ */
static void bmt_wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { esp_wifi_connect(); }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
        xEventGroupClearBits(g_bmt_wifi_evgrp, BMT_WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(g_bmt_wifi_evgrp, BMT_WIFI_CONNECTED_BIT);
    }
}

static void bmt_wifi_init(void)
{
    g_bmt_wifi_evgrp = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &bmt_wifi_event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &bmt_wifi_event_handler, NULL, &got_ip));
    wifi_config_t wifi_cfg = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
    strncpy((char *)wifi_cfg.sta.ssid,     BMT_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, BMT_WIFI_PASS, sizeof(wifi_cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(g_bmt_wifi_evgrp, BMT_WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

/* ============================================================================
 * MQTT
 * ============================================================================ */
static void bmt_ota_distribute_task(void *arg);   /* forward declaration */

static void bmt_mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        g_bmt_mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT connected to ThingsBoard");
        esp_mqtt_client_subscribe(g_bmt_mqtt_client, "v1/devices/me/rpc/request/+", 1);
        ESP_LOGI(TAG, "Subscribed to RPC topic");
        break;

    case MQTT_EVENT_DISCONNECTED:
        g_bmt_mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_DATA: {
        esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
        if (!ev->topic || !ev->data) break;
        if (strncmp(ev->topic, "v1/devices/me/rpc/request/", 26) != 0) break;

        char payload[128] = {0};
        int plen = ev->data_len < (int)sizeof(payload)-1 ? ev->data_len : (int)sizeof(payload)-1;
        memcpy(payload, ev->data, plen);
        ESP_LOGI(TAG, "[RPC] Received: %s", payload);

        if (strstr(payload, "ota_scanner")) {
            if (g_bmt_ota_running) { ESP_LOGW(TAG, "[RPC] OTA already running"); }
            else {
                ESP_LOGI(TAG, "[RPC] OTA Scanner triggered");
                xTaskCreate(bmt_ota_distribute_task, "bmt_ota", 8192,
                            (void *)(uint32_t)BMT_NODE_TYPE_SCANNER, 4, NULL);
            }
        } else if (strstr(payload, "ota_relay")) {
            if (g_bmt_ota_running) { ESP_LOGW(TAG, "[RPC] OTA already running"); }
            else {
                ESP_LOGI(TAG, "[RPC] OTA Relay triggered");
                xTaskCreate(bmt_ota_distribute_task, "bmt_ota", 8192,
                            (void *)(uint32_t)BMT_NODE_TYPE_RELAY, 4, NULL);
            }
        } else if (strstr(payload, "ota_gateway")) {
            ESP_LOGI(TAG, "[RPC] OTA Gateway triggered");
            esp_http_client_config_t http_cfg = {
                .url = BMT_OTA_GATEWAY_URL, .timeout_ms = 15000,
                .crt_bundle_attach = esp_crt_bundle_attach,
            };
            esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
            esp_err_t err = esp_https_ota(&ota_cfg);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "[RPC] Gateway OTA OK — rebooting...");
                vTaskDelay(pdMS_TO_TICKS(500)); esp_restart();
            } else {
                ESP_LOGE(TAG, "[RPC] Gateway OTA FAILED: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG, "[RPC] Unknown method: %s", payload);
        }
        break;
    }
    default: break;
    }
}

static void bmt_mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri           = BMT_TB_HOST,
        .credentials.username         = BMT_TB_GATEWAY_TOKEN,
        .network.timeout_ms           = BMT_MQTT_PUBLISH_TIMEOUT_MS,
        .network.reconnect_timeout_ms = 5000,
    };
    g_bmt_mqtt_client = esp_mqtt_client_init(&cfg);
    if (!g_bmt_mqtt_client) { ESP_LOGE(TAG, "MQTT init failed"); return; }
    esp_mqtt_client_register_event(g_bmt_mqtt_client, ESP_EVENT_ANY_ID, bmt_mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_bmt_mqtt_client);
}

/* ============================================================================
 * THINGSBOARD
 * ============================================================================ */
static void bmt_tb_connect_device(const char *dev)
{
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char json[64]; snprintf(json, sizeof(json), "{\"device\":\"%s\"}", dev);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/connect", json, 0, 1, 0);
}

static void bmt_tb_disconnect_device(const char *dev)
{
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char json[64]; snprintf(json, sizeof(json), "{\"device\":\"%s\"}", dev);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/disconnect", json, 0, 1, 0);
}

static void bmt_tb_set_role(const char *dev, const char *role)
{
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char json[128]; snprintf(json, sizeof(json), "{\"%s\":{\"role\":\"%s\"}}", dev, role);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/attributes", json, 0, 1, 0);
}

static void bmt_mqtt_pub_gateway_online(void)
{
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/devices/me/telemetry",
                            "{\"status\":\"ONLINE\"}", 0, 1, 0);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/devices/me/attributes",
                            "{\"role\":\"" BMT_ROLE_GATEWAY "\"}", 0, 1, 0);
    ESP_LOGI(TAG, "TB Gateway ONLINE");
}

static void bmt_mqtt_pub_node_status(uint16_t addr, const char *role, bool online)
{
    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;
    char dev[32], json[192];
    snprintf(dev, sizeof(dev), BMT_DEV_NAME_NODE_FMT, addr);
    if (online) { bmt_tb_connect_device(dev); bmt_tb_set_role(dev, role); }
    snprintf(json, sizeof(json), "{\"%s\":[{\"status\":\"%s\",\"addr\":\"0x%04x\"}]}",
             dev, online ? "ONLINE" : "OFFLINE", addr);
    esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/telemetry", json, 0, 1, 0);
    if (!online) bmt_tb_disconnect_device(dev);
}

static void bmt_mqtt_pub_tag(bmt_tag_report_t *r)
{
    if (!r) return;
    bmt_gateway_tag_track_t *t = bmt_tag_track_get_or_add(r->tag_id, r->tag_type);
    if (!t) { ESP_LOGW(TAG, "Tag track table full"); return; }
    if (r->scanner_id < 1 || r->scanner_id > BMT_MAX_SCANNERS) return;

    int sidx = r->scanner_id - 1;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    t->rssi_by_scanner[sidx] = r->rssi;
    t->ts_by_scanner[sidx]   = now;
    t->valid_by_scanner[sidx]= true;
    t->last_any_report_ms    = now;

    uint8_t new_zone = bmt_zone_evaluate(t);
    if (new_zone != t->current_zone_id) {
        ESP_LOGI(TAG, "Tag 0x%04x ZONE: %s -> %s",
                 r->tag_id, bmt_zone_name(t->current_zone_id), bmt_zone_name(new_zone));
        t->current_zone_id = new_zone; t->last_zone_change_ms = now;
    }

    if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) return;

    char dev[32], json[384];
    snprintf(dev, sizeof(dev), BMT_DEV_NAME_TAG_FMT, r->tag_id);

    static uint16_t s_seen_tags[BMT_MAX_SEEN_TAGS] = {0};
    static int s_seen_count = 0;
    bool first_seen = true;
    for (int i = 0; i < s_seen_count; i++)
        if (s_seen_tags[i] == r->tag_id) { first_seen = false; break; }
    if (first_seen && s_seen_count < BMT_MAX_SEEN_TAGS) {
        s_seen_tags[s_seen_count++] = r->tag_id;
        bmt_tb_connect_device(dev); bmt_tb_set_role(dev, BMT_ROLE_TAG);
    }

    snprintf(json, sizeof(json),
             "{\"%s\":[{\"scanner\":\"0x%02x\",\"type\":\"%s\","
             "\"rssi\":%d,\"distance\":%.2f,\"loss\":%u,"
             "\"zone\":\"%s\",\"zone_id\":\"0x%02x\"}]}",
             dev, r->scanner_id, r->tag_type==0x01?"PERSON":"ASSET",
             r->rssi, r->distance_dm/10.0f, r->loss_pct,
             bmt_zone_name(t->current_zone_id), t->current_zone_id);

    int msg_id = esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/telemetry", json, 0, 0, 0);
    if (msg_id >= 0) {
        g_bmt_mqtt_published++;
        ESP_LOGI(TAG, "TB [%s] zone=%s rssi=%d", dev, bmt_zone_name(t->current_zone_id), r->rssi);
    }
}

/* ============================================================================
 * MQTT WORKER + ZONE TIMEOUT TASKS
 * ============================================================================ */
static void bmt_mqtt_worker_task(void *arg)
{
    (void)arg;
    bmt_tag_report_t report;
    ESP_LOGI(TAG, "MQTT worker task started");
    while (1) {
        if (xQueueReceive(g_bmt_mqtt_queue, &report, portMAX_DELAY) == pdTRUE)
            bmt_mqtt_pub_tag(&report);
    }
}

static void bmt_zone_timeout_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++) {
            bmt_gateway_tag_track_t *t = &g_bmt_tag_track[i];
            if (!t->active) continue;
            if ((now - t->last_any_report_ms) <= BMT_TAG_OUT_OF_RANGE_MS) continue;
            if (t->current_zone_id == BMT_ZONE_UNKNOWN) continue;
            ESP_LOGW(TAG, "Tag 0x%04x OUT OF RANGE", t->tag_id);
            t->current_zone_id = BMT_ZONE_UNKNOWN;
            t->last_zone_change_ms = now;
            for (int j = 0; j < BMT_MAX_SCANNERS; j++) t->valid_by_scanner[j] = false;
            if (!g_bmt_mqtt_connected || !g_bmt_mqtt_client) continue;
            char dev[32], json[160];
            snprintf(dev, sizeof(dev), BMT_DEV_NAME_TAG_FMT, t->tag_id);
            snprintf(json, sizeof(json), "{\"%s\":[{\"zone\":\"%s\",\"zone_id\":\"0x%02x\"}]}",
                     dev, bmt_zone_name(BMT_ZONE_UNKNOWN), BMT_ZONE_UNKNOWN);
            esp_mqtt_client_publish(g_bmt_mqtt_client, "v1/gateway/telemetry", json, 0, 0, 0);
        }
    }
}

/* ============================================================================
 * OTA — SPIFFS INIT
 * ============================================================================ */
static esp_err_t bmt_spiffs_init(void)
{
    esp_vfs_spiffs_conf_t cfg = {
        .base_path = "/spiffs", .partition_label = "storage",
        .max_files = 4, .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret)); return ret; }
    size_t total, used;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %d/%d bytes used", (int)used, (int)total);
    return ESP_OK;
}

/* ============================================================================
 * OTA — HTTP DOWNLOAD
 * ============================================================================ */
__attribute__((unused)) static esp_err_t bmt_ota_http_download(const char *url, const char *save_path,
                                        uint32_t *out_size, uint32_t *out_crc)
{
    ESP_LOGI(TAG, "[OTA] Downloading: %s", url);
    remove(save_path);
    FILE *f = fopen(save_path, "wb");
    if (!f) { ESP_LOGE(TAG, "[OTA] Cannot create %s", save_path); return ESP_FAIL; }

    esp_http_client_config_t cfg = { .url=url, .timeout_ms=15000, .buffer_size=512 };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA] HTTP open failed: %s", esp_err_to_name(err));
        fclose(f); esp_http_client_cleanup(client); return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "[OTA] Content-Length: %d bytes", content_len);

    uint8_t buf[512]; uint32_t total_read=0, running_crc=0; int read_len;
    while ((read_len = esp_http_client_read(client, (char *)buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, read_len, f);
        running_crc = esp_rom_crc32_le(running_crc, buf, read_len);
        total_read += read_len;
        if (total_read % (50*1024) == 0)
            ESP_LOGI(TAG, "[OTA] Downloaded %lu/%d (%.1f%%)", total_read, content_len,
                     content_len>0?(float)total_read*100/content_len:0.0f);
    }
    fclose(f); esp_http_client_close(client); esp_http_client_cleanup(client);
    if (total_read == 0) { ESP_LOGE(TAG, "[OTA] Download failed (0 bytes)"); return ESP_FAIL; }
    *out_size = total_read; *out_crc = running_crc;
    ESP_LOGI(TAG, "[OTA] Download complete: %lu bytes, CRC=0x%08lx", total_read, running_crc);
    return ESP_OK;
}

/* ============================================================================
 * OTA — BLE BEACON TRIGGER  [v4.8]
 * Gateway advertise beacon đặc biệt → Scanner GAP callback detect → WiFi OTA
 * Bypass hoàn toàn mesh bearer (scanner GAP scan 100% → luôn nhận BLE ADV)
 * ============================================================================ */
#define BMT_OTA_BEACON_DURATION_MS  15000  /* 15s: 10 GAP scan windows → chắc chắn nhận
                                             * 6s (cũ): chỉ 4 windows, scanner xa có thể miss */

/* Beacon format: Flags + Manufacturer Specific (CID Espressif + "BMT" + 0xFA + target) */
static uint8_t bmt_ota_adv_raw[12] = {
    0x02, 0x01, 0x06,        /* Flags: LE General Discoverable */
    0x08, 0xFF,              /* Manufacturer Specific, length=8 */
    0xE5, 0x02,              /* CID Espressif (little-endian) */
    'B',  'M',  'T',         /* Magic "BMT" */
    0xFA,                    /* Command: OTA trigger */
    0x01,                    /* target_type — fill trước khi gửi */
};

static esp_ble_adv_params_t bmt_ota_adv_params = {
    .adv_int_min       = 0x0020,  /* 20ms interval — đủ nhanh để scanner bắt */
    .adv_int_max       = 0x0040,
    .adv_type          = ADV_TYPE_NONCONN_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void bmt_ota_beacon_send(uint8_t target_type)
{
    bmt_ota_adv_raw[11] = target_type;  /* set target */
    esp_ble_gap_config_adv_data_raw(bmt_ota_adv_raw, sizeof(bmt_ota_adv_raw));
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_ble_gap_start_advertising(&bmt_ota_adv_params);
    printf("[OTA] BLE beacon broadcasting (target=0x%02x, %ds)...\n",
           target_type, BMT_OTA_BEACON_DURATION_MS/1000);
    vTaskDelay(pdMS_TO_TICKS(BMT_OTA_BEACON_DURATION_MS));
    esp_ble_gap_stop_advertising();
    printf("[OTA] BLE beacon stopped\n");
}

/* ============================================================================
 * OTA — MESH SEND HELPER (gửi OTA_TRIGGER unicast đến từng node)
 * ============================================================================ */
static esp_err_t bmt_mesh_send_trigger(uint16_t dst, uint32_t opcode,
                                       const void *data, size_t len)
{
    bmt_vnd_models[0].pub->publish_addr = dst;
    bmt_vnd_models[0].pub->app_idx      = g_bmt_app_key_idx;
    bmt_vnd_models[0].pub->ttl          = 7;
    esp_err_t err = esp_ble_mesh_model_publish(
        &bmt_vnd_models[0], opcode, len, (uint8_t *)data, ROLE_PROVISIONER);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "[OTA] trigger→0x%04x FAILED: %s", dst, esp_err_to_name(err));
    return err;
}

/* ============================================================================
 * OTA — TRIGGER TASK  [v4.8]
 * Gửi OTA_TRIGGER LẦN LƯỢT từng node, cách nhau BMT_OTA_NODE_GAP_MS
 * để mỗi node OTA xong (bật WiFi → download → reboot) mới sang node kế.
 * Firmware đi qua WiFi của từng node, KHÔNG qua mesh → reliable.
 * ============================================================================ */
#define BMT_OTA_NODE_GAP_MS   90000   /* 90s mỗi node: đủ download ~840KB + reboot */

static void bmt_ota_distribute_task(void *arg)
{
    uint8_t    target_type = (uint8_t)(uint32_t)arg;
    const char *type_str   = (target_type == BMT_NODE_TYPE_SCANNER) ? "SCANNER" : "RELAY";

    g_bmt_ota_running = true;
    printf("\n[OTA] ===== Trigger WiFi OTA for all %s nodes =====\n", type_str);

    /* Đếm số node cần trigger */
    int node_count = 0;
    for (int n = 0; n < BMT_MAX_NODES; n++) {
        if (!g_bmt_nodes[n].used || !g_bmt_nodes[n].config_done) continue;
        if (target_type == BMT_NODE_TYPE_SCANNER && !g_bmt_nodes[n].is_scan) continue;
        if (target_type == BMT_NODE_TYPE_RELAY   && !g_bmt_nodes[n].is_relay) continue;
        node_count++;
    }
    if (node_count == 0) {
        printf("[OTA] No configured %s nodes found\n", type_str);
        g_bmt_ota_running = false; vTaskDelete(NULL); return;
    }
    printf("[OTA] Found %d %s node(s)\n", node_count, type_str);

    /* ===== SCANNER: 1 beacon broadcast → tất cả scanner OTA cùng lúc ===== */
    if (target_type == BMT_NODE_TYPE_SCANNER) {
        printf("[OTA] Broadcasting BLE beacon (%ds) → all %d scanners OTA simultaneously\n",
               BMT_OTA_BEACON_DURATION_MS/1000, node_count);
        bmt_ota_beacon_send(target_type);
        printf("[OTA] Waiting %ds for all scanners to download + reboot...\n",
               BMT_OTA_NODE_GAP_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(BMT_OTA_NODE_GAP_MS));
        printf("[OTA] ===== Scanner OTA complete =====\n");
        g_bmt_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* ===== RELAY: mesh trigger lần lượt từng node ===== */
    uint8_t trig = target_type;  /* payload 1 byte cho mesh trigger */
    int idx = 0;
    for (int n = 0; n < BMT_MAX_NODES; n++) {
        if (!g_bmt_nodes[n].used || !g_bmt_nodes[n].config_done) continue;
        if (!g_bmt_nodes[n].is_relay) continue;

        uint16_t addr = g_bmt_nodes[n].addr;
        idx++;
        printf("\n[OTA] ── Relay %d/%d: 0x%04x (%s) ──\n",
               idx, node_count, addr, g_bmt_nodes[n].name);

        for (int t = 0; t < 5; t++) {
            esp_err_t e = bmt_mesh_send_trigger(addr, BMT_OP_VND_OTA_TRIGGER,
                                                &trig, sizeof(trig));
            printf("[OTA] TRIGGER→0x%04x [%d/5]: %s\n", addr, t+1,
                   e == ESP_OK ? "sent" : esp_err_to_name(e));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        printf("[OTA] Relay 0x%04x triggered — waiting %ds...\n",
               addr, BMT_OTA_NODE_GAP_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(BMT_OTA_NODE_GAP_MS));
    }

    printf("\n[OTA] ===== All %s nodes triggered! =====\n", type_str);
    g_bmt_ota_running = false;
    vTaskDelete(NULL);
}

/* ============================================================================
 * UART
 * ============================================================================ */
static void bmt_uart_init(void)
{
    uart_driver_delete(BMT_UART_NUM);
    const uart_config_t cfg = {
        .baud_rate  = BMT_UART_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BMT_UART_NUM, BMT_UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BMT_UART_NUM, &cfg));
}

static void bmt_uart_cmd_task(void *arg)
{
    uint8_t ch; (void)arg;
    bmt_print_status();

    while (1) {
        int len = uart_read_bytes(BMT_UART_NUM, &ch, 1, pdMS_TO_TICKS(200));
        if (len <= 0 || ch == '\r' || ch == '\n') continue;

        switch (ch) {
        case '1': bmt_log_node_table();  break;
        case '2': bmt_log_tag_track();   break;
        case '3': bmt_log_mqtt_stats();  break;

        case 'g':
            printf("\n[OTA] Updating GATEWAY: %s\n", BMT_OTA_GATEWAY_URL);
            {
                esp_http_client_config_t http_cfg = {
                    .url = BMT_OTA_GATEWAY_URL, .timeout_ms = 15000,
                    .crt_bundle_attach = esp_crt_bundle_attach,
                };
                esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
                esp_err_t err = esp_https_ota(&ota_cfg);
                if (err == ESP_OK) {
                    printf("[OTA] Gateway OK — rebooting...\n");
                    vTaskDelay(pdMS_TO_TICKS(1000)); esp_restart();
                } else {
                    printf("[OTA] Gateway FAILED: %s\n", esp_err_to_name(err));
                }
            }
            break;

        case 's':
            printf("\n[UART] Starting MANUAL SCAN...\n"); bmt_do_scan(); break;
        case 'p':
            if (g_bmt_prov_mode != BMT_PROV_MODE_MANUAL)
                printf("\n[UART] Not in MANUAL mode. Press m first.\n");
            else bmt_provision_scan_list();
            break;
        case 'a':
            g_bmt_prov_mode = BMT_PROV_MODE_AUTO; g_bmt_scan_count = 0;
            memset(g_bmt_scan_list, 0, sizeof(g_bmt_scan_list));
            printf("\n[UART] AUTO provision mode\n"); break;
        case 'm':
            g_bmt_prov_mode = BMT_PROV_MODE_MANUAL;
            printf("\n[UART] MANUAL provision mode\n"); break;
        case '4': bmt_print_status(); break;

        case 'u':
            if (g_bmt_ota_running) { printf("\n[OTA] Already running!\n"); break; }
            printf("\n[OTA] Select target:\n      s = Scanner\n      r = Relay\n      (5s...)\n");
            { uint8_t sel=0;
              uart_read_bytes(BMT_UART_NUM, &sel, 1, pdMS_TO_TICKS(5000));
              uint8_t ttype = (sel=='r'||sel=='R') ? BMT_NODE_TYPE_RELAY : BMT_NODE_TYPE_SCANNER;
              printf("[OTA] Starting OTA for %s...\n", ttype==BMT_NODE_TYPE_SCANNER?"SCANNER":"RELAY");
              xTaskCreate(bmt_ota_distribute_task, "bmt_ota", 8192, (void *)(uint32_t)ttype, 4, NULL);
            }
            break;

        case '0':
            printf("\n[UART] SOFT RESET — restart, giu toan bo mesh NVS...\n");
            memset(g_bmt_tag_track, 0, sizeof(g_bmt_tag_track));
            vTaskDelay(pdMS_TO_TICKS(300)); esp_restart(); break;

        case '9':
            printf("\n[UART] FULL RESET — xoa tat ca, re-provision sau reboot...\n");
            {
                const esp_ble_mesh_node_t **entry = esp_ble_mesh_provisioner_get_node_table_entry();
                if (entry) {
                    int erased=0;
                    for (int i=0; i<CONFIG_BLE_MESH_MAX_PROV_NODES; i++)
                        if (entry[i]) { esp_ble_mesh_provisioner_delete_node_with_uuid(entry[i]->dev_uuid); erased++; }
                    printf("[UART] Erased %d node(s)\n", erased);
                }
            }
            bmt_nvs_clear_nodes();
            memset(g_bmt_nodes, 0, sizeof(g_bmt_nodes));
            memset(g_bmt_tag_track, 0, sizeof(g_bmt_tag_track));
            vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); break;

        default:
            printf("\n[UART] Unknown: %c\n", ch); bmt_print_status(); break;
        }
    }
}

/* ============================================================================
 * SCAN NODE CONFIG TASK
 * Gửi APP_KEY_ADD + MODEL_APP_BIND cho scanner sau khi provision.
 * ============================================================================ */
static void bmt_scan_config_task(void *arg)
{
    uint16_t addr = (uint16_t)(uint32_t)arg;
    ESP_LOGI(TAG, "[SCN_CFG] Configuring scan node 0x%04x...", addr);
    vTaskDelay(pdMS_TO_TICKS(2000));

    { esp_ble_mesh_client_common_param_t c={0}; esp_ble_mesh_cfg_client_set_state_t s={0};
      c.opcode=ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD; c.model=&bmt_root_models[1];
      c.ctx.net_idx=g_bmt_net_key_idx; c.ctx.app_idx=0xFFFF; c.ctx.addr=addr;
      c.ctx.send_ttl=7; c.msg_timeout=8000;
      s.app_key_add.net_idx=g_bmt_net_key_idx; s.app_key_add.app_idx=g_bmt_app_key_idx;
      memcpy(s.app_key_add.app_key, g_bmt_app_key, 16);
      esp_err_t e = esp_ble_mesh_config_client_set_state(&c, &s);
      ESP_LOGI(TAG, "[SCN_CFG] Step1 APP_KEY_ADD to 0x%04x: %s", addr, e==ESP_OK?"OK":esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    { esp_ble_mesh_client_common_param_t c={0}; esp_ble_mesh_cfg_client_set_state_t s={0};
      c.opcode=ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND; c.model=&bmt_root_models[1];
      c.ctx.net_idx=g_bmt_net_key_idx; c.ctx.app_idx=0xFFFF; c.ctx.addr=addr;
      c.ctx.send_ttl=7; c.msg_timeout=5000;
      s.model_app_bind.element_addr=addr; s.model_app_bind.model_app_idx=g_bmt_app_key_idx;
      s.model_app_bind.model_id=BMT_VND_MODEL_ID; s.model_app_bind.company_id=BMT_CID_ESP;
      esp_err_t e = esp_ble_mesh_config_client_set_state(&c, &s);
      ESP_LOGI(TAG, "[SCN_CFG] Step2 MODEL_APP_BIND to 0x%04x: %s", addr, e==ESP_OK?"OK":esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    int idx = bmt_find_node_index(addr);
    if (idx >= 0) { g_bmt_nodes[idx].config_done = true; bmt_nvs_save_nodes(); }
    ESP_LOGI(TAG, "[SCN_CFG] Scan node 0x%04x fully configured!", addr);
    bmt_log_node_table();
    vTaskDelete(NULL);
}

/* ============================================================================
 * RELAY NODE CONFIG TASK  [FIX-6 — MỚI]
 *
 * Vấn đề: v4.5 set config_done=true ngay khi provision relay xong mà không
 * gửi APP_KEY_ADD + MODEL_APP_BIND → relay không có AppKey bound cho vendor model
 * → Transport Layer không decrypt được RESET_CMD → watchdog không reset được relay
 * → Sau gateway full reset, relay vẫn giữ NVS cũ → mesh conflict tiếp tục.
 *
 * Lưu ý quan trọng về BLE Mesh layer:
 *   Relay FORWARD traffic ở Network Layer (dùng NetKey, không cần AppKey) → OK
 *   Relay TỰ XỬ LÝ message ở Transport/Access Layer (cần AppKey) → cần bind này
 * ============================================================================ */
static void bmt_relay_config_task(void *arg)
{
    uint16_t addr = (uint16_t)(uint32_t)arg;
    ESP_LOGI(TAG, "[RLY_CFG] Configuring relay node 0x%04x...", addr);
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Step 1: Gửi AppKey cho relay */
    { esp_ble_mesh_client_common_param_t c={0}; esp_ble_mesh_cfg_client_set_state_t s={0};
      c.opcode=ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD; c.model=&bmt_root_models[1];
      c.ctx.net_idx=g_bmt_net_key_idx; c.ctx.app_idx=0xFFFF; c.ctx.addr=addr;
      c.ctx.send_ttl=7; c.msg_timeout=8000;
      s.app_key_add.net_idx=g_bmt_net_key_idx; s.app_key_add.app_idx=g_bmt_app_key_idx;
      memcpy(s.app_key_add.app_key, g_bmt_app_key, 16);
      esp_err_t e = esp_ble_mesh_config_client_set_state(&c, &s);
      ESP_LOGI(TAG, "[RLY_CFG] Step1 APP_KEY_ADD to 0x%04x: %s", addr, e==ESP_OK?"OK":esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Step 2: Bind AppKey vào vendor model của relay
     * Sau bước này relay mới decrypt được RESET_CMD broadcast từ watchdog */
    { esp_ble_mesh_client_common_param_t c={0}; esp_ble_mesh_cfg_client_set_state_t s={0};
      c.opcode=ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND; c.model=&bmt_root_models[1];
      c.ctx.net_idx=g_bmt_net_key_idx; c.ctx.app_idx=0xFFFF; c.ctx.addr=addr;
      c.ctx.send_ttl=7; c.msg_timeout=5000;
      s.model_app_bind.element_addr=addr; s.model_app_bind.model_app_idx=g_bmt_app_key_idx;
      s.model_app_bind.model_id=BMT_VND_MODEL_ID; s.model_app_bind.company_id=BMT_CID_ESP;
      esp_err_t e = esp_ble_mesh_config_client_set_state(&c, &s);
      ESP_LOGI(TAG, "[RLY_CFG] Step2 MODEL_APP_BIND to 0x%04x: %s", addr, e==ESP_OK?"OK":esp_err_to_name(e));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    int idx = bmt_find_node_index(addr);
    if (idx >= 0) { g_bmt_nodes[idx].config_done = true; bmt_nvs_save_nodes(); }
    ESP_LOGI(TAG, "[RLY_CFG] Relay 0x%04x fully configured — RESET_CMD enabled!", addr);
    bmt_log_node_table();
    vTaskDelete(NULL);
}

/* ============================================================================
 * MESH CALLBACKS
 * ============================================================================ */
static void bmt_mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner registered"); break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner scan enabled"); break;

    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT: {
        const uint8_t *uuid=param->provisioner_recv_unprov_adv_pkt.dev_uuid;
        const uint8_t *mac =param->provisioner_recv_unprov_adv_pkt.addr;
        uint8_t  addr_type = param->provisioner_recv_unprov_adv_pkt.addr_type;
        uint16_t oob_info  = param->provisioner_recv_unprov_adv_pkt.oob_info;
        bmt_mac_cache_store(uuid, mac);

        if (g_bmt_prov_mode == BMT_PROV_MODE_MANUAL) {
            if (!g_bmt_scanning) break;
            for (int i=0; i<g_bmt_scan_count; i++)
                if (memcmp(g_bmt_scan_list[i].uuid,uuid,16)==0) goto scan_dup;
            if (g_bmt_scan_count < BMT_MAX_SCAN_LIST) {
                memcpy(g_bmt_scan_list[g_bmt_scan_count].uuid, uuid, 16);
                memcpy(g_bmt_scan_list[g_bmt_scan_count].addr, mac,  6);
                g_bmt_scan_list[g_bmt_scan_count].addr_type=addr_type;
                g_bmt_scan_list[g_bmt_scan_count].oob_info=oob_info;
                g_bmt_scan_list[g_bmt_scan_count].used=true;
                g_bmt_scan_count++;
                printf("[SCAN] [%d] %-7s MAC:", g_bmt_scan_count-1, bmt_uuid_type_str(uuid));
                for (int b=0; b<6; b++) printf("%02X%s", mac[b], b<5?":":"");
                printf("\n");
            }
            scan_dup: break;
        }

        if (bmt_uuid_already_provisioned(uuid)) break;
        ESP_LOGI(TAG, "Found unprovisioned [%s]", bmt_uuid_type_str(uuid));
        esp_ble_mesh_unprov_dev_add_t dev={0};
        memcpy(dev.uuid,uuid,16); memcpy(dev.addr,mac,6);
        dev.addr_type=addr_type; dev.oob_info=oob_info; dev.bearer=ESP_BLE_MESH_PROV_ADV;
        esp_ble_mesh_provisioner_add_unprov_dev(&dev, ADD_DEV_FLUSHABLE_DEV_FLAG|ADD_DEV_START_PROV_NOW_FLAG);
        break;
    }

    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT: {
        uint16_t addr=param->provisioner_prov_complete.unicast_addr;
        const uint8_t *uuid=param->provisioner_prov_complete.device_uuid;
        ESP_LOGI(TAG, "Provision complete addr=0x%04x type=%s", addr, bmt_uuid_type_str(uuid));
        uint8_t mac[6]={0}; bmt_mac_cache_get(uuid, mac);
        int idx = bmt_add_node(addr, uuid, mac, NULL);
        if (idx < 0) { ESP_LOGW(TAG, "Node table full"); break; }

        /* [FIX-6] Relay: launch bmt_relay_config_task giống scanner.
         * TRƯỚC (v4.5): config_done=true ngay, không có AppKey → relay không nhận
         * được RESET_CMD từ watchdog vì Transport Layer không decrypt được.
         * SAU  (v4.6): config_done=false, launch task → APP_KEY_ADD + MODEL_APP_BIND
         * → relay có AppKey → decrypt RESET_CMD OK → watchdog cycle hoạt động. */
        if (bmt_uuid_is_relay(uuid)) {
            g_bmt_nodes[idx].is_relay    = true;
            g_bmt_nodes[idx].is_scan     = false;
            g_bmt_nodes[idx].config_done = false;   /* chờ relay_config_task hoàn tất */
            snprintf(g_bmt_nodes[idx].name, sizeof(g_bmt_nodes[idx].name), "Relay_0x%04x", addr);
            ESP_LOGI(TAG, "Node 0x%04x = RELAY, launching config task...", addr);
            bmt_nvs_save_nodes(); bmt_log_node_table();
            xTaskCreate(bmt_relay_config_task, "relay_cfg", 3072, (void *)(uint32_t)addr, 5, NULL);
            break;
        }

        if (bmt_uuid_is_scan(uuid)) {
            g_bmt_nodes[idx].is_scan     = true;
            g_bmt_nodes[idx].is_relay    = false;
            g_bmt_nodes[idx].config_done = false;
            snprintf(g_bmt_nodes[idx].name, sizeof(g_bmt_nodes[idx].name), "Scan_0x%04x", addr);
            ESP_LOGI(TAG, "Node 0x%04x = SCAN, launching config task...", addr);
            bmt_nvs_save_nodes(); bmt_log_node_table();
            xTaskCreate(bmt_scan_config_task, "scan_cfg", 3072, (void *)(uint32_t)addr, 5, NULL);
            break;
        }

        ESP_LOGW(TAG, "Unknown node type"); bmt_nvs_save_nodes(); bmt_log_node_table();
        break;
    }
    default: break;
    }
}

static void bmt_mesh_cfg_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                   esp_ble_mesh_cfg_client_cb_param_t *param)
{
    if (!param || !param->params) return;
    uint16_t addr=param->params->ctx.addr; int idx=bmt_find_node_index(addr);
    if (event == ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT) {
        uint32_t op=param->params->opcode;
        if (op == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD)
            ESP_LOGI(TAG, "[CFG] APP_KEY_ADD ACK from 0x%04x", addr);
        if (op == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            ESP_LOGI(TAG, "[CFG] MODEL_APP_BIND ACK from 0x%04x", addr);
            /* [FIX-6] Báo TB ONLINE cho cả scanner lẫn relay khi bind xong */
            if (idx>=0 && g_bmt_nodes[idx].is_scan)
                bmt_mqtt_pub_node_status(addr, BMT_ROLE_SCAN,  true);
            if (idx>=0 && g_bmt_nodes[idx].is_relay)
                bmt_mqtt_pub_node_status(addr, BMT_ROLE_RELAY, true);
        }
    }
    if (event == ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT)
        ESP_LOGW(TAG, "[CFG] TIMEOUT opcode=0x%04" PRIx32 " addr=0x%04x", param->params->opcode, addr);
    if (event == ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT) {
        if (idx>=0 && g_bmt_nodes[idx].is_relay) {
            g_bmt_nodes[idx].last_seen_ms = xTaskGetTickCount()*portTICK_PERIOD_MS;
            if (!g_bmt_nodes[idx].online) {
                g_bmt_nodes[idx].online=true;
                ESP_LOGI(TAG, "Relay 0x%04x ONLINE (ping)", addr);
                bmt_mqtt_pub_node_status(addr, BMT_ROLE_RELAY, true);
            }
        }
    }
}

static void bmt_mesh_cfg_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param)
{ (void)param; ESP_LOGI(TAG, "Config server event: %d", event); }

/* [FIX-1] Increment g_bmt_mesh_received khi nhận TAG_STATUS từ scanner.
 * Counter ở tầng BLE Mesh, watchdog dùng để phân biệt:
 * "mesh bị chết" vs "chỉ MQTT bị chết" */
static void bmt_mesh_vnd_client_cb(esp_ble_mesh_model_cb_event_t event,
                                   esp_ble_mesh_model_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_MODEL_OPERATION_EVT || !param) return;
    uint32_t opcode=param->model_operation.opcode;
    uint16_t src   =param->model_operation.ctx->addr;
    uint8_t *data  =param->model_operation.msg;
    uint16_t len   =param->model_operation.length;

    if (opcode == BMT_OP_VND_TAG_STATUS) {
        if (len < sizeof(bmt_tag_report_t)) { ESP_LOGW(TAG, "[VND] TAG_STATUS too short"); return; }
        bmt_tag_report_t report; memcpy(&report, data, sizeof(report));

        /* [FIX-1] Tăng counter ngay tại tầng mesh trước khi enqueue MQTT */
        g_bmt_mesh_received++;

        ESP_LOGI(TAG, "[VND] src=0x%04x scanner=0x%02x tag=0x%04x rssi=%d (mesh_recv=%" PRIu32 ")",
                 src, report.scanner_id, report.tag_id, report.rssi, g_bmt_mesh_received);

        if (g_bmt_mqtt_queue) {
            if (xQueueSend(g_bmt_mqtt_queue, &report, 0)==pdTRUE) g_bmt_mqtt_enqueued++;
            else { g_bmt_mqtt_dropped++; ESP_LOGW(TAG, "MQTT queue FULL (dropped: %" PRIu32 ")", g_bmt_mqtt_dropped); }
        }
        return;
    }
    if (opcode == BMT_OP_VND_OTA_ACK) {
        if (len < sizeof(bmt_ota_ack_t)) return;
        bmt_ota_ack_t ack; memcpy(&ack, data, sizeof(ack));
        ESP_LOGD(TAG, "[OTA] ACK chunk=%u status=%u from 0x%04x", ack.chunk_idx, ack.status, src);
        if (ack.status == 0) xEventGroupSetBits(g_bmt_ota_evgrp, BMT_OTA_ACK_BIT);
        else ESP_LOGW(TAG, "[OTA] ACK FAIL chunk=%u from 0x%04x", ack.chunk_idx, src);
        return;
    }
}

/* ============================================================================
 * DATA WATCHDOG TASK  [v4.6]
 * ============================================================================ */
static void bmt_data_watchdog_task(void *arg)
{
    (void)arg;

    bool has_scan = false;
    for (int i = 0; i < BMT_MAX_NODES; i++) {
        if (g_bmt_nodes[i].used && g_bmt_nodes[i].is_scan && g_bmt_nodes[i].config_done) {
            has_scan = true; break;
        }
    }
    if (!has_scan) {
        ESP_LOGI(TAG, "[WDG] Fresh boot — no configured nodes, watchdog exit");
        vTaskDelete(NULL); return;
    }

    uint32_t snap = g_bmt_mesh_received;
    ESP_LOGI(TAG, "[WDG] NVS nodes detected — watching %ds... (snap=%" PRIu32 ")",
             BMT_WDG_TIMEOUT_MS/1000, snap);

    vTaskDelay(pdMS_TO_TICKS(BMT_WDG_TIMEOUT_MS));

    /* [FIX-1] Check mesh layer */
    if (g_bmt_mesh_received > snap) {
        ESP_LOGI(TAG, "[WDG] Mesh OK (%" PRIu32 " -> %" PRIu32 ") — watchdog done",
                 snap, g_bmt_mesh_received);
        vTaskDelete(NULL); return;
    }

    ESP_LOGW(TAG, "[WDG] ============================================");
    ESP_LOGW(TAG, "[WDG] No mesh data in %ds — starting reset cycle", BMT_WDG_TIMEOUT_MS/1000);
    ESP_LOGW(TAG, "[WDG] ============================================");

    /* [FIX-2] Retry RESET_CMD + [FIX-4] skip nếu OTA đang chạy */
    uint8_t dummy = 0; int sent_ok = 0;
    for (int i = 0; i < BMT_WDG_RESET_TRIES; i++) {
        if (g_bmt_ota_running) {
            ESP_LOGW(TAG, "[WDG] RESET_CMD [%d/%d]: skipped (OTA running)", i+1, BMT_WDG_RESET_TRIES);
        } else {
            bmt_vnd_models[0].pub->publish_addr = 0xFFFF;
            bmt_vnd_models[0].pub->app_idx      = g_bmt_app_key_idx;
            bmt_vnd_models[0].pub->ttl          = 7;
            esp_err_t e = esp_ble_mesh_model_publish(
                &bmt_vnd_models[0], BMT_OP_VND_RESET_CMD, sizeof(dummy), &dummy, ROLE_PROVISIONER);
            if (e == ESP_OK) sent_ok++;
            ESP_LOGW(TAG, "[WDG] RESET_CMD [%d/%d]: %s (ok=%d)",
                     i+1, BMT_WDG_RESET_TRIES, e==ESP_OK?"sent":esp_err_to_name(e), sent_ok);
        }
        vTaskDelay(pdMS_TO_TICKS(BMT_WDG_RESET_GAP_MS));
    }

    /* [FIX-3] Chờ nodes hoàn tất: local_reset + NVS erase + reboot + init */
    ESP_LOGW(TAG, "[WDG] Waiting %ds for nodes to complete reset + reboot...", BMT_WDG_NODE_WAIT_MS/1000);
    vTaskDelay(pdMS_TO_TICKS(BMT_WDG_NODE_WAIT_MS));

    ESP_LOGW(TAG, "[WDG] Gateway FULL RESET — wiping all mesh state...");
    const esp_ble_mesh_node_t **entry = esp_ble_mesh_provisioner_get_node_table_entry();
    if (entry) {
        int n = 0;
        for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++)
            if (entry[i]) { esp_ble_mesh_provisioner_delete_node_with_uuid(entry[i]->dev_uuid); n++; }
        ESP_LOGW(TAG, "[WDG] Erased %d node(s) from provisioner table", n);
    }
    bmt_nvs_clear_nodes();
    memset(g_bmt_nodes, 0, sizeof(g_bmt_nodes));
    memset(g_bmt_tag_track, 0, sizeof(g_bmt_tag_track));
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGW(TAG, "[WDG] Rebooting — auto provision on next boot...");
    esp_restart();
}

/* ============================================================================
 * RELAY PING TASK
 * ============================================================================ */
static void bmt_relay_ping_task(void *arg)
{
    (void)arg; vTaskDelay(pdMS_TO_TICKS(30000));
    while (1) {
        uint32_t now = xTaskGetTickCount()*portTICK_PERIOD_MS;
        for (int i=0; i<BMT_MAX_NODES; i++) {
            if (!g_bmt_nodes[i].used || !g_bmt_nodes[i].is_relay) continue;
            esp_ble_mesh_client_common_param_t common={0};
            esp_ble_mesh_cfg_client_get_state_t get={0};
            common.opcode=ESP_BLE_MESH_MODEL_OP_DEFAULT_TTL_GET;
            common.model=&bmt_root_models[1];
            common.ctx.net_idx=g_bmt_net_key_idx; common.ctx.app_idx=0xFFFF;
            common.ctx.addr=g_bmt_nodes[i].addr; common.ctx.send_ttl=7; common.msg_timeout=5000;
            esp_ble_mesh_config_client_get_state(&common, &get);
            if (g_bmt_nodes[i].last_seen_ms>0 &&
                (now-g_bmt_nodes[i].last_seen_ms)>BMT_RELAY_OFFLINE_TIMEOUT_MS) {
                if (g_bmt_nodes[i].online) {
                    g_bmt_nodes[i].online=false;
                    ESP_LOGW(TAG, "Relay 0x%04X OFFLINE", g_bmt_nodes[i].addr);
                    bmt_mqtt_pub_node_status(g_bmt_nodes[i].addr, BMT_ROLE_RELAY, false);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(BMT_RELAY_PING_INTERVAL_MS));
    }
}

static void bmt_wdt_feed_task(void *arg)
{
    (void)arg; esp_task_wdt_add(NULL);
    while (1) { esp_task_wdt_reset(); vTaskDelay(pdMS_TO_TICKS(10000)); }
}

/* ============================================================================
 * MESH INIT
 * ============================================================================ */
static esp_err_t bmt_ble_mesh_init_gateway(void)
{
    esp_err_t err;
    esp_ble_mesh_register_prov_callback(bmt_mesh_prov_cb);
    esp_ble_mesh_register_config_client_callback(bmt_mesh_cfg_client_cb);
    esp_ble_mesh_register_config_server_callback(bmt_mesh_cfg_server_cb);
    esp_ble_mesh_register_custom_model_callback(bmt_mesh_vnd_client_cb);

    err = esp_ble_mesh_init(&bmt_provision, &bmt_composition); if (err!=ESP_OK) return err;
    err = esp_ble_mesh_provisioner_set_dev_uuid_match(NULL,0,0,false); if (err!=ESP_OK) return err;
    err = esp_ble_mesh_provisioner_prov_enable(ESP_BLE_MESH_PROV_ADV|ESP_BLE_MESH_PROV_GATT);
    if (err!=ESP_OK) return err;

    const uint8_t *exist_net = esp_ble_mesh_provisioner_get_local_net_key(g_bmt_net_key_idx);
    if (!exist_net) {
        err = esp_ble_mesh_provisioner_add_local_net_key(g_bmt_net_key, g_bmt_net_key_idx);
        if (err!=ESP_OK) return err;
        ESP_LOGI(TAG, "NetKey added");
    } else {
        ESP_LOGI(TAG, "NetKey already exists (restored from NVS)");
    }

    const uint8_t *exist_app = esp_ble_mesh_provisioner_get_local_app_key(g_bmt_net_key_idx, g_bmt_app_key_idx);
    if (!exist_app) {
        err = esp_ble_mesh_provisioner_add_local_app_key(g_bmt_app_key, g_bmt_net_key_idx, g_bmt_app_key_idx);
        if (err!=ESP_OK) return err;
        ESP_LOGI(TAG, "AppKey added");
    } else {
        ESP_LOGI(TAG, "AppKey already exists (restored from NVS)");
    }

    err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(
        0x0001, g_bmt_app_key_idx, BMT_VND_MODEL_ID, BMT_CID_ESP);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "AppKey bound to local vendor model: 0x%04x", g_bmt_app_key_idx);
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "AppKey already bound (restored from NVS) — OK");
    } else {
        ESP_LOGE(TAG, "bind_app_key_to_local_model FAILED: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Gateway init OK");
    return ESP_OK;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */
void app_main(void)
{
    esp_err_t err;
    ESP_LOGI(TAG, "=== BMT Gateway v4.8 Starting ===");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    bmt_nvs_load_nodes();

    g_bmt_mqtt_queue = xQueueCreate(BMT_MQTT_QUEUE_SIZE, sizeof(bmt_tag_report_t));
    if (!g_bmt_mqtt_queue) { ESP_LOGE(TAG, "MQTT queue create failed"); return; }

    g_bmt_ota_evgrp = xEventGroupCreate();
    if (!g_bmt_ota_evgrp) { ESP_LOGE(TAG, "OTA event group create failed"); return; }

    bmt_spiffs_init();
    bmt_wifi_init();
    bmt_mqtt_init();
    vTaskDelay(pdMS_TO_TICKS(2000));

    err = bluetooth_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "BT init failed"); return; }

    err = bmt_ble_mesh_init_gateway();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Mesh init failed"); return; }

    printf("\n================ BMT GATEWAY v4.8 ================\n");
    bmt_print_hex_key("NetKey: ", g_bmt_net_key, 16);
    bmt_print_hex_key("AppKey: ", g_bmt_app_key, 16);
    printf("TB     : %s\n", BMT_TB_HOST);
    printf("Device : %s\n", BMT_DEV_NAME_GATEWAY);
    printf("\nZONE MAPPING:\n");
    printf("  scanner 0x01 -> %s\n", bmt_zone_name(0x01));
    printf("  scanner 0x02 -> %s\n", bmt_zone_name(0x02));
    printf("  scanner 0x03 -> %s\n", bmt_zone_name(0x03));
    printf("\nZONE PARAMS:\n");
    printf("  Hysteresis    : %d dBm\n", BMT_ZONE_HYSTERESIS_DBM);
    printf("  Scanner valid : %d ms\n",  BMT_SCANNER_VALID_MS);
    printf("  Out-of-range  : %d ms\n",  BMT_TAG_OUT_OF_RANGE_MS);
    printf("\nWATCHDOG CONFIG:\n");
    printf("  Timeout       : %d ms\n",  BMT_WDG_TIMEOUT_MS);
    printf("  Reset tries   : %d\n",     BMT_WDG_RESET_TRIES);
    printf("  Reset gap     : %d ms\n",  BMT_WDG_RESET_GAP_MS);
    printf("  Node wait     : %d ms\n",  BMT_WDG_NODE_WAIT_MS);
    printf("\nOTA CONFIG:\n");
    printf("  Scanner : %s\n", BMT_OTA_SCANNER_URL);
    printf("  Relay   : %s\n", BMT_OTA_RELAY_URL);
    printf("  Gateway : %s\n", BMT_OTA_GATEWAY_URL);
    printf("===================================================\n");

    bmt_print_status();
    bmt_log_node_table();
    bmt_mqtt_pub_gateway_online();

    bmt_uart_init();

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = BMT_WDT_TIMEOUT_S * 1000, .idle_core_mask = 0, .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_cfg);

    xTaskCreate(bmt_mqtt_worker_task,   "bmt_mqtt_wkr",  4096, NULL, 5, NULL);
    xTaskCreate(bmt_uart_cmd_task,      "bmt_uart",       6144, NULL, 4, NULL);
    xTaskCreate(bmt_relay_ping_task,    "bmt_relay_ping", 4096, NULL, 3, NULL);
    xTaskCreate(bmt_zone_timeout_task,  "bmt_zone_timer", 3072, NULL, 3, NULL);
    xTaskCreate(bmt_data_watchdog_task, "bmt_wdg",        3584, NULL, 2, NULL);
    xTaskCreate(bmt_wdt_feed_task,      "bmt_wdt_feed",   2048, NULL, 2, NULL);

    ESP_LOGI(TAG, "=== BMT Gateway v4.8 READY ===");
}