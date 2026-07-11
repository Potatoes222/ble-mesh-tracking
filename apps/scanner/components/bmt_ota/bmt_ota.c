#include "bmt_ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_app_desc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "bmt_mesh.h"
#include "bmt_scan_core.h"
#include "bmt_config.h"

static const char* TAG = "BMT_OTA";

#define BMT_OTA_NVS_KEY_PENDING "ota_pending"
#define BMT_OTA_AUTO_CHECK_INTERVAL_MS (3 * 60 * 1000)

static volatile bool s_ota_triggered = false;
static EventGroupHandle_t s_wifi_evgrp = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;
static esp_netif_t* s_sta_netif = NULL;
static esp_event_handler_instance_t s_evt_any_id = NULL;
static esp_event_handler_instance_t s_evt_got_ip = NULL;

bool bmt_ota_is_triggered(void)
{
	return s_ota_triggered;
}
static void mark_pending(void)
{
	nvs_handle_t h;
	esp_err_t err = nvs_open(BMT_SCAN_NVS_NAMESPACE, NVS_READWRITE, &h);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "mark_pending: nvs_open fail: %s", esp_err_to_name(err));
		return;
	}
	err = nvs_set_u8(h, BMT_OTA_NVS_KEY_PENDING, 1);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "mark_pending: nvs_set_u8 fail: %s", esp_err_to_name(err));
	else if ((err = nvs_commit(h)) != ESP_OK)
		ESP_LOGE(TAG, "mark_pending: nvs_commit fail: %s", esp_err_to_name(err));
	nvs_close(h);
}

