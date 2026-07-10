#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BMT_PROV_COMPLETE_BIT   BIT0

/* Khởi tạo mesh: đăng ký callback, sinh UUID từ MAC + scanner_id, init composition,
 * khôi phục provision từ NVS hoặc enable provisioning mới */
esp_err_t bmt_mesh_init(void);

/* Event group dùng để chờ/kiểm tra BMT_PROV_COMPLETE_BIT (vd trong bmt_scan.c
 * trước khi bắt đầu xử lý ADV tag) */
EventGroupHandle_t bmt_mesh_evgrp(void);

uint16_t bmt_mesh_node_addr(void);
uint16_t bmt_mesh_app_idx(void);

/* Con trỏ tới UUID 16 byte hiện tại (SCAN+MAC+...+scanner_id) — chỉ đọc,
 * dùng để in log/status, không sửa trực tiếp */
const uint8_t *bmt_mesh_uuid(void);

/* Duyệt toàn bộ tag đang active trong bmt_tag_table, publish TAG_STATUS lên mesh */
void bmt_mesh_publish_tags(void);

/* [v2.7] Báo cáo kết quả OTA (0=thành công, khác 0=thất bại) về Gateway qua mesh */
esp_err_t bmt_mesh_report_ota_result(uint8_t status);

/* Reset mesh provisioning (dùng cho lệnh UART 'r' và RESET_CMD nhận từ Gateway) */
void bmt_mesh_local_reset(void);
