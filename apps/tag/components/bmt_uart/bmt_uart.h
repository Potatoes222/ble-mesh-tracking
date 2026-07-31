#pragma once

#include "esp_err.h"

/* Initialise the UART driver and command task (1 = status). */
esp_err_t bmt_uart_init(void);