static bool check_and_clear_pending(void)
{
	nvs_handle_t h;
	uint8_t val = 0;
	if (nvs_open(BMT_SCAN_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
		return false;
	nvs_get_u8(h, BMT_OTA_NVS_KEY_PENDING, &val);
	if (val)
	{
		nvs_set_u8(h, BMT_OTA_NVS_KEY_PENDING, 0);
		nvs_commit(h);
	}
	nvs_close(h);
	return val != 0;
}

static void print_sha256_hex(const char* label, const uint8_t* sha, size_t len)
{
	printf("%s", label);
	for (size_t i = 0; i < len; i++)
		printf("%02x", sha[i]);
	printf("\n");
}

static void wifi_event_handler(void* arg, esp_event_base_t base,
                               int32_t id, void* data)
{
	if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
	{
		esp_wifi_connect();
	}
	else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
	{
		esp_wifi_connect();
		xEventGroupClearBits(s_wifi_evgrp, WIFI_CONNECTED_BIT);
	}
	else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* ev = (ip_event_got_ip_t*)data;
		ESP_LOGI(TAG, "WiFi got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
		xEventGroupSetBits(s_wifi_evgrp, WIFI_CONNECTED_BIT);
	}
}

static void ota_wifi_task(void* arg)
{
	(void)arg;

	esp_log_level_set("*", ESP_LOG_NONE);

	printf("\n[OTA] ===== WiFi OTA triggered =====\n");
	printf("[OTA] URL: %s\n", BMT_OTA_SCANNER_URL);

	/* Mặc định coi là "fail" khi rớt xuống dọn dẹp — chỉ tắt cờ này khi bỏ
	 * qua do cùng version (không phải lỗi, không cần báo Gateway) */
	bool do_report_fail = true;

	s_wifi_evgrp = xEventGroupCreate();
	esp_err_t err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
	{
		printf("[OTA] netif init failed: %s\n", esp_err_to_name(err));
		goto ota_fail;
	}
	esp_event_loop_create_default();
	s_sta_netif = esp_netif_create_default_wifi_sta();

	wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
	err = esp_wifi_init(&wcfg);
	if (err != ESP_OK)
	{
		printf("[OTA] wifi init failed: %s\n", esp_err_to_name(err));
		goto ota_fail;
	}

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
	                                    &wifi_event_handler, NULL, &s_evt_any_id);
	esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
	                                    &wifi_event_handler, NULL, &s_evt_got_ip);

	wifi_config_t wifi_cfg = {.sta = {.threshold.authmode = WIFI_AUTH_WPA2_PSK}};
	strncpy((char*)wifi_cfg.sta.ssid, BMT_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
	strncpy((char*)wifi_cfg.sta.password, BMT_WIFI_PASS, sizeof(wifi_cfg.sta.password));
	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
	esp_wifi_start();

	printf("[OTA] Connecting WiFi (max %ds)...\n", BMT_OTA_WIFI_TIMEOUT_MS / 1000);
	EventBits_t bits = xEventGroupWaitBits(s_wifi_evgrp, WIFI_CONNECTED_BIT,
	                                       pdFALSE, pdFALSE,
	                                       pdMS_TO_TICKS(BMT_OTA_WIFI_TIMEOUT_MS));
	if (!(bits & WIFI_CONNECTED_BIT))
	{
		printf("[OTA] WiFi connect timeout\n");
		goto ota_fail_wifi;
	}

	printf("[OTA] WiFi connected — checking firmware version...\n");
	{
		esp_http_client_config_t http_cfg = {
		    .url = BMT_OTA_SCANNER_URL,
		    .timeout_ms = 120000,
		    .keep_alive_enable = true,
		};
		esp_https_ota_config_t ota_cfg = {.http_config = &http_cfg};
		esp_https_ota_handle_t ota_handle = NULL;

		err = esp_https_ota_begin(&ota_cfg, &ota_handle);
		if (err != ESP_OK)
		{
			printf("[OTA] esp_https_ota_begin FAILED: %s\n", esp_err_to_name(err));
			goto ota_fail_wifi;
		}

		esp_app_desc_t new_desc;
		err = esp_https_ota_get_img_desc(ota_handle, &new_desc);
		if (err != ESP_OK)
		{
			printf("[OTA] esp_https_ota_get_img_desc FAILED: %s\n", esp_err_to_name(err));
			esp_https_ota_abort(ota_handle);
			goto ota_fail_wifi;
		}

		const esp_app_desc_t* cur_desc = esp_app_get_description();

		print_sha256_hex("[OTA] Node   SHA256: ", cur_desc->app_elf_sha256, sizeof(cur_desc->app_elf_sha256));
		print_sha256_hex("[OTA] Server SHA256: ", new_desc.app_elf_sha256, sizeof(new_desc.app_elf_sha256));

		if (memcmp(new_desc.app_elf_sha256, cur_desc->app_elf_sha256, sizeof(new_desc.app_elf_sha256)) == 0)
		{
			printf("[OTA] SHA256 match — node firmware is IDENTICAL to server firmware, skip.\n");
			esp_https_ota_abort(ota_handle);
			do_report_fail = false;
			goto ota_fail_wifi;
		}
		printf("[OTA] SHA256 differ — node firmware DIFFERS from server firmware, checking version...\n");

		printf("[OTA] Node   version: %s\n", cur_desc->version);
		printf("[OTA] Server version: %s\n", new_desc.version);

		if (strncmp(new_desc.version, cur_desc->version, sizeof(new_desc.version)) <= 0)
		{
			printf("[OTA] Server version is NOT newer — skip, no downgrade.\n");
			esp_https_ota_abort(ota_handle);
			do_report_fail = false;
			goto ota_fail_wifi;
		}

		printf("[OTA] Server version is NEWER -> flashing firmware...\n");
		while (1)
		{
			err = esp_https_ota_perform(ota_handle);
			if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
				break;
		}

		if (err == ESP_OK && esp_https_ota_is_complete_data_received(ota_handle))
		{
			err = esp_https_ota_finish(ota_handle);
		}
		else
		{
			esp_https_ota_abort(ota_handle);
			if (err == ESP_OK)
				err = ESP_FAIL;
		}
	}

	if (err == ESP_OK)
	{
		printf("[OTA] ===== OTA SUCCESS — rebooting into new firmware =====\n");
		mark_pending();
		vTaskDelay(pdMS_TO_TICKS(1000));
		esp_restart();
	}
	else
	{
		printf("[OTA] esp_https_ota FAILED: %s\n", esp_err_to_name(err));
		/* Không report ở đây — tự rơi xuống ota_fail bên dưới, chỉ report 1 lần */
	}

ota_fail_wifi:
	esp_wifi_stop();
ota_fail:
	if (s_evt_any_id)
	{
		esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_evt_any_id);
		s_evt_any_id = NULL;
	}
	if (s_evt_got_ip)
	{
		esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_evt_got_ip);
		s_evt_got_ip = NULL;
	}
	esp_wifi_deinit();
	if (s_sta_netif)
	{
		esp_netif_destroy_default_wifi(s_sta_netif);
		s_sta_netif = NULL;
	}
	if (s_wifi_evgrp)
	{
		vEventGroupDelete(s_wifi_evgrp);
		s_wifi_evgrp = NULL;
	}

	esp_log_level_set("*", ESP_LOG_INFO);
	if (do_report_fail)
	{
		bmt_mesh_report_ota_result(1);
		printf("[OTA] OTA failed — back to BLE-only mode\n");
	}
	else
	{
		printf("[OTA] No update needed — back to BLE-only mode\n");
	}
	s_ota_triggered = false;
	vTaskDelete(NULL);
}

void bmt_ota_trigger(void)
{
	if (s_ota_triggered)
	{
		ESP_LOGW(TAG, "Already triggered, ignoring");
		return;
	}
	s_ota_triggered = true;
	xTaskCreate(ota_wifi_task, "bmt_ota_wifi", 8192, NULL, 5, NULL);
}

static void report_pending_task(void* arg)
{
	(void)arg;
	vTaskDelay(pdMS_TO_TICKS(5000));
	if (check_and_clear_pending())
	{
		ESP_LOGI(TAG, "Phat hien vua OTA xong va reboot thanh cong - bao cao ve Gateway");
		bmt_mesh_report_ota_result(0);
	}
	vTaskDelete(NULL);
}

void bmt_ota_start_pending_report_task(void)
{
	xTaskCreate(report_pending_task, "ota_rpt", 2048, NULL, 3, NULL);
}
static void auto_check_task(void* arg)
{
	(void)arg;
	while (1)
	{
		vTaskDelay(pdMS_TO_TICKS(BMT_OTA_AUTO_CHECK_INTERVAL_MS));
		bmt_ota_trigger();
	}
}

void bmt_ota_start_auto_check(void)
{
	xTaskCreate(auto_check_task, "bmt_ota_chk", 2048, NULL, 3, NULL);
}
