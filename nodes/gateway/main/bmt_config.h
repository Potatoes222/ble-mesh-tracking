#pragma once

#define BMT_WIFI_SSID "TP-Link_385B"
#define BMT_WIFI_PASS "91324566"

#define BMT_TB_IP            "192.168.1.113"
#define BMT_TB_HOST          "mqtts://" BMT_TB_IP ":8883"
#define BMT_TB_CN            "bmt-tb.local"
#define BMT_TB_GATEWAY_TOKEN "eo3zw4be9kl6xv8rxsj5"

#define BMT_OTA_SERVER_BASE         "http://192.168.1.113:8080"
#define BMT_OTA_GATEWAY_URL         BMT_OTA_SERVER_BASE "/Gateway.bin"
#define BMT_OTA_RELAY_URL           BMT_OTA_SERVER_BASE "/Relay.bin"
#define BMT_OTA_INTER_NODE_DELAY_MS 60000

#define BMT_DEV_NAME_GATEWAY   "gateway"
#define BMT_DEV_NAME_NODE_FMT  "bmt_node_0x%04x"
#define BMT_DEV_NAME_SCAN_FMT  "scan_0x%04x"
#define BMT_DEV_NAME_RELAY_FMT "relay_0x%04x"
#define BMT_DEV_NAME_TAG_FMT   "tag_0x%04x"

#define BMT_ROLE_GATEWAY "gateway"
#define BMT_ROLE_RELAY   "relay"
#define BMT_ROLE_SCAN    "scan"
#define BMT_ROLE_TAG     "tag"

#define BMT_PROFILE_TAG  "ble_tag"
#define BMT_PROFILE_NODE "ble_mesh_node"

#define BMT_MAX_SCANNERS        8
#define BMT_MAX_TRACKED_TAGS    16
#define BMT_ZONE_HYSTERESIS_DBM 5
#define BMT_SCANNER_VALID_MS    3500
#define BMT_TAG_OUT_OF_RANGE_MS 10000
#define BMT_ZONE_UNKNOWN        0xFF

#define BMT_MQTT_QUEUE_SIZE         64
#define BMT_MQTT_PUBLISH_TIMEOUT_MS 500

#define BMT_MAX_NODES     10
#define BMT_MAX_SCAN_LIST 10
#define BMT_MAX_MAC_CACHE 16
#define BMT_MAX_SEEN_TAGS 16

#define BMT_RELAY_PING_INTERVAL_MS   20000
#define BMT_RELAY_OFFLINE_TIMEOUT_MS 60000
#define BMT_SCAN_DURATION_MS         10000
#define BMT_WDT_TIMEOUT_S            60

#define BMT_NVS_NAMESPACE "bmt_gw"
#define BMT_NVS_KEY_NODES "node_table"

#define BMT_UART_NUM         UART_NUM_0
#define BMT_UART_BAUD        115200
#define BMT_UART_RX_BUF_SIZE 2048
