#include "bmt_zone.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bmt_tag_track_t s_tags[BMT_MAX_TRACKED_TAGS];

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

uint8_t bmt_zone_evaluate(bmt_tag_track_t* t)
{
	uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
	int best_rssi = INT_MIN;
	uint8_t best_scanner = BMT_ZONE_UNKNOWN;
	int current_rssi = INT_MIN;
	bool current_fresh = false;

	for (int i = 0; i < BMT_MAX_SCANNERS; i++)
	{
		if (!t->valid_by_scanner[i])
			continue;
		if (now - t->ts_by_scanner[i] > BMT_SCANNER_VALID_MS)
		{
			t->valid_by_scanner[i] = false;
			continue;
		}
		if ((int)t->rssi_by_scanner[i] > best_rssi)
		{
			best_rssi = t->rssi_by_scanner[i];
			best_scanner = i + 1;
		}
		if ((i + 1) == t->current_zone_id)
		{
			current_rssi = t->rssi_by_scanner[i];
			current_fresh = true;
		}
	}

	if (best_scanner == BMT_ZONE_UNKNOWN)
		return BMT_ZONE_UNKNOWN;
	if (t->current_zone_id == BMT_ZONE_UNKNOWN || !current_fresh)
		return best_scanner;
	if (best_scanner != t->current_zone_id)
		if ((best_rssi - current_rssi) < BMT_ZONE_HYSTERESIS_DBM)
			return t->current_zone_id;
	return best_scanner;
}

const char* bmt_zone_name(uint8_t scanner_id)
{
	switch (scanner_id)
	{
	case 0x01:
		return "bedroom_1";
	case 0x02:
		return "bedroom_2";
	case 0x03:
		return "toilet";
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
