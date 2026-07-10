#include "bmt_auth.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "psa/crypto.h"

#include "bmt_scan_core.h"

static const char* TAG = "BMT_AUTH";

#define BMT_AUTH_NVS_KEY_OTA_BKN_KEY "ota_bkn_key"

/* [SECURITY] HMAC pre-shared key — PHẢI GIỐNG HỆT giá trị trong Tag/main/main.c
 * (copy y nguyên, không được lệch dù 1 byte). */
static const uint8_t BMT_TAG_HMAC_KEY[16] = {
    0x2C, 0x5C, 0xBE, 0x42, 0x87, 0xAE, 0x95, 0x4A,
    0xDE, 0xEE, 0x0C, 0x6C, 0x8B, 0x74, 0x9C, 0x45};

/* [SECURITY] HMAC key RIÊNG cho OTA-beacon từ Gateway — KHÔNG dùng chung
 * key với Tag. PHẢI GIỐNG HỆT BMT_OTA_BEACON_HMAC_KEY trong Gateway/main/main.c. */
static const uint8_t BMT_OTA_BEACON_HMAC_KEY[16] = {
    0x5A, 0x59, 0xD8, 0xBB, 0x51, 0xCE, 0xB4, 0xD8,
    0xEF, 0xC3, 0x4D, 0xC5, 0xA2, 0x2C, 0x36, 0x43};

static psa_key_id_t s_tag_key_id = 0;
static psa_key_id_t s_ota_beacon_key_id = 0;
static psa_key_id_t import_hmac_key(const uint8_t* key, size_t key_len, const char* label)
{
	psa_key_id_t key_id = 0;
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
	psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&attr, 8 * key_len);

	psa_status_t st = psa_import_key(&attr, key, key_len, &key_id);
	if (st != PSA_SUCCESS)
	{
		ESP_LOGE(TAG, "psa_import_key (%s) failed: %d", label, (int)st);
		return 0;
	}
	ESP_LOGI(TAG, "HMAC key imported OK (%s, key_id=%" PRIu32 ")", label, (uint32_t)key_id);
	return key_id;
}

void bmt_auth_init(void)
{
	psa_status_t st = psa_crypto_init();
	if (st != PSA_SUCCESS)
	{
		ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int)st);
		return;
	}
	s_tag_key_id = import_hmac_key(BMT_TAG_HMAC_KEY, sizeof(BMT_TAG_HMAC_KEY), "tag");

	uint8_t nvs_key[16];
	bool have_nvs_key = false;
	nvs_handle_t h;
	if (nvs_open(BMT_SCAN_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK)
	{
		size_t len = sizeof(nvs_key);
		if (nvs_get_blob(h, BMT_AUTH_NVS_KEY_OTA_BKN_KEY, nvs_key, &len) == ESP_OK && len == sizeof(nvs_key))
		{
			have_nvs_key = true;
		}
		nvs_close(h);
	}

	if (have_nvs_key)
	{
		ESP_LOGI(TAG, "[SECURITY] OTA-beacon key loaded from NVS (da tung rotate)");
		s_ota_beacon_key_id = import_hmac_key(nvs_key, sizeof(nvs_key), "ota-beacon");
	}
	else
	{
		ESP_LOGW(TAG, "[SECURITY] OTA-beacon key: chua co ban rotate nao, dung tam key mac dinh");
		s_ota_beacon_key_id = import_hmac_key(BMT_OTA_BEACON_HMAC_KEY, sizeof(BMT_OTA_BEACON_HMAC_KEY), "ota-beacon");
	}
}

static uint16_t hmac16(psa_key_id_t key_id, const uint8_t* data, size_t len)
{
	uint8_t mac[32];
	size_t mac_len = 0;

	psa_status_t st = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
	                                  data, len, mac, sizeof(mac), &mac_len);
	if (st != PSA_SUCCESS)
	{
		ESP_LOGE(TAG, "psa_mac_compute failed: %d", (int)st);
		return 0;
	}
	return (uint16_t)((mac[0] << 8) | mac[1]);
}

bool bmt_auth_verify_tag(const uint8_t* data, int len, uint16_t received_mac)
{
	return hmac16(s_tag_key_id, data, (size_t)len) == received_mac;
}

uint16_t bmt_auth_ota_beacon_hmac16(const uint8_t* data, size_t len)
{
	return hmac16(s_ota_beacon_key_id, data, len);
}

void bmt_auth_set_ota_beacon_key(const uint8_t* key, size_t len)
{
	if (len != 16)
	{
		ESP_LOGE(TAG, "[SECURITY] rotate key co do dai sai (%u), bo qua", (unsigned)len);
		return;
	}

	psa_key_id_t new_id = import_hmac_key(key, len, "ota-beacon-rotated");
	if (new_id == 0)
		return;

	if (s_ota_beacon_key_id != 0)
		psa_destroy_key(s_ota_beacon_key_id);
	s_ota_beacon_key_id = new_id;

	nvs_handle_t h;
	if (nvs_open(BMT_SCAN_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK)
	{
		nvs_set_blob(h, BMT_AUTH_NVS_KEY_OTA_BKN_KEY, key, len);
		nvs_commit(h);
		nvs_close(h);
	}
	ESP_LOGW(TAG, "[SECURITY] OTA-beacon key ROTATED (nhan tu Gateway qua mesh) va da luu NVS");
}
