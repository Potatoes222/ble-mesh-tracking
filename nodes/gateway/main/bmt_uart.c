#include "bmt_uart.h"

static const char *TAG = "BMT_UART";

void bmt_uart_print_status(void) {
    printf("\n=========== GATEWAY COMMANDS ===========\n");
    printf("1 -> LIST PROVISIONED NODES\n");
    printf("2 -> LIST TRACKED TAGS + ZONES\n");
    printf("3 -> MQTT QUEUE STATS\n");
    printf("s -> SCAN BEACONS (%ds)\n", BMT_SCAN_DURATION_MS / 1000);
    printf("p -> PROVISION SCAN LIST\n");
    printf("a -> AUTO PROVISION MODE\n");
    printf("4 -> SHOW STATUS\n");
    printf("u -> [OTA] update SCANNER (s) or RELAY (r)\n");
    printf("g -> [OTA] update GATEWAY firmware\n");
    printf("0 -> CLEAR NVS (forget all nodes)\n");
    printf("Provision mode: %s\n",
           bmt_scan_list_get_mode() == BMT_PROV_MODE_AUTO ? "AUTO" : "MANUAL");
    printf("=========================================\n");
}

static void wipe_mesh_stack_nodes(void) {
    const esp_ble_mesh_node_t **entry = esp_ble_mesh_provisioner_get_node_table_entry();
    if (!entry) return;
    int erased = 0;
    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++) {
        if (entry[i]) {
            esp_ble_mesh_provisioner_delete_node_with_uuid(entry[i]->dev_uuid);
            erased++;
        }
    }
    printf("[UART] Erased %d node(s) from mesh stack\n", erased);
}

static void uart_task(void *arg) {
    (void)arg;
    uint8_t ch;
    bmt_uart_print_status();

    while (1) {
        int len = uart_read_bytes(BMT_UART_NUM, &ch, 1, pdMS_TO_TICKS(200));
        if (len <= 0 || ch == '\r' || ch == '\n') continue;

        switch (ch) {
        case '1':
            bmt_node_table_print();
            break;
        case '2':
            bmt_zone_print_all();
            break;
        case '3':
            bmt_mqtt_print_stats();
            break;
        case 's':
            printf("\n[UART] Starting MANUAL SCAN...\n");
            bmt_scan_list_do_scan();
            break;
        case 'p':
            if (bmt_scan_list_get_mode() != BMT_PROV_MODE_MANUAL)
                printf("\n[UART] Not in MANUAL mode. Press s first.\n");
            else
                bmt_scan_list_provision();
            break;
        case 'a':
            bmt_scan_list_set_mode(BMT_PROV_MODE_AUTO);
            bmt_scan_list_reset();
            printf("\n[UART] AUTO provision mode\n");
            break;
        case '4':
            bmt_uart_print_status();
            break;
        case 'u':
        case 'U':
            printf("\n[OTA] Choose target: s=SCANNER, r=RELAY, ESC=cancel\n");
            uint8_t sub;
            while (1) {
                int n = uart_read_bytes(BMT_UART_NUM, &sub, 1, pdMS_TO_TICKS(10000));
                if (n <= 0) {
                    printf("[OTA] timeout, cancelled\n");
                    break;
                }
                if (sub == 's' || sub == 'S') {
                    bmt_ota_trigger_all_scanners();
                    break;
                }
                if (sub == 'r' || sub == 'R') {
                    bmt_ota_trigger_all_relays();
                    break;
                }
                if (sub == 27) {
                    printf("[OTA] cancelled\n");
                    break;
                }
            }
            break;
        case 'g':
        case 'G':
            printf("\n[OTA] Gateway self-update\n");
            bmt_ota_gateway_self_update();
            break;
        case '0':
            printf("\n[UART] Clearing NVS + REBOOT...\n");
            wipe_mesh_stack_nodes();
            bmt_node_table_clear();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;
        default:
            printf("\n[UART] Unknown: %c\n", ch);
            bmt_uart_print_status();
            break;
        }
    }
}

esp_err_t bmt_uart_init(void) {
    const uart_config_t cfg = {
        .baud_rate  = BMT_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BMT_UART_NUM, BMT_UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BMT_UART_NUM, &cfg));

    BaseType_t ok = xTaskCreate(uart_task, "bmt_uart", 4096, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "uart task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
