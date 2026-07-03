#pragma once

#include "esp_err.h"

esp_err_t bmt_mesh_init(void);
void      bmt_mesh_start_relay_ping(void);

extern const uint8_t g_bmt_net_key[16];
extern const uint8_t g_bmt_app_key[16];
