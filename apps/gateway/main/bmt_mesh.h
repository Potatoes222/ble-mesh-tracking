#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* ============================================================================
 * BMT_MESH — mesh provisioner: models, callbacks, publish, relay ping
 * ----------------------------------------------------------------------------
 * [v4.9-security] NetKey/AppKey KHÔNG hardcode — sinh random 1 lần bằng
 * esp_fill_random() lúc first-boot (bmt_mesh_generate_keys_if_needed), sau đó
 * mesh stack tự lưu vào NVS. PHẢI gọi hàm này TRƯỚC bmt_mesh_init().
 * ============================================================================ */
void bmt_mesh_generate_keys_if_needed(void);

/* Khởi tạo mesh: đăng ký callback, add NetKey/AppKey nếu chưa có, bind AppKey
 * vào vendor model local. Gọi sau bluetooth_init(). */
esp_err_t bmt_mesh_init(void);

void bmt_mesh_start_node_ping(void);

/* Publish 1 message vendor tới địa chỉ dst (unicast hoặc 0xFFFF broadcast) —
 * dùng chung cho OTA_TRIGGER (bmt_ota.c) và RESET_CMD (bmt_watchdog.c) */
esp_err_t bmt_mesh_publish(uint16_t dst, uint32_t opcode, const void* data, uint16_t len);

/* [FIX-1][FIX-watchdog] Counter đếm mesh traffic 2 chiều thật sự nhận được —
 * TAG_STATUS (có Tag) HOẶC ACK ping chủ động tới Scan/Relay (không cần Tag).
 * Độc lập với MQTT; watchdog dùng để phân biệt "mesh chết" vs "chỉ không có
 * Tag nào đang ở gần Scanner lúc đó". */
uint32_t bmt_mesh_get_received_count(void);

/* Xóa toàn bộ node đã provision khỏi mesh stack (provisioner node table) —
 * dùng cho UART '9' FULL RESET và bmt_watchdog.c. Trả về số node đã xóa. */
int bmt_mesh_wipe_all_provisioned(void);

/* In NetKey/AppKey dạng hex ra UART lúc boot (banner) — giữ key private trong
 * module này, không expose ra ngoài bằng con trỏ/mảng trực tiếp */
void bmt_mesh_print_keys(void);
