#pragma once

/* Khởi tạo WiFi STA, block cho tới khi kết nối xong (portMAX_DELAY) — Gateway
 * cần WiFi thường trực cho MQTT/ThingsBoard nên không cần timeout ở đây. */
void bmt_wifi_init(void);
