#pragma once

#include <stdbool.h>

#include "mqtt_client.h"

#include "bmt_types.h"

/* Khởi tạo MQTT client tới ThingsBoard + đăng ký RPC topic. Định tuyến lệnh
 * RPC (ota_scanner/ota_relay/ota_gateway) sang bmt_ota.h. */
void bmt_mqtt_init(void);

bool bmt_mqtt_is_connected(void);
esp_mqtt_client_handle_t bmt_mqtt_get_client(void);

/* Gọi từ bmt_mesh.c khi nhận TAG_STATUS — enqueue vào hàng đợi, KHÔNG publish
 * trực tiếp trong mesh callback (tránh block BLE host task).
 * [v6.5-relay-only] scanner_mac: MAC thật (6 byte) của scanner gửi báo cáo,
 * tra từ bmt_node_table theo địa chỉ mesh nguồn — NULL nếu tra không ra
 * (chưa kịp provision xong). */
void bmt_mqtt_enqueue_tag_report(const bmt_tag_report_t* report, const uint8_t* scanner_mac);

/* [FIX-5] bmt_thingsboard.c gọi sau mỗi lần publish thành công, để thống kê
 * mesh_received vs mqtt_published tách biệt (debug nhanh: mesh chết hay MQTT chết) */
void bmt_mqtt_note_published(void);

void bmt_mqtt_log_stats(void);

/* Task tiêu thụ hàng đợi tag report, publish lên ThingsBoard qua bmt_thingsboard.h */
void bmt_mqtt_start_worker(void);
