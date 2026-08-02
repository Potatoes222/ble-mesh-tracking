#pragma once

/* Initialise WiFi STA and wait up to BMT_WIFI_INIT_TIMEOUT_MS for an IP.
 * If the initial wait times out, boot continues while the WiFi event handler
 * keeps reconnecting in the background; MQTT connects when WiFi becomes ready. */
void bmt_wifi_init(void);
