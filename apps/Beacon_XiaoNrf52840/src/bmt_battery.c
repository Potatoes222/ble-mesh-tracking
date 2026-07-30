#include "bmt_battery.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bmt_battery, LOG_LEVEL_INF);

/* [POWER] Doc pin cho duong BAT+/BAT- (LIR2032 3.6V hoac LiPo).
 *
 * Vi sao KHONG do VDD noi: khi pin di qua BAT+ -> bo dieu ap on-board,
 * VDD bi ghim ~3.3V bat ke pin dang 4.2V hay 3.4V -> do VDD vo nghia.
 * Phai doc dien ap tai node BAT+ qua bo chia ap on-board 1M/510k dua
 * ra chan P0.31 (AIN7). Cong thuc + tri so dien tro tham khao thu vien
 * cong dong Tjoms99/xiao_sense_nrf52840_battery_lib (da verify tren HW).
 *
 * (Neu sau nay quay lai CR2032 cap thang 3V3 thi doi lai thanh do
 * NRF_SAADC_VDD trong overlay + bo phan chia ap o day — 2 duong nguon
 * khac nhau, 2 cach do khac nhau.) */
static const struct adc_dt_spec battery_adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

/* Bo chia ap on-board: BAT+ --[R1]-- P0.31 --[R2]-- (P0.14 keo GND).
 * V_bat = V_adc * (R1 + R2) / R2. Tri so danh dinh 1M/510k; R1 lay 1037k
 * theo hieu chinh thuc te cua thu vien tham khao (sai so dien tro). Neu
 * muon chinh xac hon nua: do V_bat that bang VOM roi tinh lai R1. */
#define BMT_BATT_R1_KOHM 1037
#define BMT_BATT_R2_KOHM 510

/* P0.14 = VBAT_ENABLE (active-low): keo LOW de dong bo chia ap vao mach
 * do. P0.17 = charge status (LOW khi dang sac). */
static const struct device* gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
#define BMT_BATT_ENABLE_PIN 14
#define BMT_BATT_CHARGE_STAT_PIN 17

/* Bang tra dien ap -> % cho Li-ion (LIR2032/LiPo). Duong cong SoC-vs-ap
 * cua Li-ion PHI TUYEN manh, khong the dung 1 cong thuc tuyen tinh -> tra
 * bang + noi suy giua 2 diem. Moc 4200mV=100% (day), 3300mV=0% (nguong an
 * toan toi thieu, duoi nua hong pin). Tham khao Tjoms99 lib + Nordic SoC
 * blog. */
typedef struct
{
	uint16_t mv;
	uint8_t pct;
} batt_point_t;

static const batt_point_t BATT_CURVE[] = {
    {4200, 100},
    {4110, 90},
    {4020, 80},
    {3930, 70},
    {3840, 60},
    {3750, 50},
    {3660, 40},
    {3570, 30},
    {3480, 20},
    {3390, 10},
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
	if (err < 0)
	{
		LOG_ERR("adc_sequence_init_dt failed (%d)", err);
		return err;
	}

	err = adc_read_dt(&battery_adc, &sequence);
	if (err < 0)
	{
		LOG_ERR("adc_read_dt failed (%d)", err);
		return err;
	}

	int32_t adc_mv = raw;
	err = adc_raw_to_millivolts_dt(&battery_adc, &adc_mv);
	if (err < 0)
	{
		LOG_ERR("adc_raw_to_millivolts_dt failed (%d)", err);
		return err;
	}

	/* Nhan nguoc lai ti so chia ap de ra dien ap pin that tai BAT+ */
	int32_t vbat = (adc_mv * (BMT_BATT_R1_KOHM + BMT_BATT_R2_KOHM)) / BMT_BATT_R2_KOHM;
	return vbat;
}

uint8_t bmt_battery_percent(int mv)
{
	if (mv >= BATT_CURVE[0].mv)
		return 100;
	if (mv <= BATT_CURVE[BATT_CURVE_N - 1].mv)
		return 0;

	for (size_t i = 0; i < BATT_CURVE_N - 1; i++)
	{
		uint16_t hi = BATT_CURVE[i].mv;
		uint16_t lo = BATT_CURVE[i + 1].mv;
		if (mv <= hi && mv >= lo)
		{
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
	/* P0.17 LOW = dang sac (chip sac keo xuong) */
	return gpio_pin_get(gpio0, BMT_BATT_CHARGE_STAT_PIN) == 0;
}

static void batt_log_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	int mv = bmt_battery_read_mv();
	if (mv < 0)
	{
		LOG_ERR("Battery read failed (%d)", mv);
		return;
	}
	s_last_percent = bmt_battery_percent(mv);
	LOG_INF("[BATTERY] BAT+=%d mV (~%u%%) %s", mv, s_last_percent,
	        bmt_battery_is_charging() ? "[CHARGING]" : "");
}

static void batt_log_timer_handler(struct k_timer* timer)
{
	/* k_timer callback chay ISR context, adc_read block -> day sang workqueue */
	ARG_UNUSED(timer);
	k_work_submit(&batt_log_work);
}

int bmt_battery_init(void)
{
	if (!device_is_ready(gpio0))
	{
		LOG_ERR("gpio0 not ready");
		return -ENODEV;
	}

	/* P0.14 VBAT_ENABLE active-low: OUTPUT_ACTIVE -> keo LOW -> dong bo
	 * chia ap vao mach do. Giu LOW ca doi (xem ghi chu overlay) — KHONG
	 * duoc de HIGH (du chi mot phan thoi gian), Seeed canh bao chinh thuc
	 * P0.31 co the vot qua gioi han dien ap neu P0.14 o muc HIGH. */
	int err = gpio_pin_configure(gpio0, BMT_BATT_ENABLE_PIN,
	                             GPIO_OUTPUT_ACTIVE | GPIO_ACTIVE_LOW);
	if (err < 0)
	{
		LOG_ERR("config VBAT_ENABLE (P0.14) failed (%d)", err);
		return err;
	}

	/* P0.17 charge-status: INPUT de doc trang thai sac */
	err = gpio_pin_configure(gpio0, BMT_BATT_CHARGE_STAT_PIN, GPIO_INPUT);
	if (err < 0)
	{
		LOG_ERR("config CHARGE_STAT (P0.17) failed (%d)", err);
		return err;
	}

	if (!adc_is_ready_dt(&battery_adc))
	{
		LOG_ERR("ADC device %s not ready", battery_adc.dev->name);
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&battery_adc);
	if (err < 0)
	{
		LOG_ERR("adc_channel_setup_dt failed (%d)", err);
		return err;
	}

	LOG_INF("Battery ADC (BAT+ on-board divider) ready");

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
