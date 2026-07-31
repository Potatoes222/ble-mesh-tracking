#pragma once

/* Initialise WiFi STA and block until connected (portMAX_DELAY). Gateway
 * needs WiFi permanently up for MQTT / ThingsBoard, so no timeout here. */
void bmt_wifi_init(void);
