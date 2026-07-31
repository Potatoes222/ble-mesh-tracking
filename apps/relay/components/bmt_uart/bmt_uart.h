#pragma once

#include "esp_err.h"

/* Initialise the UART driver and the command-handling task
 * (r = reset, 1 = status). Also starts the periodic health-log task
 * (which waits for provisioning to finish before counting time). */
esp_err_t bmt_uart_init(void);
