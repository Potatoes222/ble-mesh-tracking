/* ============================================================================
 * BMT (BLE Mesh Tracking) — SCAN NODE firmware  [v2.5]
 * ----------------------------------------------------------------------------
 * v2.5 — ĐỔI HOÀN TOÀN cách OTA:
 *   CŨ (v2.4): Gateway phân phối firmware qua BLE Mesh chunk-by-chunk
 *              → BLE Mesh throughput thấp, packet drop, không reliable
 *   MỚI (v2.5): Scanner tự OTA qua WiFi (giống Gateway):
 *     1. Gateway gửi OTA_TRIGGER (1 packet nhỏ) qua BLE Mesh
 *     2. Scanner nhận → bật WiFi → esp_https_ota() download firmware
 *     3. Verify + reboot vào firmware mới → tắt WiFi → quay lại BLE-only
 *   → Dùng BLE Mesh đúng việc (gửi command nhỏ)
 *   → Dùng WiFi đúng việc (bulk firmware transfer)
 *   → Đúng chuẩn công nghiệp, reliable
 *
 * v2.2-2.4 (giữ nguyên):
 *   UUID auto-gen từ MAC, scanner_id NVS, time-division GAP/mesh
 * ============================================================================ */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/uart.h"
#include "esp_ota_ops.h"
#include "esp_rom_crc.h"

/* [v2.5] WiFi + HTTPS OTA */
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"  /* cert bundle cho TLS — tự động dùng khi URL là https:// */

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#include "ble_mesh_example_init.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define TAG "BMT_SCAN"

/* ============================================================================
 * NVS CONFIG
 * ============================================================================ */
#define SCANNER_NVS_NAMESPACE   "bmt_scan"
#define SCANNER_NVS_KEY_ID      "scanner_id"

/* [FIX-2] Scanner ID động — load từ NVS khi boot, set qua UART lệnh 'i'
 * Default 0x01 nếu NVS chưa có giá trị (lần đầu flash) */
static uint8_t g_bmt_scanner_id = 0x01;

/* ============================================================================
 * BLE MESH VENDOR MODEL
 * ============================================================================ */
#define BMT_CID_ESP                     0x02E5
#define BMT_VND_MODEL_ID                0x0000

#define BMT_OP_VND_TAG_STATUS           ESP_BLE_MESH_MODEL_OP_3(0x00, BMT_CID_ESP)
#define BMT_OP_VND_OTA_TRIGGER          ESP_BLE_MESH_MODEL_OP_3(0x06, BMT_CID_ESP)  /* [v2.5] lệnh bật WiFi OTA */
#define BMT_OP_VND_RESET_CMD            ESP_BLE_MESH_MODEL_OP_3(0x05, BMT_CID_ESP)

#define BMT_NODE_TYPE                   0x01   /* scanner */

/* ============================================================================
 * WIFI + OTA CONFIG  [v2.5]
 * ============================================================================ */
#define BMT_WIFI_SSID                   "Hao"          /* cùng WiFi với Gateway */
#define BMT_WIFI_PASS                   "Hao@1510"
#define BMT_OTA_SERVER_BASE             "http://192.168.2.23:8080"
#define BMT_OTA_SCANNER_URL             BMT_OTA_SERVER_BASE "/Scanner.bin"
#define BMT_OTA_WIFI_TIMEOUT_MS         30000          /* chờ WiFi connect tối đa 30s */

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
#pragma pack()

/* ============================================================================
 * TAG ADV PROTOCOL
 * ============================================================================ */
#define BMT_TAG_TYPE_PERSON             0x01
#define BMT_TAG_TYPE_ASSET              0x02
#define BMT_CID_ESPRESSIF               0x02E5
#define BMT_CID_APPLE                   0x004C
#define BMT_TAG_MAJOR_PERSON            0x0001
#define BMT_TAG_MAJOR_ASSET             0x0002
static const uint8_t BMT_SYSTEM_UUID_PREFIX[4] = { 0xAB, 0x00, 0x00, 0x00 };
#define BMT_PHONE_TX_POWER_1M           (-59)

#pragma pack(1)
typedef struct {
    uint8_t  uuid[16];
    uint16_t major;
    uint16_t minor;
    int8_t   tx_power;
    uint8_t  sequence;
    uint16_t crc16;
} bmt_tag_adv_payload_t;

typedef struct {
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   tx_power;
    uint8_t  sequence;
    uint16_t crc16;
} bmt_tag_payload_t;
#pragma pack()

/* ============================================================================
 * TAG TRACKING / KALMAN
 * ============================================================================ */
#define BMT_MAX_TAGS                    20
#define BMT_TAG_TIMEOUT_MS              5000
#define BMT_LOG_RSSI_THRESHOLD_DBM      3
#define BMT_LOG_MIN_INTERVAL_MS         2000

