#include "bmt_tag_table.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "BMT_TAGTBL";

typedef struct
{
	float q, r, x, p, k;
	float r_var; /* [ADAPTIVE] uoc luong nhieu do (EMA cua innovation^2) */
} bmt_kalman_t;

/* [ADAPTIVE R] R khong con fix cung 2.0 — tu dieu chinh theo do bien thien
 * thuc te cua RSSI (innovation = rssi_do - rssi_da_loc). RSSI cang nhay
 * (multipath, nguoi che tag) -> r_var tang -> K giam -> loc manh hon.
 * RSSI on dinh -> r_var giam -> K tang -> bam sat mau moi nhanh hon, giam
 * do tre. R_ALPHA nho = thich nghi cham (on dinh hon, chong nhieu tuc thoi
 * lam r nhay lung tung); r_min/r_max chan de tranh K=0 (dung yen hoan toan)
 * hoac K=1 (khong loc gi ca) khi gap sample dot bien. */
#define BMT_KALMAN_R_ALPHA 0.1f
#define BMT_KALMAN_R_MIN 1.0f
#define BMT_KALMAN_R_MAX 20.0f

static bmt_scan_tag_info_t s_tags[BMT_MAX_TAGS];
static bmt_kalman_t s_kalman[BMT_MAX_TAGS];
static void kalman_init(bmt_kalman_t* kf, float initial_rssi)
{
	kf->q = 0.1f;
	kf->r = 2.0f;
	kf->x = initial_rssi;
	kf->p = 1.0f;
	kf->k = 0.0f;
	kf->r_var = 2.0f; /* seed = R mac dinh cu, hoi tu dan theo du lieu thuc */
}

static float kalman_update(bmt_kalman_t* kf, float rssi)
{
	float innovation = rssi - kf->x;

	/* [ADAPTIVE R] cap nhat uoc luong nhieu do truoc, roi moi dung no cho
	 * buoc Kalman ngay ben duoi (thay vi r co dinh 2.0f) */
	kf->r_var = (1.0f - BMT_KALMAN_R_ALPHA) * kf->r_var + BMT_KALMAN_R_ALPHA * (innovation * innovation);
	kf->r = kf->r_var;
	if (kf->r < BMT_KALMAN_R_MIN)
		kf->r = BMT_KALMAN_R_MIN;
	else if (kf->r > BMT_KALMAN_R_MAX)
		kf->r = BMT_KALMAN_R_MAX;

	kf->p = kf->p + kf->q;
	kf->k = kf->p / (kf->p + kf->r);
	kf->x = kf->x + kf->k * innovation;
	kf->p = (1.0f - kf->k) * kf->p;
	return kf->x;
}
static float calculate_distance(int8_t tx_power, float rssi_filtered)
{
	if (rssi_filtered >= 0)
		return 0.0f;
	float ratio = (tx_power - rssi_filtered) / (10.0f * BMT_PATH_LOSS_N);
	return powf(10.0f, ratio);
}

void bmt_tag_table_reset(void)
{
	memset(s_tags, 0, sizeof(s_tags));
	memset(s_kalman, 0, sizeof(s_kalman));
}

int bmt_tag_table_find(uint16_t tag_id)
{
	for (int i = 0; i < BMT_MAX_TAGS; i++)
		if (s_tags[i].active && s_tags[i].tag_id == tag_id)
			return i;
	return -1;
}

int bmt_tag_table_add(uint16_t tag_id, uint8_t battery,
                      int8_t tx_power, int8_t rssi,
                      uint8_t sequence, const uint8_t* mac)
{
	for (int i = 0; i < BMT_MAX_TAGS; i++)
	{
		if (!s_tags[i].active)
		{
			memset(&s_tags[i], 0, sizeof(bmt_scan_tag_info_t));
			s_tags[i].active = true;
			s_tags[i].battery = battery;
			s_tags[i].tag_id = tag_id;
			s_tags[i].tx_power = tx_power;
			s_tags[i].rssi_raw = rssi;
			s_tags[i].rssi_filtered = (float)rssi;
			s_tags[i].last_sequence = sequence;
			s_tags[i].total_received = 1;
			s_tags[i].last_seen_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
			s_tags[i].last_logged_rssi = rssi;
			s_tags[i].last_log_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
			s_tags[i].locked_epoch = -1;
			if (mac)
				memcpy(s_tags[i].mac, mac, 6);
			kalman_init(&s_kalman[i], (float)rssi);
			s_tags[i].distance = calculate_distance(tx_power, (float)rssi);
			ESP_LOGI(TAG, "New tag: 0x%04X (pin %u%%) RSSI=%ddBm",
			         tag_id, battery, rssi);
			return i;
		}
	}
	ESP_LOGW(TAG, "Tag table full!");
	return -1;
}

void bmt_tag_table_set_epoch(int idx, int32_t epoch)
{
	if (idx < 0 || idx >= BMT_MAX_TAGS || !s_tags[idx].active)
		return;
	s_tags[idx].locked_epoch = epoch;
}

