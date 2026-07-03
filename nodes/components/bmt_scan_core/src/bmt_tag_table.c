#include "bmt_tag_table.h"

static const char *TAG = "BMT_TAGS";

static bmt_tag_entry_t s_tags[BMT_MAX_TAGS];
static bmt_kalman_t    s_kalman[BMT_MAX_TAGS];
static volatile bool   s_new_data = false;

static void kalman_init(bmt_kalman_t *kf, float initial_rssi) {
    kf->q = 0.1f;
    kf->r = 2.0f;
    kf->x = initial_rssi;
    kf->p = 1.0f;
    kf->k = 0.0f;
}

static float kalman_update(bmt_kalman_t *kf, float rssi) {
    kf->p = kf->p + kf->q;
    kf->k = kf->p / (kf->p + kf->r);
    kf->x = kf->x + kf->k * (rssi - kf->x);
    kf->p = (1.0f - kf->k) * kf->p;
    return kf->x;
}

static float calc_distance(int8_t tx_power, float rssi_filtered) {
    if (rssi_filtered >= 0) return 0.0f;
    float ratio = (tx_power - rssi_filtered) / (10.0f * BMT_PATH_LOSS_N);
    return powf(10.0f, ratio);
}

static int find_by_id(uint16_t tag_id) {
    for (int i = 0; i < BMT_MAX_TAGS; i++)
        if (s_tags[i].active && s_tags[i].tag_id == tag_id) return i;
    return -1;
}

static int add_new(const bmt_tag_hit_t *hit) {
    for (int i = 0; i < BMT_MAX_TAGS; i++) {
        if (s_tags[i].active) continue;
        memset(&s_tags[i], 0, sizeof(bmt_tag_entry_t));
        s_tags[i].active           = true;
        s_tags[i].tag_type         = hit->tag_type;
        s_tags[i].tag_id           = hit->tag_id;
        s_tags[i].tx_power         = hit->tx_power;
        s_tags[i].rssi_raw         = hit->rssi;
        s_tags[i].rssi_filtered    = (float)hit->rssi;
        s_tags[i].last_sequence    = hit->sequence;
        s_tags[i].total_received   = 1;
        s_tags[i].last_seen_ms     = xTaskGetTickCount() * portTICK_PERIOD_MS;
        s_tags[i].last_logged_rssi = hit->rssi;
        s_tags[i].last_log_ms      = s_tags[i].last_seen_ms;
        memcpy(s_tags[i].mac, hit->mac, 6);
        kalman_init(&s_kalman[i], (float)hit->rssi);
        s_tags[i].distance = calc_distance(hit->tx_power, (float)hit->rssi);
        ESP_LOGI(TAG, "New tag: 0x%04X (%s) RSSI=%ddBm Dist=%.2fm", hit->tag_id,
                 hit->tag_type == BMT_TAG_TYPE_PERSON ? "PERSON" : "ASSET", hit->rssi,
                 s_tags[i].distance);
        return i;
    }
    ESP_LOGW(TAG, "Tag table full!");
    return -1;
}

static void maybe_log_update(bmt_tag_entry_t *t, uint8_t sequence, uint32_t now) {
    int8_t   rd = (int8_t)abs((int)t->rssi_raw - (int)t->last_logged_rssi);
    uint32_t td = now - t->last_log_ms;
    if (rd >= BMT_LOG_RSSI_THRESHOLD_DBM || td >= BMT_LOG_MIN_INTERVAL_MS) {
        ESP_LOGI(TAG, "Tag 0x%04X | RSSI=%ddBm | Filt=%.1fdBm | Dist=%.2fm | Seq=%d", t->tag_id,
                 t->rssi_raw, t->rssi_filtered, t->distance, sequence);
        t->last_logged_rssi = t->rssi_raw;
        t->last_log_ms      = now;
    }
}

