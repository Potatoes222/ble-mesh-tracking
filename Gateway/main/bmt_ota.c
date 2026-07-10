#include "bmt_ota.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "psa/crypto.h"

/* [v5.0-nimble] NimBLE native GAP API thay cho esp_gap_ble_api.h (Bluedroid-only)
 * — esp_ble_gap_config_adv_data_raw/start_advertising KHÔNG tồn tại dưới NimBLE.
 * Include "host/ble_hs.h" (không phải "host/ble_gap.h" riêng lẻ) — giống cách
 * example_init.c (common component) làm, vì BLE_OWN_ADDR_PUBLIC/BLE_HS_FOREVER
 * nằm ở nimble/ble.h được kéo vào gián tiếp qua chain include của ble_hs.h. */
#include "host/ble_hs.h"

#include "bmt_config.h"
#include "bmt_mesh.h"
#include "bmt_node_table.h"
#include "bmt_types.h"

static const char *TAG = "BMT_OTA";

#define BMT_OTA_HTTP_TIMEOUT_MS      15000
#define BMT_OTA_NODE_GAP_MS          90000  /* 90s mỗi node: đủ download ~840KB + reboot */
#define BMT_OTA_BEACON_DURATION_MS   15000  /* 15s: đủ nhiều chu kỳ GAP scan để scanner chắc chắn bắt được */

static volatile bool s_running = false;

static void print_sha256_hex(const char *label, const uint8_t *sha, size_t len)
{
    printf("%s", label);
    for (size_t i = 0; i < len; i++) printf("%02x", sha[i]);
    printf("\n");
}

/* ============================================================================
 * GATEWAY SELF-UPDATE
 * ============================================================================ */