typedef struct {
    bool     active;
    uint8_t  tag_type;
    uint16_t tag_id;
    int8_t   tx_power;
    int8_t   rssi_raw;
    float    rssi_filtered;
    float    distance;
    uint8_t  last_sequence;
    uint32_t total_received;
    uint32_t total_missed;
    uint32_t last_seen_ms;
    uint8_t  mac[6];
    int8_t   last_logged_rssi;
    uint32_t last_log_ms;
} bmt_scan_tag_info_t;

typedef struct { float q, r, x, p, k; } bmt_kalman_t;

static bmt_scan_tag_info_t g_bmt_tags  [BMT_MAX_TAGS];
static bmt_kalman_t        g_bmt_kalman[BMT_MAX_TAGS];

/* ============================================================================
 * TIME DIVISION RADIO MANAGEMENT
 * ============================================================================ */
#define BMT_GAP_SCAN_DURATION_MS        800
#define BMT_MESH_PUBLISH_DURATION_MS    700
/* Scan window < interval → chừa khe cho mesh bearer RX nhận ANNOUNCE
 * window=0x30 (30ms), interval=0x50 (50ms) → 60% duty, 40% cho mesh
 * Trước: window=interval=0x10 → 100% duty → mesh không nhận được gì */
#define BMT_SCAN_INTERVAL_UNITS         0x0050
#define BMT_SCAN_WINDOW_UNITS           0x0030
#define BMT_PROV_COMPLETE_BIT           BIT0

typedef enum { BMT_PHASE_GAP_SCAN = 0, BMT_PHASE_MESH_PUB = 1 } bmt_radio_phase_t;
static volatile bmt_radio_phase_t g_bmt_phase        = BMT_PHASE_GAP_SCAN;
static volatile bool              g_bmt_has_new_data = false;

/* ============================================================================
 * BLE MESH NODE STATE
 * ============================================================================ */
/* [FIX-1] UUID không hardcode nữa — sẽ được fill bằng MAC trong app_main
 * Layout: "SCAN"(4B) + MAC(6B) + 0x00(5B) + scanner_id(1B)
 * → Mỗi chip có UUID riêng theo MAC → không bao giờ trùng */
static uint8_t g_bmt_scan_uuid[16] = {
    0x53, 0x43, 0x41, 0x4E,   /* "SCAN" — gateway dùng để nhận ra scanner */
    0x00, 0x00, 0x00, 0x00,   /* MAC bytes 0-3 — fill trong app_main */
    0x00, 0x00,               /* MAC bytes 4-5 — fill trong app_main */
    0x00, 0x00, 0x00, 0x00,   /* padding */
    0x00,                     /* padding */
    0x00,                     /* scanner_id — fill trong app_main */
};

static uint16_t           g_bmt_node_addr = 0x0000;
static uint16_t           g_bmt_net_idx   = 0xFFFF;
static uint16_t           g_bmt_app_idx   = 0xFFFF;
static EventGroupHandle_t g_bmt_mesh_evgrp;

/* ============================================================================
 * OTA STATE  [v2.5] — WiFi OTA thay vì mesh chunk
 * ============================================================================ */
static volatile bool      g_ota_triggered  = false;  /* set khi nhận OTA_TRIGGER */
static EventGroupHandle_t g_wifi_evgrp      = NULL;
static const int          WIFI_CONNECTED_BIT = BIT0;

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

ESP_BLE_MESH_MODEL_PUB_DEFINE(bmt_vnd_pub, sizeof(bmt_tag_report_t) + 4, ROLE_NODE);

static esp_ble_mesh_model_op_t bmt_vnd_ops[] = {
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_TAG_STATUS,  sizeof(bmt_tag_report_t)),
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_OTA_TRIGGER, 1),
    ESP_BLE_MESH_MODEL_OP(BMT_OP_VND_RESET_CMD,   1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t bmt_vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(BMT_CID_ESP, BMT_VND_MODEL_ID,
                              bmt_vnd_ops, &bmt_vnd_pub, NULL),
};

static esp_ble_mesh_model_t bmt_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&bmt_cfg_server),
};

static esp_ble_mesh_elem_t bmt_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, bmt_root_models, bmt_vnd_models),
};

static esp_ble_mesh_comp_t bmt_composition = {
    .cid           = BMT_CID_ESP,
    .element_count = ARRAY_SIZE(bmt_elements),
    .elements      = bmt_elements,
};

static esp_ble_mesh_prov_t bmt_provision = { .uuid = g_bmt_scan_uuid };

static esp_ble_scan_params_t g_bmt_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = BMT_SCAN_INTERVAL_UNITS,
    .scan_window        = BMT_SCAN_WINDOW_UNITS,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
};

/* ============================================================================
 * NVS — SCANNER ID  [FIX-2]
 * ============================================================================ */
