
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bmt_config.h"
#include "bmt_auth.h"
#include "bmt_beacon.h"
#include "bmt_uart.h"

static const char* TAG = "BMT_MAIN";

void app_main(void)
{
	ESP_LOGI(TAG, "=== BLE Tag v3.0-modular Starting ===");
	ESP_LOGI(TAG, "Major=0x%04X (%s)  Minor=0x%04X  TXPwrRef=%ddBm",
	         BMT_TAG_MAJOR,
	         BMT_TAG_MAJOR == 0x0001 ? "PERSON" : "ASSET",
	         BMT_TAG_MINOR, BMT_TAG_TX_POWER);

	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
	    err == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	/* [SECURITY] Import HMAC key vào PSA Crypto keystore 1 lần lúc boot,
	 * PHẢI gọi trước bmt_beacon_start() (build_adv_data cần key sẵn sàng) */
	bmt_auth_init();

	ESP_ERROR_CHECK(bmt_beacon_start());
	ESP_ERROR_CHECK(bmt_uart_init());

	ESP_LOGI(TAG, "=== Tag READY (1=status) ===");
}
