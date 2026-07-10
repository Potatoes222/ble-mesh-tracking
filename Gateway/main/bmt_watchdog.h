#pragma once

/* [v4.6] Bảo vệ khi rút board relay (hoặc mesh đứt vì lý do khác): nếu sau
 * BMT_WDG_TIMEOUT_MS không nhận thêm TAG_STATUS nào (mesh_received không
 * tăng), gửi RESET_CMD broadcast yêu cầu TẤT CẢ node trong mesh tự xóa NVS +
 * reboot, sau đó Gateway cũng tự xóa toàn bộ node table + mesh provisioner
 * NVS của chính nó rồi reboot — cả hệ thống về trạng thái auto-provision từ
 * đầu, tránh việc 1 relay rớt mạng vĩnh viễn làm nghẽn cả mesh. */
void bmt_watchdog_start(void);
