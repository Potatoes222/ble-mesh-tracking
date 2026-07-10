#pragma once

#include "esp_err.h"

/* Khởi tạo UART + task xử lý lệnh (r=reset, 1=status), đồng thời khởi động
 * task in health log định kỳ (đợi provision xong mới bắt đầu đếm giờ) */
esp_err_t bmt_uart_init(void);
