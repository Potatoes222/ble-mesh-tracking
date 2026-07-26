#pragma once

#include <stddef.h>
#include <stdint.h>

/* Goi 1 lan luc boot, TRUOC lan build_adv_data() dau tien */
void bmt_auth_init(void);

/* Tinh HMAC-16 tren payload — dung khi build goi ADV moi lan sequence++ */
uint16_t bmt_auth_hmac16(const uint8_t* data, size_t len);
