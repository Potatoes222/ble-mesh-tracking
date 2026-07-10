#include "bmt_beacon.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

#include "bmt_config.h"
#include "bmt_auth.h"

static const char* TAG = "BMT_BEACON";

/* ============================================================================
 * GLOBALS
 * ============================================================================ */
static uint8_t s_sequence = 0;
static TimerHandle_t s_seq_timer = NULL;
static bool s_adv_active = false;

/* Raw ADV buffer — 31 bytes (maximum BLE ADV payload)
 *
 * Layout:
 *   [0..2]   Flags:   02 01 06
 *   [3]      Mfr len: 0x1B = 27 (= 1 type + 2 CID + 24 payload)
 *   [4]      Type:    0xFF (Manufacturer Specific)
 *   [5..6]   CID:     E5 02 (Espressif, little-endian)
 *   [7..30]  Payload: bmt_tag_adv_payload_t (24 bytes)
 */
#define ADV_RAW_LEN 31
#define ADV_PAYLOAD_OFF 7

static uint8_t s_adv_raw[ADV_RAW_LEN] = {
    /* Flags */
    0x02,
    0x01,
    0x06,
    /* Manufacturer Specific Data header */
    0x1B,
    0xFF,
    0xE5,
    0x02,
    /* 24 bytes payload (filled by build_adv_data) */
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

/* ============================================================================
 * BUILD ADV DATA
 * ============================================================================ */
static void build_adv_data(void)
{
	bmt_tag_adv_payload_t p;

	memcpy(p.uuid, BMT_SYSTEM_UUID, 16);
	p.major = BMT_TAG_MAJOR;
	p.minor = BMT_TAG_MINOR;
	p.tx_power = BMT_TAG_TX_POWER;
	p.sequence = s_sequence;
	p.mac16 = 0; /* bắt buộc = 0 trước khi tính HMAC */

	/* HMAC tính trên tất cả fields trừ 2 bytes mac16 cuối */
	p.mac16 = bmt_auth_hmac16((uint8_t*)&p, sizeof(p) - sizeof(p.mac16));

	memcpy(s_adv_raw + ADV_PAYLOAD_OFF, &p, sizeof(p));
}

/* ============================================================================
 * ADV PARAMS & START
 * ============================================================================ */
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0,
    .adv_int_max = 0,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void start_adv_random_interval(void)
{
	/* Random [450, 550] ms → tránh collision nhiều tag */
	uint32_t ms = BMT_ADV_INTERVAL_MIN_MS + (esp_random() % (BMT_ADV_INTERVAL_MAX_MS - BMT_ADV_INTERVAL_MIN_MS + 1));

	/* BLE unit = 0.625ms */
	uint16_t units = (uint16_t)((ms * 1000) / 625);
	s_adv_params.adv_int_min = units;
	s_adv_params.adv_int_max = units;

	build_adv_data();

	/* config_adv_data_raw → GAP callback → start_advertising */
	esp_ble_gap_config_adv_data_raw(s_adv_raw, ADV_RAW_LEN);
}

/* ============================================================================
 * SEQUENCE TIMER
 *
 * Non-connectable ADV (ADV_NONCONN_IND) không trigger ADV_STOP_COMPLETE_EVT
 * → không thể increment sequence trong callback
 * → dùng FreeRTOS timer ~500ms để increment + restart ADV
 * ============================================================================ */
static void seq_timer_cb(TimerHandle_t xTimer)
{
	(void)xTimer;
	s_sequence++; /* uint8_t: 255 → 0 tự động */

	esp_ble_gap_stop_advertising();
	start_adv_random_interval();
}

/* ============================================================================
 * GAP CALLBACK
 * ============================================================================ */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t* param)
{
	switch (event)
	{
	case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
		if (param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS)
			esp_ble_gap_start_advertising(&s_adv_params);
		else
			ESP_LOGE(TAG, "ADV data set FAILED");
		break;

	case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
		if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
		{
			s_adv_active = true;
			ESP_LOGD(TAG, "ADV OK seq=%u major=0x%04X minor=0x%04X",
			         s_sequence, BMT_TAG_MAJOR, BMT_TAG_MINOR);
		}
		else
		{
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

/* ============================================================================
 * BT INIT
 * ============================================================================ */
static esp_err_t bluetooth_init(void)
{
	ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
	ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

	esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&cfg));
	ESP_ERROR_CHECK(esp_bluedroid_enable());

	/* Set radio TX power — ảnh hưởng range và pin */
	esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BMT_TAG_RADIO_PWR);

	return ESP_OK;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */
esp_err_t bmt_beacon_start(void)
{
	esp_err_t err = bluetooth_init();
	if (err != ESP_OK)
		return err;

	err = esp_ble_gap_register_callback(gap_event_handler);
	if (err != ESP_OK)
		return err;

	/* Sequence timer: 500ms auto-reload
	 * Mỗi lần fire: sequence++, restart ADV với interval mới */
	s_seq_timer = xTimerCreate("seq", pdMS_TO_TICKS(500),
	                           pdTRUE, NULL, seq_timer_cb);
	xTimerStart(s_seq_timer, 0);

	/* Bắt đầu advertise */
	start_adv_random_interval();

	return ESP_OK;
}

uint8_t bmt_beacon_sequence(void)
{
	return s_sequence;
}
bool bmt_beacon_is_active(void)
{
	return s_adv_active;
}

uint16_t bmt_beacon_last_mac16(void)
{
	const bmt_tag_adv_payload_t* p =
	    (const bmt_tag_adv_payload_t*)(s_adv_raw + ADV_PAYLOAD_OFF);
	return p->mac16;
}
