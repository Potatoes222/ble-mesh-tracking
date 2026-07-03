#include "bmt_uart.h"

static const char *TAG = "BMT_UART";

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

    printf("\n===== BMT RELAY COMMANDS =====\n");
    printf("r -> RESET mesh (xoa NVS, ve unprovisioned)\n");
    printf("1 -> STATUS hien tai\n");
    printf("==============================\n");

    uint8_t ch;
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(200));
        if (len <= 0 || ch == '\r' || ch == '\n') continue;

        switch (ch) {
        case 'r':
        case 'R':
            printf("\n[UART] Resetting mesh provision...\n");
            bmt_mesh_reset();
            break;

        case '1':
            bmt_mesh_print_status("RELAY STATUS");
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
