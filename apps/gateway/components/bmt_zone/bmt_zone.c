#include "bmt_zone.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static bmt_tag_track_t s_tags[BMT_MAX_TRACKED_TAGS];
static SemaphoreHandle_t s_lock = NULL;

void bmt_zone_init(void)
{
	if (!s_lock)
		s_lock = xSemaphoreCreateMutex();
}

void bmt_zone_lock(void)
{
	if (s_lock)
		xSemaphoreTake(s_lock, portMAX_DELAY);
}

void bmt_zone_unlock(void)
{
	if (s_lock)
		xSemaphoreGive(s_lock);
}

int bmt_zone_track_capacity(void)
{
	return BMT_MAX_TRACKED_TAGS;
}

void bmt_zone_reset_all(void)
{
	memset(s_tags, 0, sizeof(s_tags));
}

bmt_tag_track_t* bmt_zone_track_get(int idx)
{
	if (idx < 0 || idx >= BMT_MAX_TRACKED_TAGS)
		return NULL;
	return &s_tags[idx];
}

bmt_tag_track_t* bmt_zone_track_find(uint16_t tag_id)
{
	for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++)
		if (s_tags[i].active && s_tags[i].tag_id == tag_id)
			return &s_tags[i];
	return NULL;
}

bmt_tag_track_t* bmt_zone_track_get_or_add(uint16_t tag_id, uint8_t tag_type)
{
	bmt_tag_track_t* t = bmt_zone_track_find(tag_id);
	if (t)
		return t;
	for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++)
	{
		if (!s_tags[i].active)
		{
			memset(&s_tags[i], 0, sizeof(s_tags[i]));
			s_tags[i].active = true;
			s_tags[i].tag_id = tag_id;
			s_tags[i].tag_type = tag_type;
			s_tags[i].current_zone_id = BMT_ZONE_UNKNOWN;
			return &s_tags[i];
		}
	}
	return NULL;
}

const char* bmt_zone_name(uint8_t scanner_id)
{
	switch (scanner_id)
	{
	case 0x01:
		return "room_1";
	case 0x02:
		return "room_2";
	case 0x03:
		return "room_3";
	case BMT_ZONE_UNKNOWN:
		return "out_of_range";
	default:
		return "unknown";
	}
}

void bmt_zone_log_tracked(void)
{
	uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
	printf("\n========== TRACKED TAGS ==========\n");
	bool any = false;
	for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++)
	{
		bmt_tag_track_t* t = &s_tags[i];
		if (!t->active)
			continue;
		any = true;
		printf("Tag 0x%04x (%s)\n", t->tag_id, t->tag_type == 0x01 ? "PERSON" : "ASSET");
		printf("  Zone       : %s (0x%02x)\n", bmt_zone_name(t->current_zone_id), t->current_zone_id);
		if (t->last_zone_change_ms > 0)
			printf("  Zone since : %" PRIu32 "s ago\n", (now - t->last_zone_change_ms) / 1000);
		printf("  Last report: %" PRIu32 "s ago\n", (now - t->last_any_report_ms) / 1000);
		printf("  Scanners   :\n");
		for (int j = 0; j < BMT_MAX_SCANNERS; j++)
		{
			if (!t->valid_by_scanner[j])
				continue;
			uint32_t age = now - t->ts_by_scanner[j];
			printf("    0x%02x %-12s RSSI=%4d  %s\n", j + 1, bmt_zone_name(j + 1),
			       t->rssi_by_scanner[j], age <= BMT_SCANNER_VALID_MS ? "FRESH" : "STALE");
		}
		printf("----------------------------------\n");
	}
	if (!any)
		printf("  No tracked tags\n----------------------------------\n");
}
