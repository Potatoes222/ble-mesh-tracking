#include "bmt_battery.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bmt_battery, LOG_LEVEL_INF);

/* Pin noi THANG vao VDDH cua nRF52840 (nice!nano v2 high-voltage mode). Doc
 * qua VDDHDIV5 noi bo cua SAADC — khong can chia ap ngoai va khong can GPIO
 * enable (khac ban XIAO dung P0.31 + P0.14). */
static const struct adc_dt_spec battery_adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

/* VDDHDIV5: gia tri doc duoc = VDDH / 5 -> nhan lai 5 de ra dien ap pin that.
 * (Thay cho cong thuc (R1+R2)/R2 cua bo chia ap ngoai ben XIAO.) */
#define BMT_BATT_VDDH_DIVIDER 5

/* Bang tra dien ap -> % cho Li-ion/LiPo. Duong cong SoC-vs-ap cua Li-ion PHI
 * TUYEN manh, khong the dung 1 cong thuc tuyen tinh -> tra bang + noi suy giua
 * 2 diem. Moc 4200mV=100% (day), 3300mV=0% (nguong an toan toi thieu, duoi nua
 * hong pin). GIU NGUYEN y het ban XIAO — cung hoa hoc pin, duong cong khong doi. */
typedef struct {
	uint16_t mv;
	uint8_t pct;
} batt_point_t;

static const batt_point_t BATT_CURVE[] = {
	{4200, 100}, {4110, 90}, {4020, 80}, {3930, 70}, {3840, 60},
	{3750, 50},  {3660, 40}, {3570, 30}, {3480, 20}, {3390, 10},
	{3300, 0},
};
#define BATT_CURVE_N (sizeof(BATT_CURVE) / sizeof(BATT_CURVE[0]))

#define BMT_BATT_LOG_INTERVAL_SEC 30

static struct k_timer batt_log_timer;
static struct k_work batt_log_work;

/* % pin cache — beacon doc cai nay nhet vao payload (khong doc ADC moi goi ADV).
 * Cap nhat boi work handler (30s) + 1 lan dong bo trong init. */
static uint8_t s_last_percent = 0;

uint8_t bmt_battery_last_percent(void)
{
	return s_last_percent;
}

int bmt_battery_read_mv(void)
{
	int16_t raw = 0;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};

	int err = adc_sequence_init_dt(&battery_adc, &sequence);
	if (err < 0) {
		LOG_ERR("adc_sequence_init_dt failed (%d)", err);
		return err;
	}

	err = adc_read_dt(&battery_adc, &sequence);
	if (err < 0) {
		LOG_ERR("adc_read_dt failed (%d)", err);
		return err;
	}

	int32_t adc_mv = raw;
	err = adc_raw_to_millivolts_dt(&battery_adc, &adc_mv);
	if (err < 0) {
		LOG_ERR("adc_raw_to_millivolts_dt failed (%d)", err);
		return err;
	}

	/* Nhan lai he so cua VDDHDIV5 de ra dien ap pin that tai VDDH */
	return adc_mv * BMT_BATT_VDDH_DIVIDER;
}

uint8_t bmt_battery_percent(int mv)
{
	if (mv >= BATT_CURVE[0].mv)
		return 100;
	if (mv <= BATT_CURVE[BATT_CURVE_N - 1].mv)
		return 0;

	for (size_t i = 0; i < BATT_CURVE_N - 1; i++) {
		uint16_t hi = BATT_CURVE[i].mv;
		uint16_t lo = BATT_CURVE[i + 1].mv;
		if (mv <= hi && mv >= lo) {
			/* Noi suy tuyen tinh giua 2 diem lan can */
			int pct_hi = BATT_CURVE[i].pct;
			int pct_lo = BATT_CURVE[i + 1].pct;
			return (uint8_t)(pct_lo + ((mv - lo) * (pct_hi - pct_lo)) / (hi - lo));
		}
	}
	return 0;
}

bool bmt_battery_is_charging(void)
{
	/* Board nay KHONG dua chan bao sac (~CHG cua IC sac) ra GPIO nao doc
	 * duoc — khac XIAO (co P0.17). Tra false de giu API chung voi ban XIAO.
	 *
	 * Neu sau nay can biet co dang sac hay khong: co the suy gian tiep tu
	 * chinh VDDH — khi cam USB, VDDH bi keo len ~5V, cao hon han muc pin
	 * day 4.2V. Chua lam vi chua co board that de xac minh nguong. */
	return false;
}

static void batt_log_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	int mv = bmt_battery_read_mv();
	if (mv < 0) {
		LOG_ERR("Battery read failed (%d)", mv);
		return;
	}
	s_last_percent = bmt_battery_percent(mv);
	LOG_INF("[BATTERY] VDDH=%d mV (~%u%%)", mv, s_last_percent);
}

static void batt_log_timer_handler(struct k_timer* timer)
{
	/* k_timer callback chay ISR context, adc_read block -> day sang workqueue */
	ARG_UNUSED(timer);
	k_work_submit(&batt_log_work);
}

int bmt_battery_init(void)
{
	if (!adc_is_ready_dt(&battery_adc)) {
		LOG_ERR("ADC device %s not ready", battery_adc.dev->name);
		return -ENODEV;
	}

	int err = adc_channel_setup_dt(&battery_adc);
	if (err < 0) {
		LOG_ERR("adc_channel_setup_dt failed (%d)", err);
		return err;
	}

	LOG_INF("Battery ADC (VDDHDIV5 noi bo) ready");

	/* Doc dong bo 1 lan de s_last_percent co gia tri hop le TRUOC khi beacon
	 * bat dau phat (beacon build_adv_data doc bmt_battery_last_percent). */
	int mv0 = bmt_battery_read_mv();
	if (mv0 >= 0)
		s_last_percent = bmt_battery_percent(mv0);

	k_work_init(&batt_log_work, batt_log_work_handler);
	k_timer_init(&batt_log_timer, batt_log_timer_handler, NULL);
	k_timer_start(&batt_log_timer, K_SECONDS(BMT_BATT_LOG_INTERVAL_SEC),
	              K_SECONDS(BMT_BATT_LOG_INTERVAL_SEC));

	/* Doc + in ngay 1 lan luc khoi dong */
	k_work_submit(&batt_log_work);

	return 0;
}
