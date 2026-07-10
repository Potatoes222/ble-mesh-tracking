#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Gateway tự cập nhật firmware của chính nó qua WiFi (lệnh UART 'g' / RPC
 * "ota_gateway" từ ThingsBoard). Chạy bất đồng bộ trong task riêng — reboot
 * nếu thành công, quay lại bình thường nếu thất bại. */
esp_err_t bmt_ota_gateway_self_update(void);

/* [v4.8] Trigger WiFi OTA cho tất cả Relay đã config_done — gửi lần lượt từng
 * node qua mesh unicast (OTA_TRIGGER), cách nhau để mỗi relay OTA xong mới
 * sang relay kế tiếp. */
esp_err_t bmt_ota_trigger_all_relays(void);

/* [v4.9-security] Trigger WiFi OTA cho tất cả Scanner đã config_done.
 * Ưu tiên: 1 beacon BLE raw ADV (HMAC-16 chống giả mạo) broadcast 1 lần —
 * tất cả scanner OTA gần như đồng thời (scanner GAP scan 100% nên chắc chắn
 * bắt được). Nếu gửi beacon thất bại (ví dụ do NimBLE host đang bận advertise
 * cho mesh), tự động fallback sang gửi mesh unicast OTA_TRIGGER lần lượt từng
 * scanner — chậm hơn nhưng vẫn đảm bảo cập nhật được. */
esp_err_t bmt_ota_trigger_all_scanners(void);

bool bmt_ota_is_running(void);

/* Gọi 1 lần lúc boot (main.c), trước khi UART/RPC có thể trigger OTA scanner */
void bmt_ota_beacon_key_init(void);

/* [v6.3-auto-ota] Gateway tự động định kỳ kiểm tra version/SHA256 của chính
 * mình, tự OTA nếu server có bản mới hơn — không cần UART 'g'/RPC nữa. Gọi
 * 1 lần lúc boot. (Không ảnh hưởng OTA của Scanner/Relay — đó vẫn theo lệnh
 * UART 'u'/RPC như cũ, không có auto-check riêng.) */
void bmt_ota_start_auto_check(void);

/* [v6.4-security] Mỗi 24h tự sinh 1 key HMAC beacon MỚI (random thật, không
 * suy ra từ key cũ/firmware) và push cho toàn bộ Scanner đã provision qua
 * mesh (đã được AppKey mã hóa sẵn) — thay cho việc dùng mãi 1 key hardcode.
 * Gọi 1 lần lúc boot, SAU bmt_ota_beacon_key_init(). */
void bmt_ota_start_key_rotation(void);

/* Gọi ngay sau khi 1 scanner hoàn tất provision + config (bmt_mesh.c) — đẩy
 * ngay key HMAC beacon hiện hành cho node vừa join, kể cả khi join sau khi
 * đã có rotate xảy ra trước đó. */
void bmt_ota_push_beacon_key_to_node(uint16_t addr);
