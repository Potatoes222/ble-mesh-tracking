#pragma once

#include <stdio.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_mesh.h"
#include "bmt_ota.h"
#include "bmt_scan.h"
#include "bmt_scan_core.h"

esp_err_t bmt_uart_init(void);
