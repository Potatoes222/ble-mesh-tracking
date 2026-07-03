#pragma once

#include "esp_err.h"

esp_err_t bmt_uart_init(void);
void      bmt_uart_print_status(void);