static void scanner_nvs_load_id(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SCANNER_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "[NVS] No scanner_id saved, using default: 0x%02X",
                 g_bmt_scanner_id);
        return;
    }
    if (err != ESP_OK) return;
    uint8_t id = 0;
    if (nvs_get_u8(h, SCANNER_NVS_KEY_ID, &id) == ESP_OK
            && id >= 1 && id <= 8) {
        g_bmt_scanner_id = id;
        ESP_LOGI(TAG, "[NVS] Scanner ID loaded: 0x%02X", g_bmt_scanner_id);
    } else {
        ESP_LOGW(TAG, "[NVS] Invalid scanner_id in NVS, using default: 0x%02X",
                 g_bmt_scanner_id);
    }
    nvs_close(h);
}

static void scanner_nvs_save_id(uint8_t id)
{
    nvs_handle_t h;
    if (nvs_open(SCANNER_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, SCANNER_NVS_KEY_ID, id);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "[NVS] Scanner ID saved: 0x%02X", id);
}

/* ============================================================================
 * KALMAN FILTER
 * ============================================================================ */
static void bmt_kalman_init(bmt_kalman_t *kf, float initial_rssi)
{
    kf->q = 0.1f; kf->r = 2.0f;
    kf->x = initial_rssi; kf->p = 1.0f; kf->k = 0.0f;
}

static float bmt_kalman_update(bmt_kalman_t *kf, float rssi)
{
    kf->p = kf->p + kf->q;
    kf->k = kf->p / (kf->p + kf->r);
    kf->x = kf->x + kf->k * (rssi - kf->x);
    kf->p = (1.0f - kf->k) * kf->p;
    return kf->x;
}

/* ============================================================================
 * CRC-16 CCITT
 * ============================================================================ */
