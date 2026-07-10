#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * BMT_AUTH — HMAC-16 cho Tag ADV + OTA-beacon  [thay bmt_crc.c của bản gốc]
 * ----------------------------------------------------------------------------
 * CRC16 chỉ chống lỗi nhiễu ngẫu nhiên, không chống giả mạo (ai cũng tính lại
 * được vì không cần khóa bí mật). HMAC-SHA256 (rút gọn 2 byte đầu) yêu cầu
 * biết đúng key mới verify được — chống giả mạo trong khi vẫn giữ khả năng
 * phát hiện lỗi truyền như CRC cũ (nhờ avalanche effect của SHA-256).
 *
 * 2 key TÁCH BIỆT, không dùng chung:
 *   - Tag key: verify payload Tag ADV (Tag → Scanner)
 *   - OTA-beacon key: verify beacon OTA-trigger (Gateway → Scanner)
 * Tách để 1 key lộ không ảnh hưởng kênh còn lại.
 * ============================================================================ */

/* Gọi 1 lần lúc boot, TRƯỚC khi bmt_scan bắt đầu nhận/verify bất kỳ ADV nào */
void bmt_auth_init(void);

/* Verify payload Tag ADV — dùng BMT_TAG_HMAC_KEY */
bool bmt_auth_verify_tag(const uint8_t* data, int len, uint16_t received_mac);

/* Tính HMAC cho payload OTA-beacon — dùng key hiện tại (NVS nếu đã nhận rotate
 * từ Gateway, mặc định hardcode nếu chưa từng nhận), so sánh thủ công ở caller */
uint16_t bmt_auth_ota_beacon_hmac16(const uint8_t* data, size_t len);

/* [SECURITY] Gateway push key HMAC beacon mới qua mesh (đã được AppKey mã hóa
 * sẵn ở transport layer) — thay key đang dùng + lưu NVS để sống sót qua reboot. */
void bmt_auth_set_ota_beacon_key(const uint8_t* key, size_t len);
