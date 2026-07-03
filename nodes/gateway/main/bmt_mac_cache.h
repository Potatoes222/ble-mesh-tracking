#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bmt_config.h"

typedef struct {
    bool    used;
    uint8_t uuid[16];
    uint8_t mac[6];
} bmt_mac_entry_t;

void bmt_mac_cache_store(const uint8_t *uuid, const uint8_t *mac);
bool bmt_mac_cache_get(const uint8_t *uuid, uint8_t *mac_out);
