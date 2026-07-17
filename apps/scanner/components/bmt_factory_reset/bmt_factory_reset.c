#include "bmt_factory_reset.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "BMT_FACTORY_RST";

/* Nut BOOT (GPIO0) — co san tren hau het board dev ESP32/ESP32-S3, chi co
 * tac dung dac biet luc power-on (vao che do nap firmware), con khi app da
 * chay binh thuong thi doc duoc nhu 1 GPIO input thuong. KHONG dung nut
 * EN/RST cung duoc vi giu no la chip dang o trang thai reset, khong co
 * code nao chay de dem thoi gian ca. */
#define BMT_FACTORY_RESET_GPIO GPIO_NUM_0
#define BMT_FACTORY_RESET_POLL_MS 100
#define BMT_FACTORY_RESET_HOLD_MS 10000
#define BMT_FACTORY_RESET_TICKS_NEEDED (BMT_FACTORY_RESET_HOLD_MS / BMT_FACTORY_RESET_POLL_MS)

static void do_factory_reset(void)
{
	ESP_LOGW(TAG, "==================================================");
	ESP_LOGW(TAG, "[FACTORY RESET] Giu du 10s -> xoa toan bo NVS...");
	ESP_LOGW(TAG, "==================================================");
	esp_err_t err = nvs_flash_erase();
	if (err != ESP_OK)
		ESP_LOGE(TAG, "nvs_flash_erase that bai: %s", esp_err_to_name(err));
	ESP_LOGW(TAG, "[FACTORY RESET] Xong, dang reboot...");
	vTaskDelay(pdMS_TO_TICKS(300));
	esp_restart();
}

static void factory_reset_task(void* arg)
{
	(void)arg;
	int held_ticks = 0;

	while (1)
	{
		/* Nut BOOT keo GPIO0 xuong GND khi bi giu (active-low) */
		if (gpio_get_level(BMT_FACTORY_RESET_GPIO) == 0)
		{
			held_ticks++;
			if (held_ticks == 1)
			{
				ESP_LOGI(TAG, "[FACTORY RESET] Dang giu nut BOOT...");
			}
			else if (held_ticks % (1000 / BMT_FACTORY_RESET_POLL_MS) == 0)
			{
				int remain_s = (BMT_FACTORY_RESET_TICKS_NEEDED - held_ticks) * BMT_FACTORY_RESET_POLL_MS / 1000;
				ESP_LOGW(TAG, "[FACTORY RESET] Con %ds se xoa toan bo NVS...", remain_s);
			}

			if (held_ticks >= BMT_FACTORY_RESET_TICKS_NEEDED)
			{
				do_factory_reset();
				/* khong bao gio toi day, esp_restart() da reboot */
			}
		}
		else
		{
			/* Tha nut -> huy dem ngay lap tuc — vua la logic "phai giu LIEN
			 * TUC du 10s", vua tu nhien chong nhieu (1 lan doc nhieu thoang
			 * qua se khong lam mat dem that su vi con phai giu tiep). */
			held_ticks = 0;
		}

		vTaskDelay(pdMS_TO_TICKS(BMT_FACTORY_RESET_POLL_MS));
	}
}

void bmt_factory_reset_init(void)
{
	gpio_config_t io_conf = {
	    .pin_bit_mask = (1ULL << BMT_FACTORY_RESET_GPIO),
	    .mode = GPIO_MODE_INPUT,
	    .pull_up_en = GPIO_PULLUP_ENABLE,
	    .pull_down_en = GPIO_PULLDOWN_DISABLE,
	    .intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&io_conf);

	xTaskCreate(factory_reset_task, "bmt_factory_rst", 2560, NULL, 3, NULL);
	ESP_LOGI(TAG, "Factory reset watcher started (giu nut BOOT 10s de kich hoat)");
}
