#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
	BMT_PROV_MODE_AUTO = 0,
	BMT_PROV_MODE_MANUAL = 1
} bmt_prov_mode_t;

bmt_prov_mode_t bmt_scan_list_get_mode(void);
bool bmt_scan_list_is_scanning(void);
bool bmt_scan_list_add(const uint8_t* uuid, const uint8_t* mac,
                       uint8_t addr_type, uint16_t oob_info);

/* UART 's' — chuyển MANUAL mode, quét BMT_SCAN_DURATION_MS (block caller), in kết quả */
void bmt_scan_list_start(void);
/* UART 'p' — provision toàn bộ thiết bị trong danh sách chưa được provision */
void bmt_scan_list_provision(void);
void bmt_scan_list_print(void);

/* UART 'a'/'m' — đổi chế độ auto/manual (KHÔNG tự động quét/xóa danh sách khi
 * chuyển sang manual, chỉ 's' mới quét) */
void bmt_scan_list_set_mode(bmt_prov_mode_t mode);
