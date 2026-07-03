#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_config.h"
#include "bmt_node_table.h"
#include "bmt_types.h"
#include "bmt_zone.h"

void bmt_tb_connect_device(const char *device_name, const char *device_type);
void bmt_tb_disconnect_device(const char *device_name);
void bmt_tb_set_role(const char *device_name, const char *role);

void bmt_tb_pub_gateway_online(void);
void bmt_tb_pub_node_status(uint16_t addr, const char *role, bool online);
void bmt_tb_pub_tag_report(const bmt_tag_report_t *r);
void bmt_tb_pub_node_health(uint16_t src, const bmt_node_health_t *h);
