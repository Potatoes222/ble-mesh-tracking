#pragma once

#include <stdio.h>

#include "driver/uart.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_config.h"
#include "bmt_mqtt.h"
#include "bmt_node_table.h"
#include "bmt_scan_list.h"
#include "bmt_zone.h"

esp_err_t bmt_uart_init(void);
void      bmt_uart_print_status(void);
