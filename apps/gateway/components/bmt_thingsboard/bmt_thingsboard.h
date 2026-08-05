#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bmt_types.h"

void bmt_tb_pub_gateway_online(void);
void bmt_tb_pub_gateway_ota_result(bool success);
void bmt_tb_pub_node_status(uint16_t addr, const uint8_t* mac, const char* role, bool online);
void bmt_tb_pub_tag_report(const bmt_tag_report_t* r, const uint8_t* scanner_mac);
void bmt_tb_pub_ota_result(uint16_t addr, uint8_t status);
void bmt_tb_start_zone_timeout_task(void);
void bmt_tb_reconnect_all_devices(void);
