#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
	const char* url;                 /* URL .bin trên OTA HTTP server */
	const char* wifi_ssid;           /* SSID WiFi (dung cho OTA) */
	const char* wifi_pass;           /* Password WiFi */
	const char* nvs_namespace;       /* Namespace NVS de luu co "OTA vua thanh cong, doi bao Gateway" */
	uint32_t wifi_timeout_ms;        /* Timeout doi WiFi connect (mac dinh 30000) */
	uint32_t auto_check_interval_ms; /* Chu ky tu kiem tra version (mac dinh 180000) */
	bool silence_log_during_ota;     /* true = mute log level xuong NONE luc OTA (scanner) */
} bmt_ota_config_t;

/* Goi 1 lan luc boot TRUOC khi goi bat ky ham bmt_ota_* nao khac. Config
 * duoc copy vao static local nen caller khong can giu song struct. */
void bmt_ota_init(const bmt_ota_config_t* cfg);

/* Bat WiFi OTA neu chua dang chay (idempotent) — goi tu bmt_mesh.c khi
 * nhan OTA_TRIGGER qua mesh, hoac tu UART/beacon. */
void bmt_ota_trigger(void);

/* true khi dang trong qua trinh OTA — bmt_scan dung de tam dung GAP scan
 * (nhuong toan bo radio cho WiFi), bmt_uart dung hien thi status */
bool bmt_ota_is_triggered(void);

/* Goi 1 lan luc boot (sau bmt_ota_init + mesh san sang) — neu NVS co co
 * "vua OTA xong va reboot thanh cong", tu gui bao cao THANH CONG ve Gateway. */
void bmt_ota_start_pending_report_task(void);

/* Task tu kiem tra version tren server dinh ky, tu OTA neu khac. Khong can
 * Gateway/UART trigger. Goi 1 lan luc boot. */
void bmt_ota_start_auto_check(void);
