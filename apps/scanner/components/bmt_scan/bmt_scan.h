#pragma once

#include "esp_err.h"

/* Đăng ký GAP callback, cấu hình scan params, khởi động radio_manager_task
 * (time-division GAP scan / mesh publish) và timeout_check_task */
esp_err_t bmt_scan_start(void);
