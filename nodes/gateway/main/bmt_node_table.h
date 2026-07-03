#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     used;
    bool     is_relay;
    bool     is_scan;
    bool     config_done;
    bool     online;
    uint32_t last_seen_ms;
    uint16_t addr;
    uint8_t  uuid[16];
    uint8_t  mac[6];
    char     name[32];
} bmt_node_t;

void bmt_node_table_load(void);
void bmt_node_table_save(void);
void bmt_node_table_clear(void);

int  bmt_node_table_find(uint16_t addr);
bool bmt_node_table_uuid_provisioned(const uint8_t *uuid);
int  bmt_node_table_add(uint16_t addr, const uint8_t *uuid, const uint8_t *mac, const char *name);
bmt_node_t *bmt_node_table_get(int idx);
int         bmt_node_table_capacity(void);
void        bmt_node_table_print(void);

bool        bmt_uuid_is_relay(const uint8_t *uuid);
bool        bmt_uuid_is_scan(const uint8_t *uuid);
const char *bmt_uuid_type_str(const uint8_t *uuid);

void bmt_print_uuid(const uint8_t *uuid);
void bmt_print_mac(const uint8_t *mac);
void bmt_print_hex_key(const char *label, const uint8_t *key, int len);
