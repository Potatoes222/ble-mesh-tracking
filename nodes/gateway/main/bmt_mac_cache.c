#include "bmt_mac_cache.h"
#include "bmt_config.h"

#include <string.h>

typedef struct {
    bool    used;
    uint8_t uuid[16];
    uint8_t mac[6];
} entry_t;

static entry_t s_cache[BMT_MAX_MAC_CACHE];

void bmt_mac_cache_store(const uint8_t *uuid, const uint8_t *mac) {
    for (int i = 0; i < BMT_MAX_MAC_CACHE; i++) {
        if (s_cache[i].used && memcmp(s_cache[i].uuid, uuid, 16) == 0) {
            memcpy(s_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < BMT_MAX_MAC_CACHE; i++) {
        if (!s_cache[i].used) {
            s_cache[i].used = true;
            memcpy(s_cache[i].uuid, uuid, 16);
            memcpy(s_cache[i].mac, mac, 6);
            return;
        }
    }
}

bool bmt_mac_cache_get(const uint8_t *uuid, uint8_t *mac_out) {
    for (int i = 0; i < BMT_MAX_MAC_CACHE; i++) {
        if (s_cache[i].used && memcmp(s_cache[i].uuid, uuid, 16) == 0) {
            memcpy(mac_out, s_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}
