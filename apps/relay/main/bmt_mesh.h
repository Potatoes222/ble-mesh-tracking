#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BMT_PROV_COMPLETE_BIT   BIT0

/* Khởi tạo mesh: đăng ký callback, init composition (health server + vendor
 * model RESET_CMD/OTA_TRIGGER/OTA_RESULT), khôi phục provision từ NVS hoặc
 * enable provisioning mới */
esp_err_t bmt_mesh_init(void);

EventGroupHandle_t bmt_mesh_evgrp(void);

uint16_t bmt_mesh_node_addr(void);
uint16_t bmt_mesh_net_idx(void);
uint16_t bmt_mesh_app_idx(void);
bool     bmt_mesh_relay_enabled(void);

/* [v1.4] Báo cáo kết quả OTA (0=thành công, khác 0=thất bại) về Gateway qua mesh */
esp_err_t bmt_mesh_report_ota_result(uint8_t status);

/* Reset mesh provisioning (dùng cho lệnh UART 'r' và RESET_CMD nhận từ Gateway) */
void bmt_mesh_local_reset(void);
