#pragma once

#include <stdbool.h>

#include "mqtt_client.h"

#include "bmt_types.h"

/* Initialise the MQTT client to ThingsBoard and subscribe to the RPC
 * topic. Routes RPC commands (ota_scanner / ota_relay / ota_gateway) to
 * bmt_ota.h. */
void bmt_mqtt_init(void);

bool bmt_mqtt_is_connected(void);
esp_mqtt_client_handle_t bmt_mqtt_get_client(void);
void bmt_mqtt_enqueue_tag_report(const bmt_tag_report_t* report, const uint8_t* scanner_mac);

/* Called by bmt_thingsboard.c after each successful publish so we can
 * separate mesh_received from mqtt_published (quick debug: is mesh down
 * or is MQTT down?). */
void bmt_mqtt_note_published(void);

void bmt_mqtt_log_stats(void);

/* Task that drains the tag-report queue and publishes to ThingsBoard
 * via bmt_thingsboard.h. */
void bmt_mqtt_start_worker(void);
