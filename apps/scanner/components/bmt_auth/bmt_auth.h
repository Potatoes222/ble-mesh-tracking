#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Gọi 1 lần lúc boot, TRƯỚC khi bmt_scan bắt đầu nhận/verify bất kỳ ADV nào */
void bmt_auth_init(void);

/* Verify payload Tag ADV — dùng epoch key derive từ BMT_TAG_MASTER_KEY, tự
 * đoán + windowed resync (xem giải thích đầy đủ trong bmt_auth.c). tag_id
 * lấy từ p.minor trong payload, dùng để tra state đồng bộ epoch riêng của
 * từng tag. */
bool bmt_auth_verify_tag(uint16_t tag_id, const uint8_t* data, int len, uint16_t received_mac);

/* Epoch hiện Scanner đang "lock" cho tag này (dùng cho hiển thị/debug) —
 * trả về -1 nếu chưa từng lock (chưa thấy tag hoặc mới add xong chưa verify
 * lần nào thành công). */
int32_t bmt_auth_get_locked_epoch(uint16_t tag_id);

/* Tính HMAC cho payload OTA-beacon — dùng key hiện tại (NVS nếu đã nhận rotate
 * từ Gateway, mặc định hardcode nếu chưa từng nhận), so sánh thủ công ở caller */
uint16_t bmt_auth_ota_beacon_hmac16(const uint8_t* data, size_t len);

/* [SECURITY] Gateway push key HMAC beacon mới qua mesh (đã được AppKey mã hóa
 * sẵn ở transport layer) — thay key đang dùng + lưu NVS để sống sót qua reboot. */
void bmt_auth_set_ota_beacon_key(const uint8_t* key, size_t len);
