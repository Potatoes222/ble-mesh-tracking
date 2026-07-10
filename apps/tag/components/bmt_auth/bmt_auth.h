#pragma once

#include <stddef.h>
#include <stdint.h>

/* Gọi 1 lần lúc boot, TRƯỚC lần build_adv_data() đầu tiên */
void bmt_auth_init(void);

/* Tính HMAC-16 trên payload — dùng khi build gói ADV mỗi lần sequence++ */
uint16_t bmt_auth_hmac16(const uint8_t* data, size_t len);
