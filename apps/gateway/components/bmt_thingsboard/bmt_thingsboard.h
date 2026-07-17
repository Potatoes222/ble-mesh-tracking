#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bmt_types.h"

void bmt_tb_pub_gateway_online(void);

/* [ADD] Gateway tu bao cao ket qua OTA CUA CHINH NO (khac voi
 * bmt_tb_pub_ota_result — cai do la cho Scanner/Relay bao ve qua mesh).
 * Publish qua v1/devices/me/telemetry (cung dang {"ota_result": "SUCCESS"|
 * "FAILED"} nhu Scanner/Relay) de rule chain ble_mesh_node_ota (dang gan cho
 * ca profile "default" ma Gateway thuoc ve) tu chuyen thanh attribute
 * ota_last_result/ota_last_time — khong can rule chain rieng. */
void bmt_tb_pub_gateway_ota_result(bool success);
void bmt_tb_pub_node_status(uint16_t addr, const uint8_t* mac, const char* role, bool online);
void bmt_tb_pub_tag_report(const bmt_tag_report_t* r, const uint8_t* scanner_mac);

/* Gọi từ bmt_mesh.c khi nhận OTA_RESULT từ node */
void bmt_tb_pub_ota_result(uint16_t addr, uint8_t status);

/* Task quét định kỳ, đánh dấu tag OUT_OF_RANGE nếu quá lâu không có report
 * từ scanner nào — publish zone=out_of_range lên ThingsBoard */
void bmt_tb_start_zone_timeout_task(void);
