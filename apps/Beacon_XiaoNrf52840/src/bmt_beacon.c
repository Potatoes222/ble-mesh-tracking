#include "bmt_beacon.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#include "bmt_config.h"
#include "bmt_auth.h"
#include "bmt_types.h"
#include "bmt_battery.h"

LOG_MODULE_REGISTER(bmt_beacon, LOG_LEVEL_INF);

/* Company ID Espressif (0x02E5) — GIU NGUYEN de Scanner (ESP32) hien co
 * van nhan dien duoc tag, du Tag gio chay chip Nordic. Scanner loc theo
 * dung CID nay trong manufacturer data — khong lien quan hang chip that. */
#define BMT_CID_ESPRESSIF 0x02E5

static uint8_t s_sequence = 0;
static bool s_adv_active = false;

/* Buffer manufacturer data = 2 byte CID (little-endian) + 24 byte payload
 * — dung 1 buffer tinh (khong dung stack) vi bt_le_adv_start() doi hoi
 * con tro data hop le tai thoi diem goi, giong cach s_adv_raw ben ESP32. */
static uint8_t s_mfg_data[2 + sizeof(bmt_tag_adv_payload_t)];

static struct k_timer s_seq_timer;
static struct k_work s_seq_work;

static void build_adv_data(void)
{
	bmt_tag_adv_payload_t p;

	memcpy(p.uuid, BMT_SYSTEM_UUID, 16);
	/* Truoc day field nay la major (PERSON/ASSET); gio mang % pin (0-100).
	 * Doc gia tri cache (bmt_battery cap nhat 30s/lan) — khong doc ADC moi
	 * goi ADV de nhe hon cho pin. */
	p.battery = bmt_battery_last_percent();
	p.minor = BMT_TAG_MINOR;
	p.tx_power = BMT_TAG_TX_POWER;
	p.sequence = s_sequence;
	p.mac16 = 0; /* bat buoc = 0 truoc khi tinh HMAC */

	/* HMAC tinh tren tat ca field tru 2 byte mac16 cuoi — GIONG HET
	 * cong thuc ben ESP32, chi khac API goi (bmt_auth_hmac16 dung chung). */
	p.mac16 = bmt_auth_hmac16((uint8_t*)&p, sizeof(p) - sizeof(p.mac16));

	s_mfg_data[0] = (uint8_t)(BMT_CID_ESPRESSIF & 0xFF);
	s_mfg_data[1] = (uint8_t)(BMT_CID_ESPRESSIF >> 8);
	memcpy(s_mfg_data + 2, &p, sizeof(p));
}

static void start_adv_random_interval(void)
{
	/* Random [900, 1100] ms -> tranh collision nhieu tag, GIONG HET ESP32 */
	uint32_t ms = BMT_ADV_INTERVAL_MIN_MS +
	              (sys_rand32_get() % (BMT_ADV_INTERVAL_MAX_MS - BMT_ADV_INTERVAL_MIN_MS + 1));

	/* [FIX POWER] s_seq_timer fire dung luc "ms" nay het (one-shot, tu
	 * khoi dong lai moi lan o day) thay vi chu ky co dinh 500ms. Truoc day
	 * timer fix 500ms trong khi radio duoc set interval 900-1100ms -> cu
	 * ~500ms lai bt_le_adv_stop()+start giua chung, radio chua chay het 1
	 * chu ky da bi restart -> dong trung binh gan gap doi. Doi one-shot khop
	 * dung "ms" that dang dung cho radio. (Luu y: dung signed-merge de flash,
	 * xem _fw_backup + build notes — flash app chua ky se bi mcuboot tu choi.) */
	k_timer_start(&s_seq_timer, K_MSEC(ms), K_NO_WAIT);

	/* BLE unit = 0.625ms — cung don vi voi ESP-IDF, cong thuc doi giong het */
	uint16_t units = (uint16_t)((ms * 1000) / 625);

	build_adv_data();

	const struct bt_le_adv_param param = {
		.id = BT_ID_DEFAULT,
		.sid = 0,
		.secondary_max_skip = 0,
		.options = BT_LE_ADV_OPT_USE_IDENTITY, /* dia chi cong khai, khong dung private/random rotating */
		.interval_min = units,
		.interval_max = units,
		.peer = NULL,
	};

	const struct bt_data ad[] = {
		/* Flags = 0x06 (LE General Discoverable | No BR/EDR) — GIONG HET gia
		 * tri hardcode ben ESP32 */
		BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
		BT_DATA(BT_DATA_MANUFACTURER_DATA, s_mfg_data, sizeof(s_mfg_data)),
	};

	int err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("ADV start FAILED (err %d)", err);
		s_adv_active = false;
		return;
	}
	s_adv_active = true;
	LOG_DBG("ADV OK seq=%u pin=%u%% minor=0x%04X", s_sequence,
	        bmt_battery_last_percent(), BMT_TAG_MINOR);
}

static void seq_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);
	s_sequence++; /* uint8_t: 255 -> 0 tu dong, giong ESP32 */

	bt_le_adv_stop();
	start_adv_random_interval();
}

static void seq_timer_handler(struct k_timer* timer)
{
	/* [QUAN TRONG] k_timer callback chay trong ISR context — KHONG duoc
	 * goi truc tiep bt_le_adv_stop/start o day (co the block, gay loi).
	 * Day cong viec that sang system workqueue qua k_work, giong tinh
	 * than "FreeRTOS timer callback ngan, cong viec nang lam o task rieng"
	 * nhung ESP32 khong bi rang buoc nay chat che nhu Zephyr ISR. */
	ARG_UNUSED(timer);
	k_work_submit(&s_seq_work);
}

int bmt_beacon_start(void)
{
	k_work_init(&s_seq_work, seq_work_handler);

	/* [FIX POWER] Timer one-shot, tu dat lai thoi gian trong
	 * start_adv_random_interval() (khong con auto-reload 500ms co dinh). */
	k_timer_init(&s_seq_timer, seq_timer_handler, NULL);

	/* Bat dau advertise lan dau — ham nay tu k_timer_start() cho lan dau */
	start_adv_random_interval();

	return 0;
}

uint8_t bmt_beacon_sequence(void)
{
	return s_sequence;
}

bool bmt_beacon_is_active(void)
{
	return s_adv_active;
}

uint16_t bmt_beacon_last_mac16(void)
{
	bmt_tag_adv_payload_t p;
	memcpy(&p, s_mfg_data + 2, sizeof(p));
	return p.mac16;
}