static uint16_t bmt_crc16_ccitt(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

static bool bmt_verify_crc16(const uint8_t *data, int len, uint16_t received_crc)
{
    return (bmt_crc16_ccitt(data, len) == received_crc);
}

/* ============================================================================
 * DISTANCE CALCULATION
 * ============================================================================ */
#define BMT_PATH_LOSS_N  2.5f

static float bmt_calculate_distance(int8_t tx_power, float rssi_filtered)
{
    if (rssi_filtered >= 0) return 0.0f;
    float ratio = (tx_power - rssi_filtered) / (10.0f * BMT_PATH_LOSS_N);
    return powf(10.0f, ratio);
}

/* ============================================================================
 * TAG TABLE
 * ============================================================================ */
static int bmt_find_tag(uint16_t tag_id)
{
    for (int i = 0; i < BMT_MAX_TAGS; i++)
        if (g_bmt_tags[i].active && g_bmt_tags[i].tag_id == tag_id) return i;
    return -1;
}

static int bmt_add_tag(uint16_t tag_id, uint8_t tag_type,
                       int8_t tx_power, int8_t rssi,
                       uint8_t sequence, uint8_t *mac)
{
    for (int i = 0; i < BMT_MAX_TAGS; i++) {
        if (!g_bmt_tags[i].active) {
            memset(&g_bmt_tags[i], 0, sizeof(bmt_scan_tag_info_t));
            g_bmt_tags[i].active           = true;
            g_bmt_tags[i].tag_type         = tag_type;
            g_bmt_tags[i].tag_id           = tag_id;
            g_bmt_tags[i].tx_power         = tx_power;
            g_bmt_tags[i].rssi_raw         = rssi;
            g_bmt_tags[i].rssi_filtered    = (float)rssi;
            g_bmt_tags[i].last_sequence    = sequence;
            g_bmt_tags[i].total_received   = 1;
            g_bmt_tags[i].last_seen_ms     = xTaskGetTickCount() * portTICK_PERIOD_MS;
            g_bmt_tags[i].last_logged_rssi = rssi;
            g_bmt_tags[i].last_log_ms      = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (mac) memcpy(g_bmt_tags[i].mac, mac, 6);
            bmt_kalman_init(&g_bmt_kalman[i], (float)rssi);
            g_bmt_tags[i].distance = bmt_calculate_distance(tx_power, (float)rssi);
            ESP_LOGI(TAG, "New tag: 0x%04X (%s) RSSI=%ddBm",
                     tag_id,
                     tag_type == BMT_TAG_TYPE_PERSON ? "PERSON" : "ASSET",
                     rssi);
            return i;
        }
    }
    ESP_LOGW(TAG, "Tag table full!");
    return -1;
}

static void bmt_update_tag(int idx, int8_t rssi, uint8_t sequence)
{
    bmt_scan_tag_info_t *t   = &g_bmt_tags[idx];
    uint32_t             now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (sequence == t->last_sequence) {
        t->rssi_raw      = rssi;
        t->rssi_filtered = bmt_kalman_update(&g_bmt_kalman[idx], (float)rssi);
        t->distance      = bmt_calculate_distance(t->tx_power, t->rssi_filtered);
        t->last_seen_ms  = now;
        t->total_received++;
        return;
    }

    int8_t diff = (int8_t)(sequence - t->last_sequence);
    if (diff < 0 && diff > -10) return;

    uint8_t expected = t->last_sequence + 1;
    if (sequence != expected) {
        uint8_t missed = 0;
        if (diff > 1)        missed = (uint8_t)(diff - 1);
        else if (diff < -10) missed = (uint8_t)(256 - t->last_sequence - 1 + sequence);
        if (missed > 0 && missed < 200) t->total_missed += missed;
    }

    t->rssi_raw      = rssi;
    t->rssi_filtered = bmt_kalman_update(&g_bmt_kalman[idx], (float)rssi);
    t->distance      = bmt_calculate_distance(t->tx_power, t->rssi_filtered);
    t->last_sequence = sequence;
    t->total_received++;
    t->last_seen_ms  = now;
    g_bmt_has_new_data = true;

    int8_t   rd = (int8_t)abs((int)rssi - (int)t->last_logged_rssi);
    uint32_t td = now - t->last_log_ms;
    if (rd >= BMT_LOG_RSSI_THRESHOLD_DBM || td >= BMT_LOG_MIN_INTERVAL_MS) {
        ESP_LOGI(TAG, "Tag 0x%04X | RSSI=%ddBm | Filt=%.1fdBm | Dist=%.2fm",
                 t->tag_id, rssi, t->rssi_filtered, t->distance);
        t->last_logged_rssi = rssi;
        t->last_log_ms      = now;
    }
}

static void bmt_print_tag_table(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool has_tag = false;
    printf("\n========== TAG TABLE (Scanner 0x%02X) ==========\n",
           g_bmt_scanner_id);
    for (int i = 0; i < BMT_MAX_TAGS; i++) {
        if (!g_bmt_tags[i].active) continue;
        has_tag = true;
        bmt_scan_tag_info_t *t   = &g_bmt_tags[i];
        uint32_t             age = (now - t->last_seen_ms) / 1000;
        uint32_t tot = t->total_received + t->total_missed;
        float    lr  = (tot > 0)
                       ? (float)t->total_missed / tot * 100.0f : 0.0f;
        printf("Tag 0x%04X | %s | RSSI=%d | Filt=%.1f | Dist=%.2fm"
               " | Loss=%.1f%% | %lus ago\n",
               t->tag_id,
               t->tag_type == BMT_TAG_TYPE_PERSON ? "PERSON" : "ASSET",
               t->rssi_raw, t->rssi_filtered, t->distance, lr, age);
    }
    if (!has_tag) printf("  No tags in range\n");
    printf("--------------------------------------------------\n");
}

/* ============================================================================
 * PARSE ADV PAYLOAD
 * ============================================================================ */
static bool bmt_parse_tag_payload(uint8_t *adv_data, uint8_t adv_len,
                                  bmt_tag_payload_t *out)
{
    if (!adv_data || adv_len < 4 || !out) return false;
    int pos = 0;
    while (pos < adv_len) {
        uint8_t field_len  = adv_data[pos];
        if (field_len == 0 || pos + field_len >= adv_len) break;
        uint8_t field_type = adv_data[pos + 1];
        if (field_type == 0xFF && field_len >= 3) {
            uint16_t cid = (uint16_t)adv_data[pos + 2]
                         | ((uint16_t)adv_data[pos + 3] << 8);
            if (cid == BMT_CID_ESPRESSIF && field_len >= (1 + 2 + 24)) {
                bmt_tag_adv_payload_t *p =
                    (bmt_tag_adv_payload_t *)(adv_data + pos + 4);
                if (memcmp(p->uuid, BMT_SYSTEM_UUID_PREFIX, 4) != 0)
                    goto next_field;
                if (!bmt_verify_crc16((uint8_t *)p,
                        sizeof(*p) - sizeof(p->crc16), p->crc16))
                    goto next_field;
                out->tag_type = (p->major == BMT_TAG_MAJOR_PERSON)
                                ? BMT_TAG_TYPE_PERSON : BMT_TAG_TYPE_ASSET;
                out->tag_id   = p->minor;
                out->tx_power = p->tx_power;
                out->sequence = p->sequence;
                out->crc16    = p->crc16;
                return true;
            }
            if (cid == BMT_CID_APPLE && field_len >= 26
                    && adv_data[pos + 4] == 0x02
                    && adv_data[pos + 5] == 0x15) {
                uint16_t major = ((uint16_t)adv_data[pos + 22] << 8)
                               | adv_data[pos + 23];
                uint16_t minor = ((uint16_t)adv_data[pos + 24] << 8)
                               | adv_data[pos + 25];
                if (major != BMT_TAG_MAJOR_PERSON
                        && major != BMT_TAG_MAJOR_ASSET)
                    goto next_field;
                out->tag_type = (major == BMT_TAG_MAJOR_PERSON)
                                ? BMT_TAG_TYPE_PERSON : BMT_TAG_TYPE_ASSET;
                out->tag_id   = minor | 0x8000;
                out->tx_power = BMT_PHONE_TX_POWER_1M;
                out->sequence = 0;
                out->crc16    = 0;
                return true;
            }
        }
        next_field: pos += field_len + 1;
    }
    return false;
}

/* ============================================================================
 * OTA FUNCTIONS
 * ============================================================================ */
/* [v2.5] WiFi event handler — bật khi cần OTA */
static void bmt_wifi_event_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(g_wifi_evgrp, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "[OTA] WiFi got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(g_wifi_evgrp, WIFI_CONNECTED_BIT);
    }
}

