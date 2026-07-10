#include "bmt_scan_list.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_err.h"

#include "bmt_node_table.h"

#define BMT_MAX_SCAN_LIST     10
#define BMT_SCAN_DURATION_MS  10000

typedef struct {
    bool     used;
    uint8_t  uuid[16], addr[6];
    uint8_t  addr_type;
    uint16_t oob_info;
} bmt_scan_entry_t;

static bmt_scan_entry_t   s_list[BMT_MAX_SCAN_LIST];
static int                s_count    = 0;
static bool               s_scanning = false;
static bmt_prov_mode_t    s_mode     = BMT_PROV_MODE_AUTO;

bmt_prov_mode_t bmt_scan_list_get_mode(void)   { return s_mode; }
bool            bmt_scan_list_is_scanning(void){ return s_scanning; }

void bmt_scan_list_set_mode(bmt_prov_mode_t mode)
{
    s_mode = mode;
    if (mode == BMT_PROV_MODE_AUTO) {
        s_count = 0;
        memset(s_list, 0, sizeof(s_list));
    }
}

bool bmt_scan_list_add(const uint8_t *uuid, const uint8_t *mac,
                       uint8_t addr_type, uint16_t oob_info)
{
    if (!s_scanning) return false;
    for (int i = 0; i < s_count; i++)
        if (memcmp(s_list[i].uuid, uuid, 16) == 0) return false;
    if (s_count >= BMT_MAX_SCAN_LIST) return false;

    memcpy(s_list[s_count].uuid, uuid, 16);
    memcpy(s_list[s_count].addr, mac,  6);
    s_list[s_count].addr_type = addr_type;
    s_list[s_count].oob_info  = oob_info;
    s_list[s_count].used      = true;
    s_count++;
    return true;
}

void bmt_scan_list_print(void)
{
    printf("\n========== SCAN LIST ==========\n");
    if (s_count == 0) { printf("  (empty)\n================================\n"); return; }
    for (int i = 0; i < s_count; i++) {
        bool already = bmt_node_table_uuid_provisioned(s_list[i].uuid);
        printf("  [%d] %-7s UUID: ", i, bmt_uuid_type_str(s_list[i].uuid));
        for (int b = 0; b < 16; b++) printf("%02X%s", s_list[i].uuid[b], b < 15 ? ":" : "");
        printf("\n        MAC : ");
        for (int b = 0; b < 6; b++) printf("%02X%s", s_list[i].addr[b], b < 5 ? ":" : "");
        printf("  %s\n", already ? "[ALREADY PROVISIONED]" : "[NEW]");
    }
    printf("================================\n");
}

void bmt_scan_list_start(void)
{
    s_count = 0;
    memset(s_list, 0, sizeof(s_list));
    s_scanning = true;
    s_mode     = BMT_PROV_MODE_MANUAL;
    printf("\n[SCAN] Scanning %ds...\n", BMT_SCAN_DURATION_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(BMT_SCAN_DURATION_MS));
    s_scanning = false;
    printf("[SCAN] Done.\n");
    bmt_scan_list_print();
}

void bmt_scan_list_provision(void)
{
    int count = 0;
    for (int i = 0; i < s_count; i++) {
        if (bmt_node_table_uuid_provisioned(s_list[i].uuid)) {
            printf("[PROV] Skip [%d] already provisioned\n", i); continue;
        }
        esp_ble_mesh_unprov_dev_add_t dev = {0};
        memcpy(dev.uuid, s_list[i].uuid, 16);
        memcpy(dev.addr, s_list[i].addr, 6);
        dev.addr_type = s_list[i].addr_type;
        dev.oob_info  = s_list[i].oob_info;
        dev.bearer    = ESP_BLE_MESH_PROV_ADV;
        esp_err_t err = esp_ble_mesh_provisioner_add_unprov_dev(
            &dev, ADD_DEV_FLUSHABLE_DEV_FLAG | ADD_DEV_START_PROV_NOW_FLAG);
        if (err == ESP_OK) {
            printf("[PROV] Provisioning [%d] %s...\n", i, bmt_uuid_type_str(s_list[i].uuid));
            count++; vTaskDelay(pdMS_TO_TICKS(500));
        } else printf("[PROV] Failed [%d]: %s\n", i, esp_err_to_name(err));
    }
    if (count == 0) printf("[PROV] Nothing to provision.\n");
}
