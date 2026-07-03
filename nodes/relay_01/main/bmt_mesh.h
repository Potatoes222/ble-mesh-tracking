#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t bmt_mesh_init(void);

void bmt_mesh_reset(void);
void bmt_mesh_print_status(const char *title);
void bmt_mesh_start_monitor(void);

uint16_t bmt_mesh_get_node_addr(void);
