#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bmt_types.h"

esp_err_t bmt_mesh_init(void);

bool     bmt_mesh_is_provisioned(void);
bool     bmt_mesh_is_ready(void);
uint16_t bmt_mesh_node_addr(void);
uint16_t bmt_mesh_app_idx(void);
uint16_t bmt_mesh_net_idx(void);

esp_err_t bmt_mesh_publish_tag_report(const bmt_tag_report_t *r);
esp_err_t bmt_mesh_publish_node_health(const bmt_node_health_t *h);

void bmt_mesh_reset(void);