void bmt_tag_table_update(int idx, int8_t rssi, uint8_t sequence, uint8_t battery)
{
	bmt_scan_tag_info_t* t = &s_tags[idx];
	uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

	t->battery = battery; /* cap nhat % pin moi goi (tag chi add 1 lan) */

	if (sequence == t->last_sequence)
	{
		t->rssi_raw = rssi;
		t->rssi_filtered = kalman_update(&s_kalman[idx], (float)rssi);
		t->distance = calculate_distance(t->tx_power, t->rssi_filtered);
		t->last_seen_ms = now;
		t->total_received++;
		return;
	}

	int8_t diff = (int8_t)(sequence - t->last_sequence);

	/* [SECURITY] Anti-replay lớp 1: loại bỏ gói lùi lại gần đây trong cửa sổ
	 * nhỏ (đến trễ do overlap kênh quảng cáo, hoặc replay 1 gói vừa capture) */
	if (diff < 0 && diff > -10)
		return;

	if (diff < -10 || diff > BMT_MAX_SEQ_JUMP)
	{
		ESP_LOGW(TAG, "Tag 0x%04X: sequence nhay bat thuong (%u -> %u, diff=%d)"
		              " - reset tracking (co the la Tag reboot hoac goi bi replay)",
		         t->tag_id, t->last_sequence, sequence, diff);
		kalman_init(&s_kalman[idx], (float)rssi);
		t->rssi_raw = rssi;
		t->rssi_filtered = (float)rssi;
		t->distance = calculate_distance(t->tx_power, (float)rssi);
		t->last_sequence = sequence;
		t->total_received++;
		t->last_seen_ms = now;
		return;
	}

	uint8_t expected = t->last_sequence + 1;
	if (sequence != expected)
	{
		uint8_t missed = (diff > 1) ? (uint8_t)(diff - 1) : 0;
		if (missed > 0 && missed < 200)
			t->total_missed += missed;
	}

	t->rssi_raw = rssi;
	t->rssi_filtered = kalman_update(&s_kalman[idx], (float)rssi);
	t->distance = calculate_distance(t->tx_power, t->rssi_filtered);
	t->last_sequence = sequence;
	t->total_received++;
	t->last_seen_ms = now;

	int8_t rd = (int8_t)abs((int)rssi - (int)t->last_logged_rssi);
	uint32_t td = now - t->last_log_ms;
	if (rd >= BMT_LOG_RSSI_THRESHOLD_DBM || td >= BMT_LOG_MIN_INTERVAL_MS)
	{
		ESP_LOGI(TAG, "Tag 0x%04X | RSSI=%ddBm | Filt=%.1fdBm | Dist=%.2fm",
		         t->tag_id, rssi, t->rssi_filtered, t->distance);
		t->last_logged_rssi = rssi;
		t->last_log_ms = now;
	}
}

void bmt_tag_table_check_timeouts(void)
{
	uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
	for (int i = 0; i < BMT_MAX_TAGS; i++)
	{
		if (!s_tags[i].active)
			continue;
		if ((now - s_tags[i].last_seen_ms) > BMT_TAG_TIMEOUT_MS)
		{
			ESP_LOGW(TAG, "Tag 0x%04X OUT", s_tags[i].tag_id);
			s_tags[i].active = false;
			memset(&s_kalman[i], 0, sizeof(bmt_kalman_t));
		}
	}
}

void bmt_tag_table_print(uint8_t scanner_id)
{
	uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
	bool has_tag = false;
	printf("\n========== TAG TABLE (Scanner 0x%02X) ==========\n", scanner_id);
	for (int i = 0; i < BMT_MAX_TAGS; i++)
	{
		if (!s_tags[i].active)
			continue;
		has_tag = true;
		bmt_scan_tag_info_t* t = &s_tags[i];
		uint32_t age = (now - t->last_seen_ms) / 1000;
		uint32_t tot = t->total_received + t->total_missed;
		float lr = (tot > 0)
		               ? (float)t->total_missed / tot * 100.0f
		               : 0.0f;
		printf("Tag 0x%04X | Pin=%u%% | RSSI=%d | Filt=%.1f | Dist=%.2fm"
		       " | Loss=%.1f%% | Epoch=%ld | %lus ago\n",
		       t->tag_id, t->battery,
		       t->rssi_raw, t->rssi_filtered, t->distance, lr,
		       (long)t->locked_epoch, (unsigned long)age);
	}
	if (!has_tag)
		printf("  No tags in range\n");
	printf("--------------------------------------------------\n");
}

const bmt_scan_tag_info_t* bmt_tag_table_at(int idx)
{
	if (idx < 0 || idx >= BMT_MAX_TAGS)
		return NULL;
	if (!s_tags[idx].active)
		return NULL;
	return &s_tags[idx];
}
