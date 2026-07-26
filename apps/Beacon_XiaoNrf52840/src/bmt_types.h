#pragma once

#include <stdint.h>

/* PHAI GIONG HET struct ben ESP32 (apps/tag/components/bmt_beacon/bmt_beacon.h)
 * — day la giao dien duy nhat giua Tag va he thong (Scanner/Mesh/Gateway).
 * Sai 1 byte la Scanner tu choi nhan dien tag. */
#pragma pack(1)
typedef struct
{
	uint8_t uuid[16];    /* 16B: system UUID (AB000000-...) */
	uint16_t battery;    /*  2B: % pin con lai (0-100). Truoc day la "major"
	                      *      (PERSON/ASSET) — da bo vi tag deo khong can
	                      *      phan loai, dung 2B nay cho battery. Giu nguyen
	                      *      vi tri/kich thuoc nen HMAC + layout 24B khong doi. */
	uint16_t minor;      /*  2B: tag ID trong he thong */
	int8_t tx_power;     /*  1B: measured power tai 1m (calibrate) */
	uint8_t sequence;    /*  1B: 0-255, wraps -> Scanner tinh loss rate */
	uint16_t mac16;      /*  2B: HMAC-SHA256 rut gon (thay CRC16) */
} bmt_tag_adv_payload_t; /* = 24 bytes */
#pragma pack()