/* [v2.5] Task thực hiện WiFi OTA — chạy khi nhận OTA_TRIGGER qua mesh
 * Flow: bật WiFi → connect → esp_https_ota() → verify → reboot
 * Nếu fail: tắt WiFi, quay lại BLE-only bình thường */
static void bmt_ota_wifi_task(void *arg)
{
    (void)arg;

    /* Tắt toàn bộ ESP_LOG* trong lúc OTA — chỉ giữ printf OTA
     * radio_manager đã stop GAP scan + publish khi g_ota_triggered=true
     * nên chỉ còn WiFi stack spam log → tắt hết */
    esp_log_level_set("*", ESP_LOG_NONE);

    printf("\n[OTA] ===== WiFi OTA triggered =====\n");
    printf("[OTA] URL: %s\n", BMT_OTA_SCANNER_URL);

    /* 1. Khởi tạo WiFi station */
    g_wifi_evgrp = xEventGroupCreate();
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "[OTA] netif init failed"); goto ota_fail;
    }
    /* event loop có thể đã tạo bởi BLE — bỏ qua nếu đã tồn tại */
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "[OTA] wifi init failed: %s", esp_err_to_name(err)); goto ota_fail; }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &bmt_wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &bmt_wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_cfg = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
    strncpy((char *)wifi_cfg.sta.ssid,     BMT_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, BMT_WIFI_PASS, sizeof(wifi_cfg.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();

    printf("[OTA] Connecting WiFi (max %ds)...\n", BMT_OTA_WIFI_TIMEOUT_MS / 1000);
    EventBits_t bits = xEventGroupWaitBits(g_wifi_evgrp, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(BMT_OTA_WIFI_TIMEOUT_MS));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "[OTA] WiFi connect timeout"); goto ota_fail_wifi;
    }

    /* 2. esp_https_ota — hoạt động với cả http:// và https://
     * crt_bundle_attach: tự động verify cert khi https://, bỏ qua khi http://
     * Sau này chỉ cần đổi URL → https:// là có TLS, không sửa code */
    printf("[OTA] WiFi connected — downloading firmware...\n");
    esp_http_client_config_t http_cfg = {
        .url               = BMT_OTA_SCANNER_URL,
        .timeout_ms        = 120000,  /* 120s — đủ cho WiFi/BLE coexist delay */
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
    err = esp_https_ota(&ota_cfg);

    if (err == ESP_OK) {
        printf("[OTA] ===== SUCCESS — rebooting into new firmware =====\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "[OTA] esp_https_ota FAILED: %s", esp_err_to_name(err));
    }

ota_fail_wifi:
    /* Tắt WiFi, quay lại BLE-only */
    esp_wifi_stop();
    esp_wifi_deinit();
ota_fail:
    printf("[OTA] OTA failed — back to BLE-only mode\n");
    g_ota_triggered = false;
    vTaskDelete(NULL);
}

/* ============================================================================
 * GAP CALLBACK
 * ============================================================================ */
static void bmt_gap_event_handler(esp_gap_ble_cb_event_t event,
                                  esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan params set OK"); break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
            ESP_LOGE(TAG, "Scan start FAILED");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;

        /* [v2.5] Kiểm tra OTA beacon TRƯỚC khi xử lý tag
         * Gateway advertise beacon với pattern: CID(E502) + "BMT" + 0xFA + target
         * Scanner đang scan 100% duty → CHẮC CHẮN nhận được beacon này */
        if (!g_ota_triggered) {
            uint8_t *adv = param->scan_rst.ble_adv;
            uint8_t  len = param->scan_rst.adv_data_len;
            for (int i = 0; i + 8 < len; ) {   /* FIX: +8 thay +9 để không miss field cuối */
                uint8_t field_len  = adv[i];
                if (field_len == 0) break;
                uint8_t field_type = adv[i+1];
                if (field_type == 0xFF && field_len >= 8 &&  /* FIX: >=8 thay >=9, field_len=8 */
                    adv[i+2] == 0xE5 && adv[i+3] == 0x02 &&  /* CID Espressif */
                    adv[i+4] == 'B'  && adv[i+5] == 'M' &&
                    adv[i+6] == 'T'  && adv[i+7] == 0xFA &&   /* OTA magic */
                    (adv[i+8] == BMT_NODE_TYPE || adv[i+8] == 0xFF)) { /* target match */
                    printf("[OTA] BLE beacon from Gateway — triggering WiFi OTA!\n");
                    g_ota_triggered = true;
                    xTaskCreate(bmt_ota_wifi_task, "bmt_ota_wifi", 8192, NULL, 5, NULL);
                    break;
                }
                i += field_len + 1;
            }
        }
        if (g_ota_triggered) break;  /* đang OTA, bỏ qua tag processing */
        if (g_bmt_phase != BMT_PHASE_GAP_SCAN) break;
        EventBits_t bits = xEventGroupGetBits(g_bmt_mesh_evgrp);
        if (!(bits & BMT_PROV_COMPLETE_BIT)) break;

        bmt_tag_payload_t payload;
        if (!bmt_parse_tag_payload(param->scan_rst.ble_adv,
                                   param->scan_rst.adv_data_len,
                                   &payload)) break;

        int idx = bmt_find_tag(payload.tag_id);
        if (idx < 0)
            idx = bmt_add_tag(payload.tag_id, payload.tag_type,
                              payload.tx_power, param->scan_rst.rssi,
                              payload.sequence, param->scan_rst.bda);
        else
            bmt_update_tag(idx, param->scan_rst.rssi, payload.sequence);

        g_bmt_has_new_data = true;
        break;
    }
    default: break;
    }
}

