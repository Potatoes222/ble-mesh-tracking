#pragma once

#include <stdint.h>

#include "bmt_types.h"

void bmt_tb_pub_gateway_online(void);
void bmt_tb_pub_node_status(uint16_t addr, const char* role, bool online);
void bmt_tb_pub_tag_report(const bmt_tag_report_t* r, const uint8_t* scanner_mac);

/* Gọi từ bmt_mesh.c khi nhận OTA_RESULT từ node */
void bmt_tb_pub_ota_result(uint16_t addr, uint8_t status);

/* Task quét định kỳ, đánh dấu tag OUT_OF_RANGE nếu quá lâu không có report
 * từ scanner nào — publish zone=out_of_range lên ThingsBoard */
void bmt_tb_start_zone_timeout_task(void);
