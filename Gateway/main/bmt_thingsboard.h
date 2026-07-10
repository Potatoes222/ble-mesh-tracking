#pragma once

#include <stdint.h>

#include "bmt_types.h"

void bmt_tb_pub_gateway_online(void);
void bmt_tb_pub_node_status(uint16_t addr, const char *role, bool online);

/* [v6.5-relay-only] Gọi từ bmt_mqtt.c (worker task) sau khi dequeue. Gateway
 * KHÔNG còn tự tính/publish zone — chỉ relay {scanner MAC, rssi, distance,
 * loss} thô lên ThingsBoard, rule chain bên đó tự quyết định zone (theo yêu
 * cầu "Gateway chỉ trung chuyển"). Vẫn cập nhật bmt_zone.h cục bộ CHỈ để phục
 * vụ UART lệnh '2' debug tại chỗ, không dùng giá trị đó để publish nữa.
 * scanner_mac: NULL nếu Gateway tra không ra MAC (scanner chưa provision xong
 * lúc nhận được report này) — khi đó dùng report->scanner_id (hex) làm fallback. */
void bmt_tb_pub_tag_report(const bmt_tag_report_t *r, const uint8_t *scanner_mac);

/* Gọi từ bmt_mesh.c khi nhận OTA_RESULT từ node */
void bmt_tb_pub_ota_result(uint16_t addr, uint8_t status);

/* Task quét định kỳ, đánh dấu tag OUT_OF_RANGE nếu quá lâu không có report
 * từ scanner nào — publish zone=out_of_range lên ThingsBoard */
void bmt_tb_start_zone_timeout_task(void);
