#pragma once

/* Khoi tao task theo doi nut BOOT (GPIO0) o nen — giu lien tuc du 10 giay se
 * tu dong FACTORY RESET (xoa toan bo NVS: node table + NetKey/AppKey + moi
 * config khac, KHONG dong toi firmware) roi tu reboot.
 * Goi 1 lan luc boot, cang som cang tot. */
void bmt_factory_reset_init(void);
