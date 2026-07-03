#pragma once

#include <stdbool.h>
#include "bmt_types.h"
#include "esp_err.h"

void bmt_mqtt_init(void);
bool bmt_mqtt_is_connected(void);

int bmt_mqtt_publish(const char *topic, const char *json, int qos);

esp_err_t bmt_mqtt_queue_create(void);
bool      bmt_mqtt_enqueue_tag_report(const bmt_tag_report_t *r);
void      bmt_mqtt_start_worker(void);

void bmt_mqtt_print_stats(void);
