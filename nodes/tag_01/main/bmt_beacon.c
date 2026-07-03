#include "bmt_beacon.h"
#include "bmt_config.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "BMT_BEACON";

#pragma pack(1)
typedef struct {
    uint8_t  uuid[16];
    uint16_t major;
    uint16_t minor;
    int8_t   tx_power;
    uint8_t  sequence;
    uint16_t crc16;
} bmt_adv_payload_t;
#pragma pack()

#define BMT_ADV_RAW_LEN     31
#define BMT_ADV_PAYLOAD_OFF 7

static uint8_t s_adv_raw[BMT_ADV_RAW_LEN] = {
    0x02, 0x01, 0x06, 0x1B, 0xFF, 0xE5, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t       s_sequence   = 0;
static TimerHandle_t s_seq_timer  = NULL;
static bool          s_adv_active = false;

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0,
    .adv_int_max       = 0,
    .adv_type          = ADV_TYPE_NONCONN_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr_type    = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint16_t crc16_ccitt(const uint8_t *data, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

static void build_adv_data(void) {
    bmt_adv_payload_t p;
    memcpy(p.uuid, BMT_SYSTEM_UUID, 16);
    p.major    = BMT_TAG_MAJOR;
    p.minor    = BMT_TAG_MINOR;
    p.tx_power = BMT_TAG_TX_POWER_REF;
    p.sequence = s_sequence;
    p.crc16    = 0;
    p.crc16    = crc16_ccitt((uint8_t *)&p, sizeof(p) - sizeof(p.crc16));

    memcpy(s_adv_raw + BMT_ADV_PAYLOAD_OFF, &p, sizeof(p));
}

static void start_adv_random_interval(void) {
    uint32_t span  = BMT_ADV_INTERVAL_MAX_MS - BMT_ADV_INTERVAL_MIN_MS + 1;
    uint32_t ms    = BMT_ADV_INTERVAL_MIN_MS + (esp_random() % span);
    uint16_t units = (uint16_t)((ms * 1000) / 625);

    s_adv_params.adv_int_min = units;
    s_adv_params.adv_int_max = units;

    build_adv_data();
    esp_ble_gap_config_adv_data_raw(s_adv_raw, BMT_ADV_RAW_LEN);
}

static void seq_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    s_sequence++;
    esp_ble_gap_stop_advertising();
    start_adv_random_interval();
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS)
            esp_ble_gap_start_advertising(&s_adv_params);
        else
            ESP_LOGE(TAG, "ADV data set FAILED");
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_active = true;
            ESP_LOGD(TAG, "ADV OK seq=%u", s_sequence);
        } else {
            ESP_LOGE(TAG, "ADV start FAILED");
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_adv_active = false;
        break;

    default:
        break;
    }
}

static esp_err_t bluetooth_init(void) {
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BMT_TAG_RADIO_PWR);
    return ESP_OK;
}

esp_err_t bmt_beacon_init(void) {
    ESP_ERROR_CHECK(bluetooth_init());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

    s_seq_timer = xTimerCreate("seq", pdMS_TO_TICKS(BMT_SEQ_TICK_MS), pdTRUE, NULL, seq_timer_cb);
    if (!s_seq_timer) return ESP_ERR_NO_MEM;
    xTimerStart(s_seq_timer, 0);

    start_adv_random_interval();
    return ESP_OK;
}

uint8_t bmt_beacon_get_sequence(void) {
    return s_sequence;
}

uint16_t bmt_beacon_get_crc16(void) {
    const bmt_adv_payload_t *p = (const bmt_adv_payload_t *)(s_adv_raw + BMT_ADV_PAYLOAD_OFF);
    return p->crc16;
}

bool bmt_beacon_is_active(void) {
    return s_adv_active;
}
