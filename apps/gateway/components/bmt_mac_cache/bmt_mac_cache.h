#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Temporary UUID -> MAC cache used during scan-before-provision. The
 * PROV_COMPLETE event does not carry the MAC, so we look it up here
 * from what RECV_UNPROV_ADV_PKT recorded earlier. */
void bmt_mac_cache_store(const uint8_t* uuid, const uint8_t* mac);
bool bmt_mac_cache_get(const uint8_t* uuid, uint8_t* mac_out);
