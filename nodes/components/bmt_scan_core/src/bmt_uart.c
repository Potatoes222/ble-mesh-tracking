#include "bmt_uart.h"
#include "bmt_mesh.h"
#include "bmt_scan.h"
#include "bmt_scan_core.h"

#include <stdio.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BMT_UART";

static void print_status(void) {
    uint16_t node = bmt_mesh_node_addr();
    uint16_t app  = bmt_mesh_app_idx();
    uint16_t net  = bmt_mesh_net_idx();
    printf("\n===== BMT SCAN STATUS =====\n");
    printf("Scanner ID: 0x%02X\n", bmt_scan_core_scanner_id());
    printf("Node addr : 0x%04X %s\n", node, node == 0 ? "(NOT provisioned)" : "(provisioned)");
    printf("Net idx   : 0x%04X\n", net);
    printf("App idx   : 0x%04X %s\n", app, app == 0xFFFF ? "(waiting AppKey)" : "(AppKey OK)");
    printf("Phase     : %s\n",
           bmt_scan_get_phase() == BMT_PHASE_GAP_SCAN ? "GAP_SCAN" : "MESH_PUB");
    printf("===========================\n");
}

static void uart_task(void *arg) {
    (void)arg;

    const uart_config_t cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &cfg);

    printf("\n===== BMT SCAN NODE COMMANDS =====\n");
    printf("r -> RESET mesh (xoa NVS, reboot)\n");
    printf("1 -> STATUS hien tai\n");
    printf("==================================\n");

    uint8_t ch;
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(200));
        if (len <= 0 || ch == '\r' || ch == '\n') continue;

        switch (ch) {
        case 'r':
        case 'R':
            printf("\n[UART] Resetting mesh provision + REBOOT...\n");
            bmt_mesh_reset();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;

        case '1':
            print_status();
            break;

        default:
            printf("[UART] Unknown: %c  (r=reset, 1=status)\n", ch);
            break;
        }
    }
}

esp_err_t bmt_uart_init(void) {
    BaseType_t ok = xTaskCreate(uart_task, "bmt_uart", 2048, NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "uart task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
