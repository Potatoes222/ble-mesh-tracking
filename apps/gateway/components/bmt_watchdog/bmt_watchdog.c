#include "bmt_watchdog.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmt_mesh.h"
#include "bmt_node_table.h"
#include "bmt_ota.h"
#include "bmt_types.h"
#include "bmt_zone.h"

static const char* TAG = "BMT_WDG";

#define BMT_WDG_TIMEOUT_MS 30000
#define BMT_WDG_RESET_TRIES 5
#define BMT_WDG_RESET_GAP_MS 1500
#define BMT_WDG_NODE_WAIT_MS 12000
#define BMT_WDG_STABILIZE_MS 15000

static void data_watchdog_task(void* arg)
{
	(void)arg;

	vTaskDelay(pdMS_TO_TICKS(BMT_WDG_STABILIZE_MS));

	bool has_scan = false;
	for (int i = 0; i < bmt_node_table_capacity(); i++)
	{
		bmt_node_t* n = bmt_node_table_get(i);
		if (n && n->used && n->is_scan && n->config_done)
		{
			has_scan = true;
			break;
		}
	}
	if (!has_scan)
	{
		ESP_LOGI(TAG, "Fresh boot — no configured nodes, watchdog exit");
		vTaskDelete(NULL);
		return;
	}

	while (1)
	{
		uint32_t snap = bmt_mesh_get_received_count();
		ESP_LOGI(TAG, "NVS nodes detected — watching %ds... (snap=%" PRIu32 ")",
		         BMT_WDG_TIMEOUT_MS / 1000, snap);

		vTaskDelay(pdMS_TO_TICKS(BMT_WDG_TIMEOUT_MS));

		if (bmt_mesh_get_received_count() > snap)
		{
			ESP_LOGI(TAG, "Mesh OK (%" PRIu32 " -> %" PRIu32 ") — watchdog done",
			         snap, bmt_mesh_get_received_count());
			vTaskDelete(NULL);
			return;
		}

		ESP_LOGW(TAG, "============================================");
		ESP_LOGW(TAG, "No mesh data in %ds — starting reset cycle", BMT_WDG_TIMEOUT_MS / 1000);
		ESP_LOGW(TAG, "============================================");

		uint8_t dummy = 0;
		int sent_ok = 0;
		for (int i = 0; i < BMT_WDG_RESET_TRIES; i++)
		{
			if (bmt_ota_is_running())
			{
				ESP_LOGW(TAG, "RESET_CMD [%d/%d]: skipped (OTA running)", i + 1, BMT_WDG_RESET_TRIES);
			}
			else
			{
				esp_err_t e = bmt_mesh_publish(0xFFFF, BMT_OP_VND_RESET_CMD, &dummy, sizeof(dummy));
				if (e == ESP_OK)
					sent_ok++;
				ESP_LOGW(TAG, "RESET_CMD [%d/%d]: %s (ok=%d)",
				         i + 1, BMT_WDG_RESET_TRIES, e == ESP_OK ? "sent" : esp_err_to_name(e), sent_ok);
			}
			vTaskDelay(pdMS_TO_TICKS(BMT_WDG_RESET_GAP_MS));
		}

		if (sent_ok == 0)
		{
			ESP_LOGE(TAG, "RESET_CMD that bai ca %d lan — mesh/radio co ve chua san sang, "
			              "retry sau %ds",
			         BMT_WDG_RESET_TRIES, BMT_WDG_TIMEOUT_MS / 1000);
			continue;
		}
		break;
	}

	ESP_LOGW(TAG, "Waiting %ds for nodes to complete reset + reboot...", BMT_WDG_NODE_WAIT_MS / 1000);
	vTaskDelay(pdMS_TO_TICKS(BMT_WDG_NODE_WAIT_MS));

	ESP_LOGW(TAG, "Gateway FULL RESET — wiping all mesh state...");
	int n = bmt_mesh_wipe_all_provisioned();
	ESP_LOGW(TAG, "Erased %d node(s) from provisioner table", n);

	bmt_node_table_clear();
	bmt_zone_reset_all();
	vTaskDelay(pdMS_TO_TICKS(1000));
	ESP_LOGW(TAG, "Rebooting — auto provision on next boot...");
	esp_restart();
}

void bmt_watchdog_start(void)
{
	xTaskCreate(data_watchdog_task, "bmt_wdg", 3584, NULL, 2, NULL);
}
