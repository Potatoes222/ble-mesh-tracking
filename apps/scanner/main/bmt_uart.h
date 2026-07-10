#pragma once

#include "esp_err.h"

/* Khởi tạo UART driver + task xử lý lệnh (r=reset, i=set scanner ID,
 * o=test OTA thủ công, 1=status) */
esp_err_t bmt_uart_init(void);
