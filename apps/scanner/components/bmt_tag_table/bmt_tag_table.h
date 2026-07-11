#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bmt_types.h"

#define BMT_MAX_TAGS 20
#define BMT_TAG_TIMEOUT_MS 5000
#define BMT_PATH_LOSS_N 2.5f
#define BMT_LOG_RSSI_THRESHOLD_DBM 3
#define BMT_LOG_MIN_INTERVAL_MS 2000

/* [SECURITY] Anti-replay — ngưỡng nhảy sequence tối đa được coi là hợp lệ
 * trong khi đang track liên tục 1 tag. Xem giải thích đầy đủ trong bmt_tag_table.c */
#define BMT_MAX_SEQ_JUMP 30

typedef struct
{
	bool active;
	uint8_t tag_type;
	uint16_t tag_id;
	int8_t tx_power;
	int8_t rssi_raw;
	float rssi_filtered;
	float distance;
	uint8_t last_sequence;
	uint32_t total_received;
	uint32_t total_missed;
	uint32_t last_seen_ms;
	uint8_t mac[6];
	int8_t last_logged_rssi;
	uint32_t last_log_ms;
} bmt_scan_tag_info_t;

void bmt_tag_table_reset(void);

int bmt_tag_table_find(uint16_t tag_id);
int bmt_tag_table_add(uint16_t tag_id, uint8_t tag_type, int8_t tx_power,
                      int8_t rssi, uint8_t sequence, const uint8_t* mac);
void bmt_tag_table_update(int idx, int8_t rssi, uint8_t sequence);
void bmt_tag_table_check_timeouts(void);
void bmt_tag_table_print(uint8_t scanner_id);

/* Truy cập read-only cho module khác (vd bmt_mesh.c để publish) — trả về
 * NULL nếu idx ngoài phạm vi hoặc tag không active */
const bmt_scan_tag_info_t* bmt_tag_table_at(int idx);
