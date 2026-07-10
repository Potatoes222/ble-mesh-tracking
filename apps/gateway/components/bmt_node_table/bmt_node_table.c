#include "bmt_node_table.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "BMT_NODETBL";

#define BMT_NVS_NAMESPACE "bmt_gw"
#define BMT_NVS_KEY_NODES "node_table"

static bmt_node_t s_nodes[BMT_MAX_NODES];

int bmt_node_table_capacity(void)
{
	return BMT_MAX_NODES;
}

bmt_node_t* bmt_node_table_get(int idx)
{
	if (idx < 0 || idx >= BMT_MAX_NODES)
		return NULL;
	return &s_nodes[idx];
}

int bmt_node_table_find(uint16_t addr)
{
	for (int i = 0; i < BMT_MAX_NODES; i++)
		if (s_nodes[i].used && s_nodes[i].addr == addr)
			return i;
	return -1;
}

bool bmt_node_table_uuid_provisioned(const uint8_t* uuid)
{
	for (int i = 0; i < BMT_MAX_NODES; i++)
		if (s_nodes[i].used && memcmp(s_nodes[i].uuid, uuid, 16) == 0)
			return true;
	return false;
}

int bmt_node_table_add(uint16_t addr, const uint8_t* uuid,
                       const uint8_t* mac, const char* name)
{
	int idx = bmt_node_table_find(addr);
	if (idx >= 0)
	{
		if (uuid)
			memcpy(s_nodes[idx].uuid, uuid, 16);
		if (mac)
			memcpy(s_nodes[idx].mac, mac, 6);
		if (name && name[0])
			strncpy(s_nodes[idx].name, name, sizeof(s_nodes[idx].name) - 1);
		return idx;
	}
	for (int i = 0; i < BMT_MAX_NODES; i++)
	{
		if (!s_nodes[i].used)
		{
			memset(&s_nodes[i], 0, sizeof(s_nodes[i]));
			s_nodes[i].used = true;
			s_nodes[i].addr = addr;
			if (uuid)
				memcpy(s_nodes[i].uuid, uuid, 16);
			if (mac)
				memcpy(s_nodes[i].mac, mac, 6);
			if (name && name[0])
				strncpy(s_nodes[i].name, name, sizeof(s_nodes[i].name) - 1);
			else
				snprintf(s_nodes[i].name, sizeof(s_nodes[i].name), "Node_0x%04x", addr);
			return i;
		}
	}
	return -1;
}
void bmt_node_table_save(void)
{
	nvs_handle_t h;
	esp_err_t err = nvs_open(BMT_NVS_NAMESPACE, NVS_READWRITE, &h);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "save: nvs_open failed: %s", esp_err_to_name(err));
		return;
	}
	err = nvs_set_blob(h, BMT_NVS_KEY_NODES, s_nodes, sizeof(s_nodes));
	if (err == ESP_OK)
		err = nvs_commit(h);
	nvs_close(h);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "save: write/commit failed: %s", esp_err_to_name(err));
	}
	else
	{
		int count = 0;
		for (int i = 0; i < BMT_MAX_NODES; i++)
			if (s_nodes[i].used)
				count++;
		ESP_LOGI(TAG, "Node table saved to NVS (%d nodes)", count);
	}
}

void bmt_node_table_load(void)
{
	nvs_handle_t h;
	esp_err_t err = nvs_open(BMT_NVS_NAMESPACE, NVS_READONLY, &h);
	if (err == ESP_ERR_NVS_NOT_FOUND)
	{
		ESP_LOGI(TAG, "No saved node table, starting fresh");
		return;
	}
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "load: nvs_open failed: %s", esp_err_to_name(err));
		return;
	}
	size_t size = sizeof(s_nodes);
	err = nvs_get_blob(h, BMT_NVS_KEY_NODES, s_nodes, &size);
	nvs_close(h);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "load: nvs_get_blob failed: %s (size=%u/%u)",
		         esp_err_to_name(err), (unsigned)size, (unsigned)sizeof(s_nodes));
	}
	if (err == ESP_OK)
	{
		int count = 0;
		for (int i = 0; i < BMT_MAX_NODES; i++)
		{
			if (!s_nodes[i].used)
				continue;
			count++;
			if (s_nodes[i].is_relay || s_nodes[i].is_scan)
			{
				s_nodes[i].online = false;
				s_nodes[i].last_seen_ms = 0;
			}
		}
		ESP_LOGI(TAG, "Node table loaded (%d nodes)", count);
	}
}

