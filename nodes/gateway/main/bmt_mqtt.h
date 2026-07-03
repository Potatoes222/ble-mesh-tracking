#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bmt_config.h"
#include "bmt_types.h"

void bmt_mqtt_init(void);
bool bmt_mqtt_is_connected(void);

int bmt_mqtt_publish(const char *topic, const char *json, int qos);

esp_err_t bmt_mqtt_queue_create(void);
bool      bmt_mqtt_enqueue_tag_report(const bmt_tag_report_t *r);
void      bmt_mqtt_start_worker(void);

void bmt_mqtt_print_stats(void);
