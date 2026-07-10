#pragma once

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * BMT_AUTH — HMAC-16 cho Tag ADV payload  [thay CRC16 của bản gốc]
 * ----------------------------------------------------------------------------
 * CRC16 chỉ chống lỗi nhiễu ngẫu nhiên, không chống giả mạo (ai cũng tính lại
 * được vì không cần khóa bí mật). HMAC-SHA256 (rút gọn 2 byte đầu) yêu cầu
 * biết đúng key mới tính ra giá trị đúng — chống giả mạo trong khi vẫn giữ
 * khả năng phát hiện lỗi truyền như CRC cũ (nhờ avalanche effect SHA-256).
 * ⚠️ Key PHẢI GIỐNG HỆT giá trị BMT_TAG_HMAC_KEY trong Scanner/main/bmt_auth.c
 * ============================================================================ */

/* Gọi 1 lần lúc boot, TRƯỚC lần build_adv_data() đầu tiên */
void bmt_auth_init(void);

/* Tính HMAC-16 trên payload — dùng khi build gói ADV mỗi lần sequence++ */
uint16_t bmt_auth_hmac16(const uint8_t* data, size_t len);
