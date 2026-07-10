#pragma once

#include "esp_err.h"

/* Khởi tạo UART + task xử lý lệnh (1=status) */
esp_err_t bmt_uart_init(void);
