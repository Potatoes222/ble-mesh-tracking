#include "bmt_auth.h"

#include <inttypes.h>

#include "esp_log.h"
#include "psa/crypto.h"

static const char *TAG = "BMT_AUTH";

/* [SECURITY] HMAC pre-shared key — PHẢI GIỐNG HỆT giá trị trong
 * Scanner/main/bmt_auth.c (copy y nguyên, không được lệch dù 1 byte).
 * Giá trị dưới đây được sinh ngẫu nhiên bằng CSPRNG (python secrets.token_bytes),
 * KHÔNG phải pattern dễ đoán. Nếu cần đổi key mới, sinh lại và dán vào cả
 * 2 file (Tag + Scanner), rồi flash lại cả 2 phía. */
static const uint8_t BMT_TAG_HMAC_KEY[16] = {
    0x2C, 0x5C, 0xBE, 0x42, 0x87, 0xAE, 0x95, 0x4A,
    0xDE, 0xEE, 0x0C, 0x6C, 0x8B, 0x74, 0x9C, 0x45
};

static psa_key_id_t s_key_id = 0;

/* [FIX] mbedtls 4 (ESP-IDF v6.0.1) đã bỏ các hàm tiện ích mbedtls_md_hmac_*
 * → dùng PSA Crypto API chuẩn. Key import 1 LẦN lúc khởi động, tái sử dụng
 * key_id cho mọi lần tính HMAC sau đó (tránh import/destroy lặp lại mỗi
 * 500ms mỗi lần build_adv_data). */
void bmt_auth_init(void)
{
    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int)st);
        return;
    }

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attr, 8 * sizeof(BMT_TAG_HMAC_KEY));

    st = psa_import_key(&attr, BMT_TAG_HMAC_KEY, sizeof(BMT_TAG_HMAC_KEY), &s_key_id);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key failed: %d", (int)st);
        return;
    }
    ESP_LOGI(TAG, "HMAC key imported OK (key_id=%" PRIu32 ")", (uint32_t)s_key_id);
}

uint16_t bmt_auth_hmac16(const uint8_t *data, size_t len)
{
    uint8_t mac[32];
    size_t  mac_len = 0;

    psa_status_t st = psa_mac_compute(s_key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                      data, len, mac, sizeof(mac), &mac_len);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_mac_compute failed: %d", (int)st);
        return 0;
    }
    return (uint16_t)((mac[0] << 8) | mac[1]);
}
