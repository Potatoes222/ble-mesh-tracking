#pragma once

#include "esp_err.h"

/* Initialise the UART driver and the command-handling task
 * (commands: r = reset, i = set scanner ID, o = manual OTA test,
 * 1 = status). */
esp_err_t bmt_uart_init(void);
