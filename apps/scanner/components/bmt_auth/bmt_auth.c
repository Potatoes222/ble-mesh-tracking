#include "bmt_auth.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"

#include "bmt_scan_core.h"

static const char* TAG = "BMT_AUTH";

#define BMT_AUTH_NVS_KEY_OTA_BKN_KEY "ota_bkn_key"

/* [SECURITY] Master key dùng derive epoch key cho Tag — PHẢI GIỐNG HỆT giá
 * trị BMT_TAG_MASTER_KEY trong apps/tag/components/bmt_auth/bmt_auth.c (copy
 * y nguyên, không được lệch dù 1 byte). */
static const uint8_t BMT_TAG_MASTER_KEY[16] = {
    0x2C, 0x5C, 0xBE, 0x42, 0x87, 0xAE, 0x95, 0x4A,
    0xDE, 0xEE, 0x0C, 0x6C, 0x8B, 0x74, 0x9C, 0x45};

/* PHẢI GIỐNG HỆT BMT_EPOCH_TICK_SEC bên Tag. */
#define BMT_EPOCH_TICK_SEC 3600
/* Cửa sổ hẹp quanh epoch dự đoán (bù trôi làm tròn tick) khi tag đã "lock". */
#define BMT_EPOCH_NARROW_WINDOW 1
/* Wide resync khi lần đầu thấy tag / tag mất đồng bộ (thay pin): quét
 * epoch 0..MAX — ứng với tối đa ~8 ngày Tag chạy liên tục trước khi gặp lại
 * bất kỳ scanner nào trong hệ thống. Chỉ tốn kém khi thực sự cần resync,
 * không phải mỗi gói tin. */
#define BMT_EPOCH_WIDE_MAX 200
#define BMT_AUTH_MAX_TAGS 20

typedef struct
{
	bool valid;
	uint16_t tag_id;
	uint16_t locked_epoch;
	int64_t locked_at_us; /* 0 = chưa từng lock */
} bmt_auth_epoch_state_t;

static bmt_auth_epoch_state_t s_epoch_state[BMT_AUTH_MAX_TAGS];
static psa_key_id_t s_tag_master_key_id = 0;

/* [SECURITY] HMAC key RIÊNG cho OTA-beacon từ Gateway — KHÔNG dùng chung
 * key với Tag. PHẢI GIỐNG HỆT BMT_OTA_BEACON_HMAC_KEY trong Gateway/main/main.c. */
static const uint8_t BMT_OTA_BEACON_HMAC_KEY[16] = {
    0x5A, 0x59, 0xD8, 0xBB, 0x51, 0xCE, 0xB4, 0xD8,
    0xEF, 0xC3, 0x4D, 0xC5, 0xA2, 0x2C, 0x36, 0x43};

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
	s_tag_master_key_id = import_hmac_key(BMT_TAG_MASTER_KEY, sizeof(BMT_TAG_MASTER_KEY), "tag-master");

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

/* Derive key cho 1 epoch cụ thể từ master key rồi thử verify — dùng chung
 * cho cả narrow-window (fast path) lẫn wide resync (cold start / thay pin).
 * Import/destroy key tạm mỗi lần gọi để không rò slot PSA key. */
static bool try_epoch(uint16_t epoch, const uint8_t* data, size_t len, uint16_t received_mac)
{
	uint8_t derived[32];
	size_t derived_len = 0;
	uint8_t epoch_le[2] = {(uint8_t)(epoch & 0xFF), (uint8_t)(epoch >> 8)};

	if (psa_mac_compute(s_tag_master_key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
	                    epoch_le, sizeof(epoch_le), derived, sizeof(derived), &derived_len) != PSA_SUCCESS)
		return false;

	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
	psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&attr, 128);

	psa_key_id_t tmp_id = 0;
	if (psa_import_key(&attr, derived, 16, &tmp_id) != PSA_SUCCESS)
		return false;

	bool ok = (hmac16(tmp_id, data, len) == received_mac);
	psa_destroy_key(tmp_id);
	return ok;
}

static bmt_auth_epoch_state_t* find_or_alloc_epoch_state(uint16_t tag_id)
{
	int free_idx = -1;
	for (int i = 0; i < BMT_AUTH_MAX_TAGS; i++)
	{
		if (s_epoch_state[i].valid && s_epoch_state[i].tag_id == tag_id)
			return &s_epoch_state[i];
		if (!s_epoch_state[i].valid && free_idx < 0)
			free_idx = i;
	}
	if (free_idx < 0)
		return NULL; /* het cho -> wide resync moi lan, khong luu state */

	s_epoch_state[free_idx].valid = true;
	s_epoch_state[free_idx].tag_id = tag_id;
	s_epoch_state[free_idx].locked_epoch = 0;
	s_epoch_state[free_idx].locked_at_us = 0;
	return &s_epoch_state[free_idx];
}

/* [SECURITY] Tag khong co mesh/WiFi nen khong nhan duoc key rotate day
 * xuong — thay vao do Tag+Scanner cung xoay key theo epoch thoi gian cuc
 * bo (TOTP-style, receiver-less). Scanner khong biet chac epoch hien tai
 * cua Tag (Tag reset epoch ve 0 moi lan mat nguon/thay pin) nen phai:
 *  1) Fast path: neu da "lock" epoch cua tag nay tu truoc, du doan epoch
 *     hien tai = epoch da lock + so tick da troi qua, thu cua so hep [-1,+1].
 *  2) Neu fast path fail (vd Tag vua thay pin -> epoch cuc bo nhay ve gan 0,
 *     khong con lien he voi du doan cu) -> wide resync: quet 0..WIDE_MAX. */
bool bmt_auth_verify_tag(uint16_t tag_id, const uint8_t* data, int len, uint16_t received_mac)
{
	bmt_auth_epoch_state_t* st = find_or_alloc_epoch_state(tag_id);
	int64_t now_us = esp_timer_get_time();

	if (st && st->locked_at_us > 0)
	{
		int64_t elapsed_ticks = (now_us - st->locked_at_us) / (1000000LL * BMT_EPOCH_TICK_SEC);
		uint16_t predicted = (uint16_t)(st->locked_epoch + elapsed_ticks);

		for (int d = -BMT_EPOCH_NARROW_WINDOW; d <= BMT_EPOCH_NARROW_WINDOW; d++)
		{
			uint16_t cand = (uint16_t)(predicted + d);
			if (try_epoch(cand, data, (size_t)len, received_mac))
			{
				st->locked_epoch = cand;
				st->locked_at_us = now_us;
				return true;
			}
		}
		ESP_LOGW(TAG, "Tag 0x%04X: narrow-window auth fail (predicted epoch=%u) -> wide resync",
		         tag_id, predicted);
	}

	for (uint16_t e = 0; e <= BMT_EPOCH_WIDE_MAX; e++)
	{
		if (try_epoch(e, data, (size_t)len, received_mac))
		{
			if (st)
			{
				st->locked_epoch = e;
				st->locked_at_us = now_us;
			}
			ESP_LOGW(TAG, "Tag 0x%04X: wide resync OK -> epoch=%u", tag_id, e);
			return true;
		}
	}

	return false;
}

int32_t bmt_auth_get_locked_epoch(uint16_t tag_id)
{
	for (int i = 0; i < BMT_AUTH_MAX_TAGS; i++)
		if (s_epoch_state[i].valid && s_epoch_state[i].tag_id == tag_id && s_epoch_state[i].locked_at_us > 0)
			return (int32_t)s_epoch_state[i].locked_epoch;
	return -1;
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