static void apply_update(int idx, int8_t rssi, uint8_t sequence) {
    bmt_tag_entry_t *t   = &s_tags[idx];
    uint32_t         now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (sequence == t->last_sequence) {
        t->rssi_raw      = rssi;
        t->rssi_filtered = kalman_update(&s_kalman[idx], (float)rssi);
        t->distance      = calc_distance(t->tx_power, t->rssi_filtered);
        t->last_seen_ms  = now;
        t->total_received++;
        maybe_log_update(t, sequence, now);
        return;
    }

    int8_t diff = (int8_t)(sequence - t->last_sequence);
    if (diff < 0 && diff > -10) {
        ESP_LOGD(TAG, "Tag 0x%04X backward skip", t->tag_id);
        return;
    }

    uint8_t expected = t->last_sequence + 1;
    if (sequence != expected) {
        uint8_t missed = 0;
        if (diff > 1)
            missed = (uint8_t)(diff - 1);
        else if (diff < -10)
            missed = (uint8_t)(256 - t->last_sequence - 1 + sequence);
        if (missed > 0 && missed < 200) {
            t->total_missed += missed;
            ESP_LOGW(TAG, "Tag 0x%04X miss %d exp=%d got=%d", t->tag_id, missed, expected,
                     sequence);
        }
    }

    t->rssi_raw      = rssi;
    t->rssi_filtered = kalman_update(&s_kalman[idx], (float)rssi);
    t->distance      = calc_distance(t->tx_power, t->rssi_filtered);
    t->last_sequence = sequence;
    t->total_received++;
    t->last_seen_ms = now;
    s_new_data      = true;

    maybe_log_update(t, sequence, now);
}

void bmt_tag_table_reset(void) {
    memset(s_tags, 0, sizeof(s_tags));
    memset(s_kalman, 0, sizeof(s_kalman));
    s_new_data = false;
}

void bmt_tag_table_update(const bmt_tag_hit_t *hit) {
    int idx = find_by_id(hit->tag_id);
    if (idx < 0) {
        idx = add_new(hit);
        if (idx >= 0) s_new_data = true;
        return;
    }
    apply_update(idx, hit->rssi, hit->sequence);
}

void bmt_tag_table_check_timeouts(void) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < BMT_MAX_TAGS; i++) {
        if (!s_tags[i].active) continue;
        if ((now - s_tags[i].last_seen_ms) > BMT_TAG_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Tag 0x%04X OUT OF RANGE", s_tags[i].tag_id);
            s_tags[i].active = false;
            memset(&s_kalman[i], 0, sizeof(bmt_kalman_t));
        }
    }
}

void bmt_tag_table_print(uint8_t scanner_id) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool     any = false;
    printf("\n========== TAG TABLE (Scanner 0x%02X) ==========\n", scanner_id);
    for (int i = 0; i < BMT_MAX_TAGS; i++) {
        if (!s_tags[i].active) continue;
        any                  = true;
        bmt_tag_entry_t *t   = &s_tags[i];
        uint32_t         age = (now - t->last_seen_ms) / 1000;
        uint32_t         tot = t->total_received + t->total_missed;
        float            lr  = tot ? (float)t->total_missed / tot * 100.0f : 0.0f;
        printf("Tag 0x%04X:\n", t->tag_id);
        printf("  Type      : %s\n", t->tag_type == BMT_TAG_TYPE_PERSON ? "PERSON" : "ASSET");
        printf("  MAC       : %02X:%02X:%02X:%02X:%02X:%02X\n", t->mac[0], t->mac[1], t->mac[2],
               t->mac[3], t->mac[4], t->mac[5]);
        printf("  RSSI raw  : %d dBm\n", t->rssi_raw);
        printf("  RSSI filt : %.1f dBm\n", t->rssi_filtered);
        printf("  Distance  : %.2f m\n", t->distance);
        printf("  TX Power  : %d dBm\n", t->tx_power);
        printf("  Sequence  : %d\n", t->last_sequence);
        printf("  Received  : %lu\n", t->total_received);
        printf("  Missed    : %lu\n", t->total_missed);
        printf("  Loss rate : %.1f%%\n", lr);
        printf("  Last seen : %lus ago\n", age);
        printf("--------------------------------------------------\n");
    }
    if (!any) printf("  No tags in range\n--------------------------------------------------\n");
}

bool bmt_tag_table_has_new_data(void) {
    return s_new_data;
}
void bmt_tag_table_clear_new_data(void) {
    s_new_data = false;
}

bool bmt_tag_table_get_snapshot(int index, bmt_tag_snapshot_t *out) {
    if (index < 0 || index >= BMT_MAX_TAGS || !out) return false;
    bmt_tag_entry_t *t = &s_tags[index];
    if (!t->active) return false;

    out->active         = true;
    out->tag_type       = t->tag_type;
    out->tag_id         = t->tag_id;
    out->rssi_int       = (int8_t)t->rssi_filtered;
    out->rssi_filtered  = t->rssi_filtered;
    out->distance       = t->distance;
    out->total_received = t->total_received;
    out->total_missed   = t->total_missed;
    return true;
}
