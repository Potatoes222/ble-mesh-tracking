#pragma once

#include <stdbool.h>
#include <stdint.h>

void bmt_mac_cache_store(const uint8_t *uuid, const uint8_t *mac);
bool bmt_mac_cache_get(const uint8_t *uuid, uint8_t *mac_out);