/* ============================================================================
 * MESH CALLBACKS
 * ============================================================================ */
static void bmt_mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        g_bmt_net_idx   = param->node_prov_complete.net_idx;
        g_bmt_node_addr = param->node_prov_complete.addr;
        ESP_LOGI(TAG, "Provisioned! addr=0x%04x", g_bmt_node_addr);
        xEventGroupSetBits(g_bmt_mesh_evgrp, BMT_PROV_COMPLETE_BIT);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        g_bmt_node_addr = 0x0000;
        g_bmt_net_idx   = 0xFFFF;
        g_bmt_app_idx   = 0xFFFF;
        xEventGroupClearBits(g_bmt_mesh_evgrp, BMT_PROV_COMPLETE_BIT);
        esp_ble_mesh_node_prov_enable(
            ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
        break;
    default: break;
    }
}

static void bmt_mesh_cfg_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) return;
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        g_bmt_app_idx = param->value.state_change.appkey_add.app_idx;
        ESP_LOGI(TAG, "=== AppKey received! idx=0x%04x — Ready! ===",
                 g_bmt_app_idx);
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        ESP_LOGI(TAG, "Model AppKey bind done");
        break;
    default: break;
    }
}

static void bmt_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                     esp_ble_mesh_model_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_MODEL_OPERATION_EVT || !param) return;

    uint32_t opcode = param->model_operation.opcode;
    uint16_t src    = param->model_operation.ctx->addr;
    (void)param->model_operation.msg;
    (void)param->model_operation.length;

    /* [v2.5] OTA_TRIGGER: nhận lệnh → spawn task bật WiFi + esp_https_ota
     * KHÔNG làm OTA trong callback (block BLE host task) → spawn task riêng */
    if (opcode == BMT_OP_VND_OTA_TRIGGER) {
        if (g_ota_triggered) {
            ESP_LOGW(TAG, "[OTA] Already triggered, ignoring");
            return;
        }
        ESP_LOGW(TAG, "[OTA] OTA_TRIGGER received from 0x%04x — starting WiFi OTA", src);
        g_ota_triggered = true;
        xTaskCreate(bmt_ota_wifi_task, "bmt_ota_wifi", 8192, NULL, 5, NULL);
        return;
    }
    if (opcode == BMT_OP_VND_RESET_CMD) {
        ESP_LOGW(TAG, "[VND] RESET_CMD — resetting mesh NVS + reboot...");
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_ble_mesh_node_local_reset();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return;
    }
}

/* ============================================================================
 * PUBLISH TAG DATA LÊN MESH
 * ============================================================================ */
