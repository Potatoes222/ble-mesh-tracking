#include "bmt_zone.h"
#include "bmt_config.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BMT_ZONE";

typedef struct {
    bool     active;
    uint16_t tag_id;
    uint8_t  tag_type;
    int8_t   rssi_by_scanner[BMT_MAX_SCANNERS];
    uint32_t ts_by_scanner[BMT_MAX_SCANNERS];
    bool     valid_by_scanner[BMT_MAX_SCANNERS];
    uint8_t  current_zone_id;
    uint32_t last_zone_change_ms;
    uint32_t last_any_report_ms;
} track_t;

static track_t s_tracks[BMT_MAX_TRACKED_TAGS];

const char *bmt_zone_name(uint8_t scanner_id) {
    switch (scanner_id) {
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

static track_t *find_track(uint16_t tag_id) {
    for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++)
        if (s_tracks[i].active && s_tracks[i].tag_id == tag_id) return &s_tracks[i];
    return NULL;
}

static track_t *get_or_add(uint16_t tag_id, uint8_t tag_type) {
    track_t *t = find_track(tag_id);
    if (t) return t;
    for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++) {
        if (s_tracks[i].active) continue;
        memset(&s_tracks[i], 0, sizeof(s_tracks[i]));
        s_tracks[i].active          = true;
        s_tracks[i].tag_id          = tag_id;
        s_tracks[i].tag_type        = tag_type;
        s_tracks[i].current_zone_id = BMT_ZONE_UNKNOWN;
        ESP_LOGI(TAG, "New tag tracked: 0x%04x", tag_id);
        return &s_tracks[i];
    }
    return NULL;
}

static uint8_t evaluate(track_t *t) {
    uint32_t now           = xTaskGetTickCount() * portTICK_PERIOD_MS;
    int      best_rssi     = INT_MIN;
    uint8_t  best_scanner  = BMT_ZONE_UNKNOWN;
    int      current_rssi  = INT_MIN;
    bool     current_fresh = false;

    for (int i = 0; i < BMT_MAX_SCANNERS; i++) {
        if (!t->valid_by_scanner[i]) continue;
        if (now - t->ts_by_scanner[i] > BMT_SCANNER_VALID_MS) {
            t->valid_by_scanner[i] = false;
            continue;
        }
        if ((int)t->rssi_by_scanner[i] > best_rssi) {
            best_rssi    = t->rssi_by_scanner[i];
            best_scanner = i + 1;
        }
        if ((i + 1) == t->current_zone_id) {
            current_rssi  = t->rssi_by_scanner[i];
            current_fresh = true;
        }
    }

    if (best_scanner == BMT_ZONE_UNKNOWN) return BMT_ZONE_UNKNOWN;
    if (t->current_zone_id == BMT_ZONE_UNKNOWN || !current_fresh) return best_scanner;
    if (best_scanner != t->current_zone_id && (best_rssi - current_rssi) < BMT_ZONE_HYSTERESIS_DBM)
        return t->current_zone_id;
    return best_scanner;
}

void bmt_zone_ingest_report(uint8_t scanner_id, uint16_t tag_id, uint8_t tag_type, int8_t rssi) {
    if (scanner_id < 1 || scanner_id > BMT_MAX_SCANNERS) {
        ESP_LOGW(TAG, "Scanner ID 0x%02x out of range", scanner_id);
        return;
    }

    track_t *t = get_or_add(tag_id, tag_type);
    if (!t) {
        ESP_LOGW(TAG, "Track table full for tag 0x%04x", tag_id);
        return;
    }

    int      sidx             = scanner_id - 1;
    uint32_t now              = xTaskGetTickCount() * portTICK_PERIOD_MS;
    t->rssi_by_scanner[sidx]  = rssi;
    t->ts_by_scanner[sidx]    = now;
    t->valid_by_scanner[sidx] = true;
    t->last_any_report_ms     = now;

    uint8_t new_zone = evaluate(t);
    if (new_zone != t->current_zone_id) {
        ESP_LOGI(TAG, "[LOCAL] Tag 0x%04x ZONE CHANGE: %s -> %s", tag_id,
                 bmt_zone_name(t->current_zone_id), bmt_zone_name(new_zone));
        t->current_zone_id     = new_zone;
        t->last_zone_change_ms = now;
    }
}

void bmt_zone_print_all(void) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    printf("\n========== TRACKED TAGS ==========\n");
    bool any = false;
    for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++) {
        track_t *t = &s_tracks[i];
        if (!t->active) continue;
        any = true;
        printf("Tag 0x%04x (type=%s)\n", t->tag_id, t->tag_type == 0x01 ? "PERSON" : "ASSET");
        printf("  Zone           : %s (id=0x%02x)\n", bmt_zone_name(t->current_zone_id),
               t->current_zone_id);
        if (t->last_zone_change_ms > 0)
            printf("  Zone since     : %" PRIu32 "s ago\n", (now - t->last_zone_change_ms) / 1000);
        printf("  Last report    : %" PRIu32 "s ago\n", (now - t->last_any_report_ms) / 1000);
        printf("  Scanners seen  :\n");
        bool any_scanner = false;
        for (int j = 0; j < BMT_MAX_SCANNERS; j++) {
            if (t->ts_by_scanner[j] == 0) continue;
            any_scanner        = true;
            uint32_t    age    = now - t->ts_by_scanner[j];
            const char *status = age <= BMT_SCANNER_VALID_MS      ? "FRESH"
                                 : age <= BMT_TAG_OUT_OF_RANGE_MS ? "STALE"
                                                                  : "EXPIRED";
            printf("    0x%02x (%-12s): RSSI=%4d  age=%4us  %s\n", j + 1, bmt_zone_name(j + 1),
                   t->rssi_by_scanner[j], (unsigned)(age / 1000), status);
        }
        if (!any_scanner) printf("    (none seen yet)\n");
        printf("----------------------------------\n");
    }
    if (!any) printf("  No tracked tags\n----------------------------------\n");
}

static void timeout_task(void *arg) {
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        for (int i = 0; i < BMT_MAX_TRACKED_TAGS; i++) {
            track_t *t = &s_tracks[i];
            if (!t->active) continue;
            if ((now - t->last_any_report_ms) <= BMT_TAG_OUT_OF_RANGE_MS) continue;
            if (t->current_zone_id == BMT_ZONE_UNKNOWN) continue;

            ESP_LOGW(TAG, "[LOCAL] Tag 0x%04x OUT OF RANGE", t->tag_id);
            t->current_zone_id     = BMT_ZONE_UNKNOWN;
            t->last_zone_change_ms = now;
            for (int j = 0; j < BMT_MAX_SCANNERS; j++)
                t->valid_by_scanner[j] = false;
        }
    }
}

void bmt_zone_start_timeout_task(void) {
    xTaskCreate(timeout_task, "bmt_zone_timer", 3072, NULL, 3, NULL);
}
