#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Cache tạm UUID→MAC lúc quét chưa provision — vì event PROV_COMPLETE không
 * kèm MAC, phải tra lại cache được ghi lúc RECV_UNPROV_ADV_PKT trước đó */
void bmt_mac_cache_store(const uint8_t *uuid, const uint8_t *mac);
bool bmt_mac_cache_get(const uint8_t *uuid, uint8_t *mac_out);