static void bmt_publish_tags_to_mesh(void)
{
    if (g_bmt_node_addr == 0x0000) return;
    if (g_bmt_app_idx   == 0xFFFF) return;

    int published = 0;
    for (int i = 0; i < BMT_MAX_TAGS; i++) {
        if (!g_bmt_tags[i].active) continue;

        uint32_t total    = g_bmt_tags[i].total_received
                          + g_bmt_tags[i].total_missed;
        uint8_t  loss_pct = (total > 0)
                          ? (uint8_t)(g_bmt_tags[i].total_missed * 100 / total)
                          : 0;
        int16_t dist_dm   = (int16_t)(g_bmt_tags[i].distance * 10.0f);
        if (dist_dm < 0) dist_dm = 0;

        bmt_tag_report_t report = {
            .scanner_id  = g_bmt_scanner_id,   /* [FIX-3] dùng dynamic ID */
            .tag_type    = g_bmt_tags[i].tag_type,
            .tag_id      = g_bmt_tags[i].tag_id,
            .rssi        = (int8_t)g_bmt_tags[i].rssi_filtered,
            .distance_dm = dist_dm,
            .loss_pct    = loss_pct,
        };

        bmt_vnd_models[0].pub->publish_addr = 0x0001;
        bmt_vnd_models[0].pub->app_idx      = g_bmt_app_idx;
        bmt_vnd_models[0].pub->ttl          = 7;

        esp_err_t err = esp_ble_mesh_model_publish(
            &bmt_vnd_models[0], BMT_OP_VND_TAG_STATUS,
            sizeof(report), (uint8_t *)&report, ROLE_NODE);

        if (err == ESP_OK) {
            published++;
            ESP_LOGI(TAG, "[Mesh] Published 0x%04X rssi=%d dist=%.1fm loss=%u%%",
                     report.tag_id, report.rssi, dist_dm / 10.0f, loss_pct);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (published == 0) ESP_LOGD(TAG, "[Mesh] No tags");
}

/* ============================================================================
 * TASKS
 * ============================================================================ */
static void bmt_radio_manager_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Waiting for provision...");
    xEventGroupWaitBits(g_bmt_mesh_evgrp, BMT_PROV_COMPLETE_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_ble_gap_stop_scanning();
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_err_t err = esp_ble_gap_set_scan_params(&g_bmt_scan_params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_scan_params failed"); vTaskDelete(NULL); return;
    }

    ESP_LOGI(TAG, "=== Radio Manager started (Scanner ID=0x%02X) ===",
             g_bmt_scanner_id);
    g_bmt_phase = BMT_PHASE_MESH_PUB;

    while (1) {
        /* [v2.5] OTA active: dừng GAP scan để WiFi OTA có toàn bộ radio
         * WiFi và BLE coexist nhưng OTA cần băng thông → tạm dừng scan tag */
        if (g_ota_triggered) {
            if (g_bmt_phase == BMT_PHASE_GAP_SCAN) {
                esp_ble_gap_stop_scanning();
                g_bmt_phase = BMT_PHASE_MESH_PUB;
                ESP_LOGI(TAG, "[OTA] GAP scan paused for WiFi OTA");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* Normal time division: GAP scan → mesh publish */
        g_bmt_phase        = BMT_PHASE_GAP_SCAN;
        g_bmt_has_new_data = false;
        esp_ble_gap_start_scanning(0);
        vTaskDelay(pdMS_TO_TICKS(BMT_GAP_SCAN_DURATION_MS));

        g_bmt_phase = BMT_PHASE_MESH_PUB;
        esp_ble_gap_stop_scanning();
        vTaskDelay(pdMS_TO_TICKS(100));

        if (g_bmt_has_new_data) {
            bmt_publish_tags_to_mesh();
            bmt_print_tag_table();
            g_bmt_has_new_data = false;
        }
        vTaskDelay(pdMS_TO_TICKS(BMT_MESH_PUBLISH_DURATION_MS - 100));
    }
}

static void bmt_timeout_check_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* Tag timeout — bỏ qua khi đang OTA (WiFi chiếm radio) */
        if (!g_ota_triggered) {
            for (int i = 0; i < BMT_MAX_TAGS; i++) {
                if (!g_bmt_tags[i].active) continue;
                if ((now - g_bmt_tags[i].last_seen_ms) > BMT_TAG_TIMEOUT_MS) {
                    ESP_LOGW(TAG, "Tag 0x%04X OUT", g_bmt_tags[i].tag_id);
                    g_bmt_tags[i].active = false;
                    memset(&g_bmt_kalman[i], 0, sizeof(bmt_kalman_t));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ============================================================================
 * MESH INIT
 * ============================================================================ */
static esp_err_t bmt_ble_mesh_init_scan(void)
{
    esp_ble_mesh_register_prov_callback(bmt_mesh_prov_cb);
    esp_ble_mesh_register_config_server_callback(bmt_mesh_cfg_server_cb);
    esp_ble_mesh_register_custom_model_callback(bmt_mesh_custom_model_cb);

    ESP_ERROR_CHECK(esp_ble_mesh_init(&bmt_provision, &bmt_composition));

    bool provisioned = esp_ble_mesh_node_is_provisioned();
    if (provisioned) {
        ESP_LOGI(TAG, "Already provisioned (restored from NVS)");
        g_bmt_node_addr = 0x00FF;   /* sentinel: provisioned, addr by stack */
        g_bmt_app_idx   = 0x0000;
        xEventGroupSetBits(g_bmt_mesh_evgrp, BMT_PROV_COMPLETE_BIT);
        ESP_LOGI(TAG, "[MESH] NVS restore OK | scanner_id=0x%02X | app_idx=0x%04X",
                 g_bmt_scanner_id, g_bmt_app_idx);
    } else {
        g_bmt_phase = BMT_PHASE_MESH_PUB;
        ESP_ERROR_CHECK(esp_ble_mesh_node_prov_enable(
            ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        ESP_LOGI(TAG, "Scanner ID=0x%02X | Waiting provision...",
                 g_bmt_scanner_id);
    }
    return ESP_OK;
}

/* ============================================================================
 * UART CMD  [FIX-2] thêm lệnh 'i' để set scanner ID
 * ============================================================================ */
static void bmt_uart_cmd_task(void *arg)
{
    (void)arg;
    uart_config_t cfg = {
        .baud_rate  = 115200, .data_bits = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &cfg);

    printf("\n===== BMT SCAN NODE v2.5 =====\n");
    printf("Scanner ID : 0x%02X\n", g_bmt_scanner_id);
    printf("Node type  : 0x%02X (scanner)\n", BMT_NODE_TYPE);
    printf("UUID       : SCAN + MAC (auto)\n");
    printf("Commands:\n");
    printf("  r -> RESET mesh provision\n");
    printf("  i -> SET scanner ID (1-8)\n");
    printf("  o -> TEST WiFi OTA\n");
    printf("  1 -> STATUS\n");
    printf("==============================\n");

    uint8_t ch;
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(200));
        if (len <= 0 || ch == '\r' || ch == '\n') continue;

        switch (ch) {
        case 'r': case 'R':
            printf("\n[UART] Reset mesh + reboot...\n");
            esp_ble_mesh_node_local_reset();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;

        /* [FIX-2] Set Scanner ID — lưu vào NVS, reset để apply */
        case 'i': case 'I':
            printf("\n[UART] Enter scanner ID (1-8): ");
            fflush(stdout);
            {
                uint8_t id_char = 0;
                /* Đợi 10s cho user nhập */
                uart_read_bytes(UART_NUM_0, &id_char, 1, pdMS_TO_TICKS(10000));
                uint8_t new_id = id_char - '0';
                if (new_id >= 1 && new_id <= 8) {
                    scanner_nvs_save_id(new_id);
                    g_bmt_scanner_id = new_id;
                    printf("%d\n[UART] Scanner ID set to 0x%02X\n"
                           "[UART] Bấm 'r' để reset và apply UUID mới\n",
                           new_id, new_id);
                } else {
                    printf("\n[UART] Invalid ID — phải từ 1 đến 8\n");
                }
            }
            break;

        case '1':
            printf("\n===== SCANNER STATUS =====\n");
            printf("Scanner ID : 0x%02X\n", g_bmt_scanner_id);
            printf("UUID       : ");
            for (int i = 0; i < 16; i++)
                printf("%02X%s", g_bmt_scan_uuid[i], i < 15 ? ":" : "\n");
            printf("Node addr  : 0x%04X\n", g_bmt_node_addr);
            printf("App idx    : 0x%04X\n", g_bmt_app_idx);
            printf("OTA state  : %s\n", g_ota_triggered ? "WiFi OTA running" : "IDLE");
            printf("Heap free  : %lu bytes\n",
                   (unsigned long)esp_get_free_heap_size());
            printf("==========================\n");
            break;

        /* [v2.5] Test OTA trực tiếp trên scanner này (không cần Gateway) */
        case 'o': case 'O':
            if (g_ota_triggered) { printf("\n[OTA] Already running!\n"); break; }
            printf("\n[UART] Manual OTA trigger — starting WiFi OTA...\n");
            g_ota_triggered = true;
            xTaskCreate(bmt_ota_wifi_task, "bmt_ota_wifi", 8192, NULL, 5, NULL);
            break;

        default:
            printf("[UART] Unknown: %c  (r=reset, i=set ID, o=OTA, 1=status)\n", ch);
            break;
        }
    }
}

/* ============================================================================
 * MAIN
 * ============================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "=== BMT Scan Node v2.5 Starting ===");

    /* NVS init */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES
            || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* [FIX-2] Load scanner ID từ NVS (tồn tại qua OTA) */
    scanner_nvs_load_id();

    /* [FIX-1] Auto-gen UUID từ MAC address
     * Layout: "SCAN"(4B) + MAC(6B) + 0x00(5B) + scanner_id(1B)
     * MAC đã unique theo hardware → UUID unique không cần config */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    memcpy(&g_bmt_scan_uuid[4], mac, 6);        /* byte 4-9 = MAC */
    g_bmt_scan_uuid[15] = g_bmt_scanner_id;     /* byte 15 = scanner ID */

    ESP_LOGI(TAG, "Scanner ID : 0x%02X", g_bmt_scanner_id);
    ESP_LOGI(TAG, "MAC        : %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "UUID       : SCAN+%02X%02X%02X%02X%02X%02X+...+%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             g_bmt_scanner_id);

    g_bmt_mesh_evgrp = xEventGroupCreate();

    err = bluetooth_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "BT init failed!"); return; }

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(bmt_gap_event_handler));

    err = bmt_ble_mesh_init_scan();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Mesh init failed!"); return; }

    xTaskCreate(bmt_radio_manager_task, "bmt_radio",   3072, NULL, 6, NULL);
    xTaskCreate(bmt_timeout_check_task, "bmt_timeout", 2048, NULL, 3, NULL);
    xTaskCreate(bmt_uart_cmd_task,      "bmt_uart",    2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "=== Scan Node v2.5 READY (r=reset, i=set ID, 1=status) ===");
}