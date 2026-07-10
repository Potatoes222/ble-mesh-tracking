#pragma once

#include <stdbool.h>
void bmt_ota_trigger(void);

/* true khi đang trong quá trình OTA — bmt_scan.c dùng để tạm dừng GAP scan
 * (nhường toàn bộ radio cho WiFi), bmt_uart.c dùng hiển thị status */
bool bmt_ota_is_triggered(void);

/* Gọi 1 lần lúc boot (sau khi mesh sẵn sàng) — nếu NVS có cờ "vừa OTA xong
 * và reboot thành công", tự gửi báo cáo THÀNH CÔNG về Gateway */
void bmt_ota_start_pending_report_task(void);

/* [v6.0-auto-ota] Tự động định kỳ kiểm tra version trên server, tự OTA nếu
 * khác — không cần Gateway/UART trigger. Gọi 1 lần lúc boot. */
void bmt_ota_start_auto_check(void);
