#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BMT_MAX_SCANNERS 8
#define BMT_MAX_TRACKED_TAGS 16
#define BMT_ZONE_HYSTERESIS_DBM 5
#define BMT_SCANNER_VALID_MS 3500
#define BMT_TAG_OUT_OF_RANGE_MS 10000
#define BMT_ZONE_UNKNOWN 0xFF

typedef struct
{
	bool active;
	uint16_t tag_id;
	uint8_t tag_type;
	int8_t rssi_by_scanner[BMT_MAX_SCANNERS];
	uint32_t ts_by_scanner[BMT_MAX_SCANNERS];
	bool valid_by_scanner[BMT_MAX_SCANNERS];
	uint8_t current_zone_id;
	uint32_t last_zone_change_ms, last_any_report_ms;
} bmt_tag_track_t;

/* Khoi tao mutex bao ve bang tag — goi 1 lan trong app_main truoc khi
 * bat ky task nao co the goi ham khac trong module nay. */
void bmt_zone_init(void);

/* Take/give mutex bao ve toan bo bang bmt_tag_track_t — caller PHAI gom cac
 * thao tac lien quan (find/get_or_add + update field + evaluate) vao mot cap
 * lock/unlock, khong duoc giu con tro qua bien lock. */
void bmt_zone_lock(void);
void bmt_zone_unlock(void);

int bmt_zone_track_capacity(void);
bmt_tag_track_t* bmt_zone_track_get(int idx);
bmt_tag_track_t* bmt_zone_track_find(uint16_t tag_id);
/* Tạo entry mới nếu chưa track tag_id — trả về NULL nếu bảng đầy */
bmt_tag_track_t* bmt_zone_track_get_or_add(uint16_t tag_id, uint8_t tag_type);

/* Đánh giá scanner nào đang giữ zone hiện tại, có hysteresis chống nhảy zone
 * liên tục khi 2 scanner có RSSI xấp xỉ nhau */
uint8_t bmt_zone_evaluate(bmt_tag_track_t* t);

const char* bmt_zone_name(uint8_t scanner_id);

/* UART '2' — in bảng tag đang track + zone hiện tại */
void bmt_zone_log_tracked(void);

/* Xóa sạch toàn bộ bảng tag đang track — dùng cho UART '0'/'9' và
 * bmt_watchdog.c khi gateway full reset */
void bmt_zone_reset_all(void);
