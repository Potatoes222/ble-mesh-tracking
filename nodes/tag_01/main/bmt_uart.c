#include "bmt_uart.h"

static const char *TAG = "BMT_UART";

static void print_status(void) {
    const uint8_t *u = BMT_SYSTEM_UUID;
    printf("\n========== TAG STATUS ==========\n");
    printf("UUID      : %02X%02X%02X%02X-%02X%02X-%02X%02X-"
           "%02X%02X-%02X%02X%02X%02X%02X%02X\n",
           u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13],
           u[14], u[15]);
    printf("Major     : 0x%04X (%s)\n", BMT_TAG_MAJOR,
           BMT_TAG_MAJOR == 0x0001 ? "PERSON" : "ASSET");
    printf("Minor     : 0x%04X  (Tag ID = %u)\n", BMT_TAG_MINOR, BMT_TAG_MINOR);
    printf("TX Power  : %d dBm  (measured power ref)\n", BMT_TAG_TX_POWER_REF);
    printf("Sequence  : %u\n", bmt_beacon_get_sequence());
    printf("CRC-16    : 0x%04X\n", bmt_beacon_get_crc16());
    printf("ADV state : %s\n", bmt_beacon_is_active() ? "ACTIVE" : "STOPPED");
    printf("Heap free : %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("================================\n");
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

    printf("\n===== TAG COMMANDS: 1=status =====\n");

    uint8_t ch;
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(200));
        if (len <= 0 || ch == '\r' || ch == '\n') continue;
        if (ch == '1') print_status();
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