void bmt_node_table_clear(void)
{
	nvs_handle_t h;
	if (nvs_open(BMT_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK)
	{
		nvs_erase_key(h, BMT_NVS_KEY_NODES);
		nvs_commit(h);
		nvs_close(h);
		ESP_LOGI(TAG, "NVS cleared");
	}
	memset(s_nodes, 0, sizeof(s_nodes));
}

bool bmt_uuid_is_relay(const uint8_t* uuid)
{
	if (!uuid)
		return false;
	return (uuid[0] == 0x52 && uuid[1] == 0x45 && uuid[2] == 0x4C && uuid[3] == 0x41 && uuid[4] == 0x59);
}

bool bmt_uuid_is_scan(const uint8_t* uuid)
{
	if (!uuid)
		return false;
	return (uuid[0] == 0x53 && uuid[1] == 0x43 && uuid[2] == 0x41 && uuid[3] == 0x4E);
}

const char* bmt_uuid_type_str(const uint8_t* uuid)
{
	if (bmt_uuid_is_relay(uuid))
		return "RELAY";
	if (bmt_uuid_is_scan(uuid))
		return "SCAN";
	return "UNKNOWN";
}

static void print_uuid(const uint8_t* uuid)
{
	for (int i = 0; i < 16; i++)
	{
		printf("%02X", uuid[i]);
		if (i != 15)
			printf(":");
	}
}

static void print_mac(const uint8_t* mac)
{
	for (int i = 0; i < 6; i++)
	{
		printf("%02X", mac[i]);
		if (i != 5)
			printf(":");
	}
}

void bmt_node_table_print(void)
{
	printf("\n================ NODE TABLE ================\n");
	bool has_node = false;
	uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
	for (int i = 0; i < BMT_MAX_NODES; i++)
	{
		if (!s_nodes[i].used)
			continue;
		has_node = true;
		printf("Node %d\n", i + 1);
		printf("  Address     : 0x%04X\n", s_nodes[i].addr);
		printf("  Name        : %s\n", s_nodes[i].name[0] ? s_nodes[i].name : "(unknown)");
		printf("  Type        : %s\n",
		       s_nodes[i].is_relay ? "RELAY" : s_nodes[i].is_scan ? "SCAN"
		                                                          : "UNKNOWN");
		printf("  UUID        : ");
		print_uuid(s_nodes[i].uuid);
		printf("\n");
		printf("  MAC         : ");
		print_mac(s_nodes[i].mac);
		printf("\n");
		printf("  Config done : %s\n", s_nodes[i].config_done ? "YES" : "NO");
		if (s_nodes[i].is_relay)
		{
			printf("  Status      : %s\n", s_nodes[i].online ? "ONLINE" : "OFFLINE");
			if (s_nodes[i].last_seen_ms > 0)
				printf("  LastSeen    : %" PRIu32 "s ago\n", (now - s_nodes[i].last_seen_ms) / 1000);
			else
				printf("  LastSeen    : never\n");
		}
		else if (s_nodes[i].is_scan)
		{
			printf("  Status      : %s\n", s_nodes[i].config_done ? "ACTIVE" : "CONFIGURING...");
		}
		printf("------------------------------------------\n");
	}
	if (!has_node)
	{
		printf("  No provisioned nodes\n------------------------------------------\n");
	}
}