static void self_update_task(void *arg)
{
    (void)arg;
    printf("\n[OTA] ===== Gateway self-update =====\n");
    printf("[OTA] URL: %s\n", BMT_OTA_GATEWAY_URL);

    /* [SECURITY] HTTP thuần (LAN nội bộ) — quyết định có chủ đích, xem comment
     * tại BMT_OTA_SERVER_BASE trong bmt_config.h. Không gắn crt_bundle_attach
     * vì không có tác dụng gì với http://.
     * [v6.0-auto-ota] Dùng bộ API "advanced" để đọc version trong header .bin
     * trên server TRƯỚC khi tải hết, so với version đang chạy — bỏ qua nếu
     * cùng version thay vì luôn tải lại toàn bộ ~1MB không cần thiết. */
    esp_http_client_config_t http_cfg = {
        .url = BMT_OTA_GATEWAY_URL, .timeout_ms = BMT_OTA_HTTP_TIMEOUT_MS,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
    esp_https_ota_handle_t ota_handle = NULL;

    esp_err_t err = esp_https_ota_begin(&ota_cfg, &ota_handle);
    if (err != ESP_OK) {
        printf("[OTA] esp_https_ota_begin FAILED: %s\n", esp_err_to_name(err));
        goto self_update_fail;
    }

    esp_app_desc_t new_desc;
    err = esp_https_ota_get_img_desc(ota_handle, &new_desc);
    if (err != ESP_OK) {
        printf("[OTA] esp_https_ota_get_img_desc FAILED: %s\n", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        goto self_update_fail;
    }

    const esp_app_desc_t *cur_desc = esp_app_get_description();

    /* ===== GIAI ĐOẠN 1: SHA256 — nội dung .elf có thật sự khác không ===== */
    print_sha256_hex("[OTA] Node   SHA256: ", cur_desc->app_elf_sha256, sizeof(cur_desc->app_elf_sha256));
    print_sha256_hex("[OTA] Server SHA256: ", new_desc.app_elf_sha256, sizeof(new_desc.app_elf_sha256));

    if (memcmp(new_desc.app_elf_sha256, cur_desc->app_elf_sha256, sizeof(new_desc.app_elf_sha256)) == 0) {
        printf("[OTA] SHA256 match — node firmware is IDENTICAL to server firmware, skip.\n");
        esp_https_ota_abort(ota_handle);
        s_running = false;
        vTaskDelete(NULL);
        return;
    }
    printf("[OTA] SHA256 differ — node firmware DIFFERS from server firmware, checking version...\n");

    /* ===== GIAI ĐOẠN 2: SHA256 đã khác — so version (mtime nguồn, dạng
     * YYYYMMDDHHMMSS) để biết bản nào MỚI HƠN, tránh downgrade. ===== */
    printf("[OTA] Node   version: %s\n", cur_desc->version);
    printf("[OTA] Server version: %s\n", new_desc.version);

    if (strncmp(new_desc.version, cur_desc->version, sizeof(new_desc.version)) <= 0) {
        printf("[OTA] Server version is NOT newer — skip, no downgrade.\n");
        esp_https_ota_abort(ota_handle);
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    printf("[OTA] Server version is NEWER -> flashing firmware...\n");
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    }

    if (err == ESP_OK && esp_https_ota_is_complete_data_received(ota_handle)) {
        err = esp_https_ota_finish(ota_handle);
    } else {
        esp_https_ota_abort(ota_handle);
        if (err == ESP_OK) err = ESP_FAIL;
    }

    if (err == ESP_OK) {
        printf("[OTA] ===== OTA SUCCESS — rebooting =====\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

self_update_fail:
    printf("[OTA] Gateway self-update FAILED: %s\n", esp_err_to_name(err));
    s_running = false;
    vTaskDelete(NULL);
}

esp_err_t bmt_ota_gateway_self_update(void)
{
    if (s_running) { ESP_LOGW(TAG, "OTA already running"); return ESP_ERR_INVALID_STATE; }
    s_running = true;
    if (xTaskCreate(self_update_task, "bmt_ota_self", 8192, NULL, 4, NULL) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ============================================================================
 * OTA-BEACON  [v4.9-security → v5.0-nimble]
 * ----------------------------------------------------------------------------
 * Gateway advertise 1 gói BLE ADV thô đặc biệt → Scanner GAP-scan 100% duty
 * chắc chắn bắt được → tự bật WiFi OTA. Bypass hoàn toàn mesh bearer.
 *
 * [SECURITY] HMAC-16 chống giả mạo (key RIÊNG, KHÔNG dùng chung với Tag) —
 * PHẢI GIỐNG HỆT BMT_OTA_BEACON_HMAC_KEY trong Scanner/main/bmt_auth.c.
 *
 * [⚠️ CHƯA KIỂM CHỨNG TRÊN PHẦN CỨNG THẬT] Dưới Bluedroid, code gốc dùng
 * esp_ble_gap_config_adv_data_raw()/esp_ble_gap_start_advertising(). API này
 * KHÔNG tồn tại dưới NimBLE — thay bằng ble_gap_adv_set_data()/ble_gap_adv_start()
 * (NimBLE host API thuần, "host/ble_gap.h"). Rủi ro chưa xác nhận được: BLE Mesh
 * (esp_ble_mesh) chạy trên NimBLE có thể ĐANG tự chiếm advertising instance của
 * host (network/proxy beacon) → ble_gap_adv_start() có thể trả lỗi
 * BLE_HS_EALREADY. Nếu vậy, hàm bên dưới sẽ return lỗi và
 * bmt_ota_trigger_all_scanners() tự động fallback sang mesh unicast (không có
 * broadcast đồng thời, nhưng vẫn cập nhật được đầy đủ scanner).
 * ============================================================================ */
/* [SECURITY] Key HMAC beacon KHÔNG còn hardcode cố định — chỉ dùng làm giá trị
 * "bootstrap" cho lần boot ĐẦU TIÊN của Gateway (trước khi NVS có key nào).
 * Ngay từ lần boot đó, Gateway tự sinh 1 key RANDOM thật (esp_fill_random,
 * giống hệt cách sinh NetKey/AppKey ở bmt_mesh.c) và lưu NVS — key hardcode
 * bên dưới sau đó không còn ý nghĩa gì (không node nào cần match nó, vì mỗi
 * Scanner nhận key hiện hành trực tiếp từ Gateway ngay sau khi provision
 * xong, qua kênh mesh đã được AppKey mã hóa — xem bmt_ota_push_beacon_key_to_node).
 * Root cause được giải quyết: dump firmware chỉ lộ key TẠI THỜI ĐIỂM ĐÓ (nếu
 * còn ở dạng NVS-random), không suy ra được key của lần rotate khác vì key
 * mới sinh random độc lập, không suy diễn được từ key cũ hay từ firmware. */
static const uint8_t BMT_OTA_BEACON_HMAC_KEY_BOOTSTRAP[16] = {
    0x5A, 0x59, 0xD8, 0xBB, 0x51, 0xCE, 0xB4, 0xD8,
    0xEF, 0xC3, 0x4D, 0xC5, 0xA2, 0x2C, 0x36, 0x43
};

#define BMT_OTA_NVS_NAMESPACE         "bmt_ota"
#define BMT_OTA_NVS_KEY_BEACON_KEY    "beacon_key"
/* [v6.4-security] Chu kỳ tự sinh key HMAC beacon MỚI (random) rồi push cho
 * toàn bộ Scanner đã provision qua mesh — giới hạn thời gian sống của 1 key,
 * theo yêu cầu của thầy hướng dẫn. */
#define BMT_OTA_KEY_ROTATE_INTERVAL_MS  (24ULL * 60 * 60 * 1000)

static psa_key_id_t s_beacon_hmac_key_id = 0;
static uint8_t      s_beacon_hmac_key_raw[16];

static void beacon_key_persist(const uint8_t *key)
{
    nvs_handle_t h;
    if (nvs_open(BMT_OTA_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, BMT_OTA_NVS_KEY_BEACON_KEY, key, 16);
    nvs_commit(h);
    nvs_close(h);
}

static esp_err_t beacon_key_import(const uint8_t *key)
{
    if (s_beacon_hmac_key_id != 0) { psa_destroy_key(s_beacon_hmac_key_id); s_beacon_hmac_key_id = 0; }

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attr, 8 * 16);

    psa_status_t st = psa_import_key(&attr, key, 16, &s_beacon_hmac_key_id);
    if (st != PSA_SUCCESS) { ESP_LOGE(TAG, "[SECURITY] psa_import_key failed: %d", (int)st); return ESP_FAIL; }
    memcpy(s_beacon_hmac_key_raw, key, 16);
    return ESP_OK;
}

static void beacon_hmac_key_init(void)
{
    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) { ESP_LOGE(TAG, "[SECURITY] psa_crypto_init failed: %d", (int)st); return; }

    uint8_t nvs_key[16];
    bool    have_nvs_key = false;
    nvs_handle_t h;
    if (nvs_open(BMT_OTA_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(nvs_key);
        if (nvs_get_blob(h, BMT_OTA_NVS_KEY_BEACON_KEY, nvs_key, &len) == ESP_OK && len == sizeof(nvs_key)) {
            have_nvs_key = true;
        }
        nvs_close(h);
    }

    if (have_nvs_key) {
        beacon_key_import(nvs_key);
        ESP_LOGI(TAG, "[SECURITY] OTA-beacon key loaded from NVS (key_id=%" PRIu32 ")", (uint32_t)s_beacon_hmac_key_id);
        return;
    }

    /* Lần boot đầu tiên: sinh key RANDOM thật, không dùng key bootstrap
     * hardcode để verify — bootstrap key chỉ tồn tại trong Scanner cho tới
     * khi Scanner đó được provision + nhận push key thật lần đầu. */
    uint8_t new_key[16];
    esp_fill_random(new_key, sizeof(new_key));
    beacon_key_persist(new_key);
    beacon_key_import(new_key);
    ESP_LOGW(TAG, "[SECURITY] OTA-beacon key: sinh RANDOM lan dau (khong dung key hardcode), key_id=%" PRIu32,
             (uint32_t)s_beacon_hmac_key_id);
    (void)BMT_OTA_BEACON_HMAC_KEY_BOOTSTRAP; /* chỉ để tham chiếu trong comment/tài liệu */
}

/* [v6.4-security] Sinh key HMAC beacon MỚI (random) + lưu NVS + push cho toàn
 * bộ Scanner đã provision qua mesh (vendor message đã được AppKey mã hóa). */
static void beacon_key_rotate_and_push(void)
{
    uint8_t new_key[16];
    esp_fill_random(new_key, sizeof(new_key));
    beacon_key_persist(new_key);
    beacon_key_import(new_key);
    ESP_LOGW(TAG, "[SECURITY] OTA-beacon key ROTATED (key_id=%" PRIu32 ") — pushing to all scanners...",
             (uint32_t)s_beacon_hmac_key_id);

    int pushed = 0;
    for (int i = 0; i < bmt_node_table_capacity(); i++) {
        bmt_node_t *n = bmt_node_table_get(i);
        if (!n || !n->used || !n->is_scan || !n->config_done) continue;
        esp_err_t e = bmt_mesh_publish(n->addr, BMT_OP_VND_OTA_KEY_PUSH, s_beacon_hmac_key_raw, 16);
        if (e == ESP_OK) pushed++;
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGW(TAG, "[SECURITY] Key rotate: pushed to %d scanner(s)", pushed);
}

/* Gọi ngay sau khi 1 scanner hoàn tất provision + config (scan_config_task
 * trong bmt_mesh.c) — đảm bảo scanner mới join luôn có key hiện hành, kể cả
 * khi join sau 1 hoặc nhiều lần rotate đã xảy ra. */
void bmt_ota_push_beacon_key_to_node(uint16_t addr)
{
    if (s_beacon_hmac_key_id == 0) return;
    esp_err_t e = bmt_mesh_publish(addr, BMT_OP_VND_OTA_KEY_PUSH, s_beacon_hmac_key_raw, 16);
    ESP_LOGI(TAG, "[SECURITY] Pushed current beacon key to 0x%04x: %s", addr, e == ESP_OK ? "OK" : esp_err_to_name(e));
}

static void key_rotate_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(BMT_OTA_KEY_ROTATE_INTERVAL_MS));
        beacon_key_rotate_and_push();
    }
}

void bmt_ota_start_key_rotation(void)
{
    xTaskCreate(key_rotate_task, "bmt_key_rot", 3072, NULL, 3, NULL);
}

static uint16_t beacon_hmac16(const uint8_t *data, size_t len)
{
    uint8_t mac[32]; size_t mac_len = 0;
    psa_status_t st = psa_mac_compute(s_beacon_hmac_key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                      data, len, mac, sizeof(mac), &mac_len);
    if (st != PSA_SUCCESS) { ESP_LOGE(TAG, "[SECURITY] psa_mac_compute failed: %d", (int)st); return 0; }
    return (uint16_t)((mac[0] << 8) | mac[1]);
}

/* Beacon format: Flags + Manufacturer Specific (CID Espressif + "BMT" + 0xFA + target + mac16) */
static uint8_t s_beacon_raw[14] = {
    0x02, 0x01, 0x06,        /* Flags: LE General Discoverable */
    0x0A, 0xFF,              /* Manufacturer Specific, length=10 */
    0xE5, 0x02,              /* CID Espressif (little-endian) */
    'B',  'M',  'T',         /* Magic "BMT" */
    0xFA,                    /* Command: OTA trigger */
    0x01,                    /* target_type — fill trước khi gửi */
    0x00, 0x00,              /* mac16 — HMAC-16 fill trước khi gửi */
};

static int adv_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE)
        ESP_LOGI(TAG, "[OTA] beacon adv complete, reason=%d", event->adv_complete.reason);
    return 0;
}

static esp_err_t beacon_send(uint8_t target_type)
{
    s_beacon_raw[11] = target_type;
    uint16_t mac = beacon_hmac16(&s_beacon_raw[7], 5); /* magic(3)+cmd(1)+target(1) */
    s_beacon_raw[12] = (uint8_t)(mac >> 8);
    s_beacon_raw[13] = (uint8_t)(mac & 0xFF);

    int rc = ble_gap_adv_set_data(s_beacon_raw, sizeof(s_beacon_raw));
    if (rc != 0) {
        ESP_LOGE(TAG, "[OTA] ble_gap_adv_set_data FAILED rc=%d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 32;  /* 20ms = 32 * 0.625ms */
    adv_params.itvl_max  = 64;  /* 40ms */

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, adv_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "[OTA] ble_gap_adv_start FAILED rc=%d — mesh host có thể đang tự advertise, "
                 "fallback sang mesh unicast", rc);
        return ESP_FAIL;
    }

    printf("[OTA] NimBLE beacon broadcasting (target=0x%02x, mac16=0x%04x, %ds)...\n",
           target_type, mac, BMT_OTA_BEACON_DURATION_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(BMT_OTA_BEACON_DURATION_MS));
    ble_gap_adv_stop();
    printf("[OTA] NimBLE beacon stopped\n");
    return ESP_OK;
}

/* ============================================================================
 * DISTRIBUTE TASK
 * ============================================================================ */
typedef struct { uint8_t node_type; bool is_scan_filter; } distribute_arg_t;

static void distribute_task(void *arg)
{
    distribute_arg_t d = *(distribute_arg_t *)arg;
    uint8_t     target_type = d.node_type;
    bool        is_scan     = d.is_scan_filter;
    const char *type_str    = is_scan ? "SCANNER" : "RELAY";

    int node_count = 0;
    for (int i = 0; i < bmt_node_table_capacity(); i++) {
        bmt_node_t *n = bmt_node_table_get(i);
        if (!n || !n->used || !n->config_done) continue;
        if (is_scan  && !n->is_scan)  continue;
        if (!is_scan && !n->is_relay) continue;
        node_count++;
    }
    if (node_count == 0) {
        printf("[OTA] No configured %s nodes found\n", type_str);
        s_running = false; vTaskDelete(NULL); return;
    }
    printf("[OTA] Found %d %s node(s)\n", node_count, type_str);

    /* ===== SCANNER: thử 1 beacon broadcast → tất cả scanner OTA gần như đồng thời.
     * Nếu beacon gửi thất bại (NimBLE chưa xác nhận tương thích), fallback sang
     * mesh unicast lần lượt — Scanner đã đăng ký nhận OTA_TRIGGER qua mesh sẵn
     * (dùng cho Relay), nên fallback này không cần sửa gì bên Scanner. ===== */
    if (is_scan) {
        printf("[OTA] Broadcasting NimBLE beacon (%ds) — all %d scanners OTA simultaneously\n",
               BMT_OTA_BEACON_DURATION_MS/1000, node_count);
        if (beacon_send(target_type) == ESP_OK) {
            printf("[OTA] Waiting %ds for all scanners to download + reboot...\n", BMT_OTA_NODE_GAP_MS/1000);
            vTaskDelay(pdMS_TO_TICKS(BMT_OTA_NODE_GAP_MS));
            printf("[OTA] ===== Scanner OTA complete =====\n");
            s_running = false; vTaskDelete(NULL); return;
        }
        printf("[OTA] Beacon broadcast failed — falling back to mesh unicast (chậm hơn nhưng chắc chắn)\n");
    }

    /* ===== RELAY (hoặc SCANNER fallback): mesh unicast lần lượt từng node ===== */
    int idx = 0;
    for (int i = 0; i < bmt_node_table_capacity(); i++) {
        bmt_node_t *n = bmt_node_table_get(i);
        if (!n || !n->used || !n->config_done) continue;
        if (is_scan  && !n->is_scan)  continue;
        if (!is_scan && !n->is_relay) continue;

        idx++;
        printf("\n[OTA] -- %s %d/%d: 0x%04x (%s) --\n", type_str, idx, node_count, n->addr, n->name);

        /* [FIX] Gửi ĐỦ 5 lần liên tiếp không điều kiện (giống main.c gốc) — KHÔNG
         * break khi publish local trả OK, vì esp_ble_mesh_model_publish() chỉ xác
         * nhận gói đã được đưa vào hàng đợi gửi local, không xác nhận Relay/Scanner
         * thực sự nhận được qua sóng. Gửi lặp lại nhiều lần tăng khả năng gói tới
         * đích khi mesh đang bận (nhiều TAG_STATUS dồn dập cùng lúc). */
        for (int retry = 0; retry < 5; retry++) {
            esp_err_t e = bmt_mesh_publish(n->addr, BMT_OP_VND_OTA_TRIGGER, &target_type, sizeof(target_type));
            printf("[OTA] TRIGGER -> 0x%04x [%d/5]: %s\n", n->addr, retry + 1,
                   e == ESP_OK ? "sent" : esp_err_to_name(e));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        printf("[OTA] %s 0x%04x triggered — waiting %ds...\n", type_str, n->addr, BMT_OTA_NODE_GAP_MS/1000);
        vTaskDelay(pdMS_TO_TICKS(BMT_OTA_NODE_GAP_MS));
    }

    printf("\n[OTA] ===== All %s nodes triggered! =====\n", type_str);
    s_running = false;
    vTaskDelete(NULL);
}

static esp_err_t start_distribute(bool is_scan_filter, uint8_t node_type)
{
    if (s_running) { ESP_LOGW(TAG, "OTA already running"); return ESP_ERR_INVALID_STATE; }
    static distribute_arg_t s_arg;   /* static: sống hết vòng đời task, task đọc 1 lần lúc start */
    s_arg.is_scan_filter = is_scan_filter;
    s_arg.node_type      = node_type;
    s_running = true;
    if (xTaskCreate(distribute_task, "bmt_ota_dst", 4096, &s_arg, 4, NULL) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t bmt_ota_trigger_all_scanners(void) { return start_distribute(true,  BMT_NODE_TYPE_SCANNER); }
esp_err_t bmt_ota_trigger_all_relays(void)   { return start_distribute(false, BMT_NODE_TYPE_RELAY); }
bool      bmt_ota_is_running(void)           { return s_running; }

/* Gọi 1 lần lúc boot (main.c) — import key HMAC cho OTA-beacon, cần sẵn sàng
 * trước khi UART/RPC có thể trigger OTA bất kỳ lúc nào */
void bmt_ota_beacon_key_init(void) { beacon_hmac_key_init(); }

/* [v6.3-auto-ota] Đồng bộ với Scanner/Relay — Gateway cũng tự định kỳ kiểm tra
 * version/SHA256 của chính mình, không cần ai bấm 'g'/gửi RPC nữa. Gọi lại
 * đúng bmt_ota_gateway_self_update() nên an toàn nhờ guard s_running sẵn có
 * (không đụng độ nếu đang có OTA relay/scanner distribute chạy cùng lúc, vì
 * chung 1 s_running — self_update sẽ tự lùi lại lần check kế tiếp nếu bận). */
#define BMT_OTA_GATEWAY_AUTO_CHECK_INTERVAL_MS  (3 * 60 * 1000)

static void gateway_auto_check_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(BMT_OTA_GATEWAY_AUTO_CHECK_INTERVAL_MS));
        bmt_ota_gateway_self_update();
    }
}

void bmt_ota_start_auto_check(void)
{
    xTaskCreate(gateway_auto_check_task, "bmt_ota_chk", 3072, NULL, 3, NULL);
}
