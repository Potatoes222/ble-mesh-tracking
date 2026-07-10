#pragma once

/* ============================================================================
 * BMT_CONFIG — USER CONFIG cho Gateway
 * ============================================================================ */
#define BMT_WIFI_SSID "YOUR_WIFI_SSID"
#define BMT_WIFI_PASS "YOUR_WIFI_PASSWORD"

/* [SECURITY] MQTTS (TLS) tới ThingsBoard CE tự host qua Docker (xem
 * g:\Thesis\thingsboard\docker-compose.yml) — thay cho ThingsBoard Cloud dùng
 * tạm lúc trước. Đổi BMT_TB_IP theo đúng IP máy đang chạy `docker compose up
 * -d` (mặc định để trùng máy chạy server OTA bên dưới). Chứng chỉ verify
 * theo common_name BMT_TB_CN ("bmt-tb.local"), KHÔNG theo IP — đổi IP không
 * cần tạo lại cert, xem g:\Thesis\thingsboard\tls\gen_certs.sh. */
#define BMT_TB_IP "192.168.2.23"
#define BMT_TB_HOST "mqtts://" BMT_TB_IP ":8883"
#define BMT_TB_CN "bmt-tb.local"
#define BMT_TB_GATEWAY_TOKEN "YOUR_TB_GATEWAY_TOKEN"

#define BMT_DEV_NAME_GATEWAY "bmt_gateway"
#define BMT_DEV_NAME_NODE_FMT "bmt_node_0x%04x"
#define BMT_DEV_NAME_TAG_FMT "bmt_tag_0x%04x"

#define BMT_ROLE_GATEWAY "gateway"
#define BMT_ROLE_RELAY "relay"
#define BMT_ROLE_SCAN "scan"
#define BMT_ROLE_TAG "tag"

/* [SECURITY/DASHBOARD] Device Profile trong ThingsBoard (khac voi attribute
 * "role" o tren) — dung o field "type" trong payload v1/gateway/connect de
 * TB tu gan dung profile cho sub-device luc auto-provision. PHAI tao san 2
 * profile nay trong TB UI (ten phai khop CHINH XAC) truoc khi Gateway ket
 * noi, xem g:\Thesis\thingsboard\docs\thingsboard-mqtt.md muc IV. */
#define BMT_PROFILE_TAG "ble_tag"
#define BMT_PROFILE_NODE "ble_mesh_node"

/* ============================================================================
 * OTA CONFIG
 * ----------------------------------------------------------------------------
 * [SECURITY — quyết định có chủ đích] Dùng HTTP thuần (không TLS) vì server
 * OTA chỉ chạy trong LAN nội bộ (không public ra internet), và việc quản lý
 * CA/certificate cho HTTPS bị đánh giá là phức tạp không tương xứng lợi ích
 * trong phạm vi triển khai thử nghiệm này. Rủi ro còn lại: kẻ tấn công ĐÃ ở
 * trong cùng mạng LAN có thể MITM tráo firmware — chấp nhận được cho LAN cô
 * lập, cần đánh giá lại nếu triển khai production/mở rộng ra ngoài LAN.
 * ============================================================================ */
#define BMT_OTA_SERVER_BASE "http://192.168.2.23:8080"
#define BMT_OTA_SCANNER_URL BMT_OTA_SERVER_BASE "/Scanner.bin"
#define BMT_OTA_RELAY_URL BMT_OTA_SERVER_BASE "/Relay.bin"
#define BMT_OTA_GATEWAY_URL BMT_OTA_SERVER_BASE "/Gateway.bin"
