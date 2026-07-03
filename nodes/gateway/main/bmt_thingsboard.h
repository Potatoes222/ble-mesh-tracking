#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bmt_types.h"

void bmt_tb_connect_device(const char *device_name, const char *device_type);
void bmt_tb_disconnect_device(const char *device_name);
void bmt_tb_set_role(const char *device_name, const char *role);

void bmt_tb_pub_gateway_online(void);
void bmt_tb_pub_node_status(uint16_t addr, const char *role, bool online);
void bmt_tb_pub_tag_report(const bmt_tag_report_t *r);
void bmt_tb_pub_node_health(uint16_t src, const bmt_node_health_t *h);
