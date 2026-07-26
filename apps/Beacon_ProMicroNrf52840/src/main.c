#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>

#include "bmt_auth.h"
#include "bmt_beacon.h"
#include "bmt_battery.h"

LOG_MODULE_REGISTER(bmt_main, LOG_LEVEL_INF);

/* [POWER] Cong tac mem cat nguon ra chan VCC ngoai (P0.13) — xem giai thich
 * day du trong boards/promicro_nrf52840_nrf52840_uf2.overlay.
 * Tom tat: nhanh VCC 3.3V dua ra ngoai mac dinh LUON CO DIEN (Zephyr khong
 * khai bao chan dieu khien nen no o trang thai reset), ton ~1mA du app khong
 * cap nguon cho thiet bi ngoai nao. */
static const struct gpio_dt_spec ext_vcc =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ext_vcc_gpios);

static void ext_vcc_off(void)
{
	if (!gpio_is_ready_dt(&ext_vcc)) {
		LOG_ERR("ext-vcc GPIO chua san sang");
		return;
	}

	/* INACTIVE + active-high = drive LOW = tat MOSFET.
	 * An toan cho MCU: MOSFET nay chi nam tren duong ra chan VCC ngoai,
	 * ban than nRF52840 an nguon rieng qua VDDH. */
	int err = gpio_pin_configure_dt(&ext_vcc, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Tat VCC ngoai that bai (err %d)", err);
		return;
	}
	LOG_INF("Da tat nguon ra chan VCC ngoai (P0.13)");
}

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	LOG_INF("Bluetooth initialized");

	/* PHAI goi truoc bmt_beacon_start() — beacon can HMAC key da san sang
	 * de tinh mac16 cho goi ADV dau tien. */
	bmt_auth_init();

	err = bmt_beacon_start();
	if (err) {
		LOG_ERR("bmt_beacon_start failed (err %d)", err);
		return;
	}
	LOG_INF("BMT Tag beacon started");
}

int main(void)
{
	printk("=== BMT Tag (nRF52840) starting ===\n");

	/* Lam SOM NHAT co the — moi ms tre la ngan do dien chay phi tren nhanh VCC */
	ext_vcc_off();

	/* Doc pin khong lien quan BLE, khoi dong doc lap voi bt_enable */
	int batt_err = bmt_battery_init();
	if (batt_err) {
		LOG_ERR("bmt_battery_init failed (err %d)", batt_err);
	}

	int err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("bt_enable failed (err %d)", err);
	}
	return 0;
}
