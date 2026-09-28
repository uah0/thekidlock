#pragma once
unsigned ble_phone_stack_free(void);

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define BLE_PHONE_MAX_BONDS 20
#define BLE_PHONE_NAME_MAX_LEN 48

typedef struct {
    char address[18];
    uint8_t address_type;
    char name[BLE_PHONE_NAME_MAX_LEN + 1];
} ble_phone_bond_t;

typedef struct {
    bool connected;
    bool encrypted;
    bool bonded;
    bool rssi_valid;
    int8_t rssi;
} ble_phone_status_t;

esp_err_t ble_phone_init(void);
esp_err_t ble_phone_apply_tx_power(void);
void ble_phone_get_status(ble_phone_status_t *out);
void ble_phone_allow_registration(void);
void ble_phone_set_registration_mode(bool enabled);
void ble_phone_get_registration(bool *allowed, uint32_t *remaining_seconds);
size_t ble_phone_get_bonds(ble_phone_bond_t *out, size_t max_bonds);
esp_err_t ble_phone_delete_bond(const char *address);
esp_err_t ble_phone_delete_all_bonds(void);
