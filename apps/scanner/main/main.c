
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bmt_factory_reset.h"
#include "bmt_scan_core.h"

static const char* TAG = "BMT_MAIN";

void app_main(void)
{
	ESP_LOGI(TAG, "=== BMT Scan Node v3.0 Ready ===");

	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	bmt_factory_reset_init();

	ESP_ERROR_CHECK(bmt_scan_core_init());
}
