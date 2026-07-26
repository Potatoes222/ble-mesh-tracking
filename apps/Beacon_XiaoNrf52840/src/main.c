#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>

#include "bmt_auth.h"
#include "bmt_beacon.h"
#include "bmt_battery.h"

LOG_MODULE_REGISTER(bmt_main, LOG_LEVEL_INF);

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
