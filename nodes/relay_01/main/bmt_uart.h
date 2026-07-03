#pragma once

#include <stdio.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_mesh.h"

esp_err_t bmt_uart_init(void);
