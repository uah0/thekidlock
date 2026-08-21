#include "ble_phone.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ap_mode.h"
#include "app_config.h"
#include "event_log.h"
#include "led.h"
#include "lock_control.h"
#include "wifi_ota.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/hci_common.h"
#include "esp_bt.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define BLE_PHONE_NAME "BLE Lock"
#define REGISTRATION_WINDOW_US (60LL * 1000000LL)
#define MAX_BONDED_PHONES 20
#define BLE_RSSI_DISCONNECT_GRACE_MS 6000
#define BOND_NAMES_NVS_NS "ble_names"
#define OWNER_NVS_NS "ble_owner"
#define OWNER_NVS_KEY "identity"

void ble_store_config_init(void);

enum {
    CHR_LOCK_INFO = 1,
    CHR_LOCK_COMMAND,
    CHR_LOCK_STATUS,
    CHR_ADMIN_CONFIG,
    CHR_ADMIN_STATE,
    CHR_ADMIN_BONDS,
    CHR_ADMIN_CONTROL,
    CHR_ADMIN_EVENTS,
    CHR_ADMIN_SCHEMA,
    CHR_OTA_CONFIG,
    CHR_OTA_STATUS,
    CHR_PHONE_NAME,
};

/* 6f4f0001-9a7b-4c21-8d32-5e6f708192a3 */
static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x01, 0x00, 0x4f, 0x6f);
/* 6f4f0002-9a7b-4c21-8d32-5e6f708192a3 */
static const ble_uuid128_t s_info_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x02, 0x00, 0x4f, 0x6f);
/* 6f4f0003-9a7b-4c21-8d32-5e6f708192a3 */
static const ble_uuid128_t s_command_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x03, 0x00, 0x4f, 0x6f);
/* 6f4f0004-9a7b-4c21-8d32-5e6f708192a3 */
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x04, 0x00, 0x4f, 0x6f);

static uint8_t s_addr_type;
static portMUX_TYPE s_registration_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_registration_deadline_us;
static portMUX_TYPE s_connection_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static int8_t s_last_rssi;
static bool s_rssi_valid;
static bool s_encrypted;
static bool s_bonded;
static bool s_advertising_restricted;

#define ADMIN_SCHEMA_MAX_BYTES 2048
#define ADMIN_SCHEMA_CHUNK_BYTES 240
#define ADMIN_SCHEMA_CHUNK_B64_MAX ((ADMIN_SCHEMA_CHUNK_BYTES + 2) / 3 * 4 + 1)
static uint16_t s_admin_schema_chunk_index;
static bool s_peer_was_bonded;
static bool s_reboot_pending;

static esp_err_t ble_phone_start_advertising(void);

esp_err_t ble_phone_apply_tx_power(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    esp_power_level_t level = (esp_power_level_t)
        ((cfg.ble_tx_power_dbm - APP_BLE_TX_POWER_MIN_DBM) / 3);
    esp_err_t adv_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, level);
    esp_err_t default_err = ESP_OK;
    if (adv_err == ESP_OK) {
        default_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, level);
    }
    if (adv_err != ESP_OK) {
        /* While an administrative phone is connected, advertising is
         * stopped, and the controller can reject changing ADV power. The
         * value is already persisted and will be applied when advertising
         * starts again. This is not a failed save operation. */
        event_log_add("BLE: мощность рекламы %d dBm отложена до запуска рекламы: %s",
                      cfg.ble_tx_power_dbm, esp_err_to_name(adv_err));
        return ESP_OK;
    }
    if (default_err != ESP_OK) {
        /* Advertising power is the user-facing setting. Some controller
         * states may reject the independent default level while advertising
         * power has already been applied successfully. */
        event_log_add("BLE: мощность рекламы установлена: %d dBm; общий уровень не изменён: %s",
                      cfg.ble_tx_power_dbm, esp_err_to_name(default_err));
    } else {
        event_log_add("BLE: мощность передатчика установлена: %d dBm",
                      cfg.ble_tx_power_dbm);
    }
    return ESP_OK;
}

static int ble_phone_get_bonded_peers(ble_addr_t *peers, int max_peers)
{
    int count = 0;
    if (!peers || max_peers <= 0) return -1;
    if (ble_store_util_bonded_peers(peers, &count, max_peers) != 0) return -1;
    return count;
}

static esp_err_t ble_phone_refresh_accept_list(bool restricted, int *bond_count)
{
    ble_addr_t peers[MAX_BONDED_PHONES];
    int count = ble_phone_get_bonded_peers(peers, MAX_BONDED_PHONES);
    if (count < 0) return ESP_FAIL;
    if (bond_count) *bond_count = count;

    if (restricted && count > CONFIG_BT_NIMBLE_WHITELIST_SIZE) {
        event_log_add("BLE: bonds=%d превышают размер списка допуска=%d",
                      count, CONFIG_BT_NIMBLE_WHITELIST_SIZE);
        return ESP_ERR_NOT_SUPPORTED;
    }
    int rc = ble_gap_wl_set(peers, restricted ? count : 0);
    if (rc != 0) {
        event_log_add("BLE: не удалось обновить список допуска: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void disconnect_after_bond_task(void *arg)
{
    uint16_t conn_handle = (uint16_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));

    portENTER_CRITICAL(&s_connection_lock);
    bool still_connected = s_conn_handle == conn_handle;
    portEXIT_CRITICAL(&s_connection_lock);
    if (still_connected) {
        event_log_add("BLE registration: новый bond сохранён, освободить соединение");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    vTaskDelete(NULL);
}

void ble_phone_get_status(ble_phone_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_connection_lock);
    out->connected = s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
    out->encrypted = s_encrypted;
    out->bonded = s_bonded;
    out->rssi_valid = s_rssi_valid;
    out->rssi = s_last_rssi;
    portEXIT_CRITICAL(&s_connection_lock);
}

static void address_to_string(const ble_addr_t *addr, char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

static void bond_name_key(const ble_addr_t *addr, char out[15])
{
    static const char hex[] = "0123456789ABCDEF";
    out[0] = 'n';
    out[1] = hex[addr->type & 0x0f];
    for (size_t i = 0; i < sizeof(addr->val); i++) {
        out[2 + i * 2] = hex[addr->val[i] >> 4];
        out[3 + i * 2] = hex[addr->val[i] & 0x0f];
    }
    out[14] = '\0';
}

static esp_err_t owner_load(ble_addr_t *owner)
{
    if (!owner) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OWNER_NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint8_t stored[7];
    size_t length = sizeof(stored);
    err = nvs_get_blob(handle, OWNER_NVS_KEY, stored, &length);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    if (length != sizeof(stored)) return ESP_ERR_INVALID_SIZE;
    owner->type = stored[0];
    memcpy(owner->val, &stored[1], sizeof(owner->val));
    return ESP_OK;
}

static esp_err_t owner_save(const ble_addr_t *owner)
{
    if (!owner) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OWNER_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint8_t stored[7] = {owner->type};
    memcpy(&stored[1], owner->val, sizeof(owner->val));
    err = nvs_set_blob(handle, OWNER_NVS_KEY, stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t owner_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OWNER_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, OWNER_NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static bool address_equal(const ble_addr_t *left, const ble_addr_t *right)
{
    return left && right && left->type == right->type &&
           memcmp(left->val, right->val, sizeof(left->val)) == 0;
}

static bool bond_name_load(const ble_addr_t *addr,
                           char out[BLE_PHONE_NAME_MAX_LEN + 1])
{
    nvs_handle_t handle;
    if (nvs_open(BOND_NAMES_NVS_NS, NVS_READONLY, &handle) != ESP_OK) return false;
    char key[15];
    bond_name_key(addr, key);
    size_t length = BLE_PHONE_NAME_MAX_LEN + 1;
    esp_err_t err = nvs_get_str(handle, key, out, &length);
    nvs_close(handle);
    return err == ESP_OK && out[0] != '\0';
}

static esp_err_t bond_name_save_once(const ble_addr_t *addr, const char *name)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BOND_NAMES_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    char key[15];
    bond_name_key(addr, key);
    size_t existing_length = 0;
    err = nvs_get_str(handle, key, NULL, &existing_length);
    if (err == ESP_OK) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    err = nvs_set_str(handle, key, name);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void bond_name_delete(const ble_addr_t *addr)
{
    nvs_handle_t handle;
    if (nvs_open(BOND_NAMES_NVS_NS, NVS_READWRITE, &handle) != ESP_OK) return;
    char key[15];
    bond_name_key(addr, key);
    esp_err_t err = nvs_erase_key(handle, key);
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

size_t ble_phone_get_bonds(ble_phone_bond_t *out, size_t max_bonds)
{
    if (!out || max_bonds == 0) return 0;
    ble_addr_t peers[MAX_BONDED_PHONES];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, MAX_BONDED_PHONES) != 0) return 0;
    size_t n = (size_t)count < max_bonds ? (size_t)count : max_bonds;
    for (size_t i = 0; i < n; i++) {
        address_to_string(&peers[i], out[i].address);
        out[i].address_type = peers[i].type;
        if (!bond_name_load(&peers[i], out[i].name)) {
            snprintf(out[i].name, sizeof(out[i].name), "имя не сохранено");
        }
    }
    return n;
}

esp_err_t ble_phone_delete_bond(const char *address)
{
    if (!address) return ESP_ERR_INVALID_ARG;
    ble_addr_t peers[MAX_BONDED_PHONES];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, MAX_BONDED_PHONES) != 0) return ESP_FAIL;
    for (int i = 0; i < count; i++) {
        char current[18];
        address_to_string(&peers[i], current);
        if (strcmp(current, address) == 0) {
            int rc = ble_store_util_delete_peer(&peers[i]);
            if (rc == 0) {
                bond_name_delete(&peers[i]);
                ble_addr_t owner;
                if (owner_load(&owner) == ESP_OK && address_equal(&owner, &peers[i])) {
                    if (owner_clear() != ESP_OK) return ESP_FAIL;
                    event_log_add("BLE owner удалён вместе с bond");
                }
                event_log_add("BLE bond %s удалён", address);
            }
            return rc == 0 ? ESP_OK : ESP_FAIL;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ble_phone_delete_all_bonds(void)
{
    if (!ap_mode_is_configuration()) {
        event_log_add("Удаление всех bonds отклонено: режим настройки выключен");
        return ESP_ERR_INVALID_STATE;
    }
    ble_addr_t peers[MAX_BONDED_PHONES];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, MAX_BONDED_PHONES) != 0) return ESP_FAIL;
    for (int i = 0; i < count; i++) {
        if (ble_store_util_delete_peer(&peers[i]) != 0) return ESP_FAIL;
        bond_name_delete(&peers[i]);
    }
    if (owner_clear() != ESP_OK) return ESP_FAIL;
    event_log_add("Все BLE bonds удалены: %d", count);
    if (led_flash_bond_reset() != ESP_OK) {
        event_log_add("LED: не удалось запустить индикацию сброса bonds");
    }
    return ESP_OK;
}

void ble_phone_allow_registration(void)
{
    portENTER_CRITICAL(&s_registration_lock);
    s_registration_deadline_us = esp_timer_get_time() + REGISTRATION_WINDOW_US;
    portEXIT_CRITICAL(&s_registration_lock);
    event_log_add("BLE registration разрешена на 60 секунд");

    // Only one BLE connection is allowed. Release an existing phone so that a
    // new handset can actually reach the Pairing/Bonding flow during the window.
    uint16_t conn_handle;
    portENTER_CRITICAL(&s_connection_lock);
    conn_handle = s_conn_handle;
    portEXIT_CRITICAL(&s_connection_lock);
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        event_log_add("BLE registration: текущее соединение отключается");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    } else {
        ble_phone_start_advertising();
    }
}

void ble_phone_set_registration_mode(bool enabled)
{
    portENTER_CRITICAL(&s_registration_lock);
    s_registration_deadline_us = enabled ? INT64_MAX : 0;
    portEXIT_CRITICAL(&s_registration_lock);
    event_log_add("BLE registration режим настройки: %s", enabled ? "открыта" : "закрыта");
    if (enabled) {
        uint16_t conn_handle;
        portENTER_CRITICAL(&s_connection_lock);
        conn_handle = s_conn_handle;
        portEXIT_CRITICAL(&s_connection_lock);
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        } else {
            ble_phone_start_advertising();
        }
    } else {
        uint16_t conn_handle;
        portENTER_CRITICAL(&s_connection_lock);
        conn_handle = s_conn_handle;
        portEXIT_CRITICAL(&s_connection_lock);
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            event_log_add("BLE: выход из режима настройки, соединение отключается");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
}

void ble_phone_get_registration(bool *allowed, uint32_t *remaining_seconds)
{
    int64_t deadline;
    portENTER_CRITICAL(&s_registration_lock);
    deadline = s_registration_deadline_us;
    portEXIT_CRITICAL(&s_registration_lock);

    int64_t remaining_us = deadline - esp_timer_get_time();
    bool active = remaining_us > 0;
    if (allowed) *allowed = active;
    if (remaining_seconds) {
        *remaining_seconds = !active ? 0 :
            (deadline == INT64_MAX ? UINT32_MAX :
             (uint32_t)((remaining_us + 999999) / 1000000));
    }
}

static bool peer_has_bond(uint16_t conn_handle, int *bond_count)
{
    ble_addr_t peers[MAX_BONDED_PHONES];
    int count = 0;
    int rc = ble_store_util_bonded_peers(peers, &count, MAX_BONDED_PHONES);
    if (bond_count) *bond_count = rc == 0 ? count : 0;
    if (rc != 0) return false;

    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0) return false;
    for (int i = 0; i < count; i++) {
        if (peers[i].type == desc.peer_id_addr.type &&
            memcmp(peers[i].val, desc.peer_id_addr.val, sizeof(peers[i].val)) == 0) {
            return true;
        }
    }
    return false;
}

static int append_value(struct os_mbuf *om, const void *value, size_t len)
{
    return os_mbuf_append(om, value, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static bool connection_is_admin(uint16_t conn_handle, bool claim_if_unset)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
        !desc.sec_state.encrypted || !desc.sec_state.bonded) {
        return false;
    }

    ble_addr_t owner;
    esp_err_t err = owner_load(&owner);
    if (err == ESP_OK) return address_equal(&owner, &desc.peer_id_addr);
    if (err != ESP_ERR_NVS_NOT_FOUND || !claim_if_unset) return false;
    if (owner_save(&desc.peer_id_addr) != ESP_OK) {
        event_log_add("BLE owner: ошибка сохранения владельца");
        return false;
    }
    char address[18];
    address_to_string(&desc.peer_id_addr, address);
    event_log_add("BLE owner назначен при входе в меню: %s", address);
    return true;
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    intptr_t characteristic = (intptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && characteristic == CHR_LOCK_INFO) {
        static const char prefix[] = "BLE Lock ";
        int rc = append_value(ctxt->om, prefix, sizeof(prefix) - 1);
        return rc == 0 ? append_value(ctxt->om, APP_VERSION, strlen(APP_VERSION)) : rc;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && characteristic == CHR_LOCK_STATUS) {
        const char *status = lock_control_is_hold_open() ? "OPEN_HOLD" :
                             (lock_control_is_open() ? "OPEN" : "LOCKED");
        return append_value(ctxt->om, status, strlen(status));
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && characteristic == CHR_LOCK_COMMAND) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        char command[16] = {0};
        if (len >= sizeof(command) || ble_hs_mbuf_to_flat(ctxt->om, command,
                                                          sizeof(command) - 1, NULL) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (len == 10 && memcmp(command, "FORCE_OPEN", 10) == 0) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
                !desc.sec_state.encrypted || !desc.sec_state.bonded) {
                event_log_add("BLE FORCE_OPEN отклонён: соединение не encrypted/bonded");
                return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
            }
            if (lock_request_manual_open() != ESP_OK) {
                event_log_add("BLE FORCE_OPEN: очередь автомата недоступна");
                return BLE_ATT_ERR_UNLIKELY;
            }
            event_log_add("BLE FORCE_OPEN принят: открытие без RSSI-порога");
            return 0;
        }
        if (len == 9 && memcmp(command, "KEEP_OPEN", 9) == 0) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
                !desc.sec_state.encrypted || !desc.sec_state.bonded) {
                event_log_add("BLE KEEP_OPEN отклонён: соединение не encrypted/bonded");
                return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
            }
            if (lock_request_toggle_hold_open() != ESP_OK) {
                event_log_add("BLE KEEP_OPEN: очередь автомата недоступна");
                return BLE_ATT_ERR_UNLIKELY;
            }
            event_log_add("BLE KEEP_OPEN принят: переключение режима Не закрывать");
            return 0;
        }
        if (len == 4 && memcmp(command, "OPEN", 4) == 0) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
                !desc.sec_state.encrypted || !desc.sec_state.bonded) {
                event_log_add("BLE OPEN отклонён: соединение не encrypted/bonded");
                return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
            }
            int8_t rssi;
            bool rssi_valid;
            portENTER_CRITICAL(&s_connection_lock);
            rssi = s_last_rssi;
            rssi_valid = s_rssi_valid;
            portEXIT_CRITICAL(&s_connection_lock);
            app_config_t cfg;
            app_config_get(&cfg);
            bool sufficient = rssi_valid && rssi >= cfg.phone_rssi_threshold;
            esp_err_t err = sufficient ? lock_request_open() : lock_request_arm_open();
            if (err != ESP_OK) {
                event_log_add("BLE OPEN: очередь автомата недоступна");
                return BLE_ATT_ERR_UNLIKELY;
            }
            if (sufficient) {
                event_log_add("BLE OPEN принят: RSSI %d >= %d dBm",
                              rssi, cfg.phone_rssi_threshold);
            } else if (rssi_valid) {
                event_log_add("BLE OPEN вооружён: RSSI %d < %d dBm",
                              rssi, cfg.phone_rssi_threshold);
            } else {
                event_log_add("BLE OPEN вооружён: ожидание первого RSSI");
            }
            return 0;
        }
        event_log_add("BLE GATT: неизвестная команда");
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && characteristic == CHR_PHONE_NAME) {
        if (!connection_is_admin(conn_handle, false)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        char name[BLE_PHONE_NAME_MAX_LEN + 1] = {0};
        if (len == 0 || len > BLE_PHONE_NAME_MAX_LEN ||
            ble_hs_mbuf_to_flat(ctxt->om, name, BLE_PHONE_NAME_MAX_LEN, NULL) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        for (uint16_t i = 0; i < len; i++) {
            if (name[i] == '\n' || name[i] == '\r' || name[i] == ',') name[i] = ' ';
            if ((unsigned char)name[i] < 0x20) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) != 0) return BLE_ATT_ERR_UNLIKELY;
        esp_err_t err = bond_name_save_once(&desc.peer_id_addr, name);
        if (err != ESP_OK) return BLE_ATT_ERR_UNLIKELY;
        event_log_add("BLE phone name принято для bonded MAC");
        return 0;
    }

    if (characteristic == CHR_OTA_CONFIG) {
        if (!connection_is_admin(conn_handle, false)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            char value[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 4];
            esp_err_t err = wifi_ota_get_config(value, sizeof(value));
            return err == ESP_OK ? append_value(ctxt->om, value, strlen(value))
                                 : BLE_ATT_ERR_UNLIKELY;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            char value[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 4];
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 0 || len >= sizeof(value) ||
                ble_hs_mbuf_to_flat(ctxt->om, value, sizeof(value) - 1, NULL) != 0) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            value[len] = '\0';
            return wifi_ota_set_runtime_config(value, len) == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
    }

    if (characteristic == CHR_OTA_STATUS) {
        if (!connection_is_admin(conn_handle, false)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            char value[32];
            esp_err_t err = wifi_ota_get_status(value, sizeof(value));
            return err == ESP_OK ? append_value(ctxt->om, value, strlen(value))
                                 : BLE_ATT_ERR_UNLIKELY;
        }
    }

    if (characteristic >= CHR_ADMIN_CONFIG && characteristic <= CHR_ADMIN_EVENTS) {
        if (!connection_is_admin(conn_handle, false)) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
        if (characteristic == CHR_ADMIN_CONFIG && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            app_config_t cfg;
            char value[96];
            app_config_get(&cfg);
            int len = snprintf(value, sizeof(value), "%d,%d,%lu,%lu,%lu,%d,%d",
                               cfg.phone_rssi_threshold,
                               cfg.phone_rssi_hold_threshold,
                               (unsigned long)cfg.long_open_ms,
                               (unsigned long)cfg.timer_a_ms,
                               (unsigned long)cfg.timer_b_s,
                               cfg.lock_gpio, cfg.lock_open_level ? 1 : 0);
            return append_value(ctxt->om, value, (size_t)len);
        }
        if (characteristic == CHR_ADMIN_CONFIG && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            char value[96] = {0};
            int8_t rssi, rssi_hold;
            unsigned long long_open, timer_a, timer_b;
            int lock_gpio, open_level;
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 0 || len >= sizeof(value) ||
                ble_hs_mbuf_to_flat(ctxt->om, value, sizeof(value) - 1, NULL) != 0 ||
                sscanf(value, "%hhd,%hhd,%lu,%lu,%lu,%d,%d", &rssi, &rssi_hold,
                       &long_open, &timer_a, &timer_b, &lock_gpio, &open_level) != 7 ||
                open_level < 0 || open_level > 1) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            app_config_t current;
            app_config_get(&current);
            esp_err_t err = app_config_set_logic(rssi, rssi_hold,
                                                  current.ble_connect_rssi_threshold,
                                                  current.ble_disconnect_rssi_threshold,
                                                  (uint32_t)long_open,
                                                  (uint32_t)timer_a, (uint32_t)timer_b,
                                                  lock_gpio,
                                                  open_level != 0);
            if (err != ESP_OK) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            err = lock_control_set_gpio(lock_gpio);
            if (err == ESP_OK) err = lock_control_config_changed();
            if (err != ESP_OK) return BLE_ATT_ERR_UNLIKELY;
            event_log_add("BLE admin конфигурация сохранена");
            return 0;
        }

        if (characteristic == CHR_ADMIN_STATE && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            event_log_add("BLE admin: чтение состояния");
            ble_phone_status_t phone;
            bool registration;
            uint32_t remaining;
            ble_phone_bond_t bonds[BLE_PHONE_MAX_BONDS];
            char value[128];
            ble_phone_get_status(&phone);
            ble_phone_get_registration(&registration, &remaining);
            size_t bond_count = ble_phone_get_bonds(bonds, BLE_PHONE_MAX_BONDS);
            int len = snprintf(value, sizeof(value),
                               "lock=%s;rssi=%s%d;registration=%lu;bonds=%u;uptime=%llu",
                               lock_control_is_open() ? "OPEN" : "LOCKED",
                               phone.rssi_valid ? "" : "NA",
                               phone.rssi_valid ? phone.rssi : 0,
                               (unsigned long)(registration ? remaining : 0),
                               (unsigned)bond_count,
                               (unsigned long long)(esp_timer_get_time() / 1000000ULL));
            return append_value(ctxt->om, value, (size_t)len);
        }

        if (characteristic == CHR_ADMIN_BONDS && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            event_log_add("BLE admin: чтение списка bonds");
            ble_phone_bond_t bonds[BLE_PHONE_MAX_BONDS];
            char value[2048];
            size_t count = ble_phone_get_bonds(bonds, BLE_PHONE_MAX_BONDS);
            size_t pos = 0;
            for (size_t i = 0; i < count; i++) {
                int written = snprintf(value + pos, sizeof(value) - pos, "%s%u,%s,%s",
                                       i ? "\n" : "", bonds[i].address_type,
                                       bonds[i].address, bonds[i].name);
                if (written < 0 || (size_t)written >= sizeof(value) - pos) break;
                pos += (size_t)written;
            }
            return append_value(ctxt->om, value, pos);
        }

        if (characteristic == CHR_ADMIN_EVENTS && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            event_log_add("BLE admin: чтение журнала событий");
            char value[480];
            size_t len = event_log_to_text(value, sizeof(value));
            return append_value(ctxt->om, value, len);
        }

        if (characteristic == CHR_ADMIN_CONTROL && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            char command[64] = {0};
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 0 || len >= sizeof(command) ||
                ble_hs_mbuf_to_flat(ctxt->om, command, sizeof(command) - 1, NULL) != 0) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            if (strncmp(command, "SCHEMA:", 7) == 0) {
                char *end = NULL;
                unsigned long index = strtoul(command + 7, &end, 10);
                if (end == command + 7 || *end != '\0' || index > 31) {
                    return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
                }
                s_admin_schema_chunk_index = (uint16_t)index;
                return 0;
            }
            if (strcmp(command, "REGISTER") == 0) {
                if (!ap_mode_is_configuration()) {
                    event_log_add("BLE admin: REGISTER отклонён вне синего режима");
                    return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
                }
                event_log_add("BLE admin: команда REGISTER");
                ble_phone_allow_registration();
                return 0;
            }
            if (strcmp(command, "OTA_HOME") == 0) {
                event_log_add("BLE admin: команда OTA_HOME");
                return wifi_ota_start() == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
            }
            if (strcmp(command, "DELETE_ALL") == 0) {
                event_log_add("BLE admin: команда DELETE_ALL");
                return ble_phone_delete_all_bonds() == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
            }
            if (strcmp(command, "REBOOT") == 0) {
                if (s_reboot_pending) return 0;
                s_reboot_pending = true;
                event_log_add("BLE admin: команда REBOOT");
                if (xTaskCreate(reboot_task, "ble_reboot", 3072, NULL, 5, NULL) != pdPASS) {
                    s_reboot_pending = false;
                    return BLE_ATT_ERR_INSUFFICIENT_RES;
                }
                return 0;
            }
            static const char delete_prefix[] = "DELETE:";
            if (strncmp(command, delete_prefix, sizeof(delete_prefix) - 1) == 0) {
                event_log_add("BLE admin: команда DELETE");
                esp_err_t err = ble_phone_delete_bond(command + sizeof(delete_prefix) - 1);
                return err == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            }
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
    }

    if (characteristic == CHR_ADMIN_SCHEMA) {
        if (!connection_is_admin(conn_handle, false)) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            app_config_t cfg;
            char json[ADMIN_SCHEMA_MAX_BYTES];
            char encoded[ADMIN_SCHEMA_CHUNK_B64_MAX];
            char value[512];
            int len;
            app_config_get(&cfg);
            len = snprintf(json, sizeof(json),
                "{\"schema\":1,\"revision\":1,\"title\":\"Настройки замка\","
                "\"sections\":[{\"id\":\"lock\",\"title\":\"Замок\",\"items\":["
                "{\"id\":\"phone_rssi_threshold\",\"title\":\"Порог RSSI ESP32 для допуска открытия\",\"type\":\"integer\",\"ui_type\":\"slider\",\"value\":%d,\"min\":-127,\"max\":0,\"step\":1,\"unit\":\"dBm\",\"read_only\":false},"
                "{\"id\":\"phone_rssi_hold_threshold\",\"title\":\"Порог RSSI ESP32 для потери присутствия\",\"type\":\"integer\",\"ui_type\":\"slider\",\"value\":%d,\"min\":-127,\"max\":0,\"step\":1,\"unit\":\"dBm\",\"read_only\":false},"
                "{\"id\":\"long_open_ms\",\"title\":\"Длительное открытие\",\"type\":\"integer\",\"ui_type\":\"slider\",\"value\":%lu,\"min\":1000,\"max\":3600000,\"step\":1,\"unit\":\"мс\",\"read_only\":false},"
                "{\"id\":\"timer_a_ms\",\"title\":\"Таймер A\",\"type\":\"integer\",\"ui_type\":\"slider\",\"value\":%lu,\"min\":0,\"max\":600000,\"step\":1,\"unit\":\"мс\",\"read_only\":false},"
                "{\"id\":\"timer_b_s\",\"title\":\"Таймер B\",\"type\":\"integer\",\"ui_type\":\"slider\",\"value\":%lu,\"min\":0,\"max\":3600,\"step\":1,\"unit\":\"с\",\"read_only\":false},"
                "{\"id\":\"lock_open_level\",\"title\":\"Уровень открытия\",\"type\":\"boolean\",\"ui_type\":\"switch\",\"value\":%s,\"read_only\":false},"
                "{\"id\":\"lock_gpio\",\"title\":\"GPIO замка\",\"type\":\"integer\",\"ui_type\":\"info\",\"value\":%d,\"read_only\":true}]},"
                "{\"id\":\"wifi_ota\",\"title\":\"Wi-Fi и OTA\",\"items\":["
                "{\"id\":\"ota_wifi_ssid\",\"title\":\"SSID Wi-Fi\",\"type\":\"string\",\"ui_type\":\"text\",\"value\":\"\",\"read_only\":false},"
                "{\"id\":\"ota_wifi_password\",\"title\":\"Пароль Wi-Fi\",\"type\":\"string\",\"ui_type\":\"text\",\"value\":\"\",\"read_only\":false},"
                "{\"id\":\"ota_firmware_url\",\"title\":\"HTTPS URL прошивки\",\"type\":\"string\",\"ui_type\":\"text\",\"value\":\"\",\"read_only\":false},"
                "{\"id\":\"ota_read\",\"title\":\"Прочитать Wi-Fi/OTA настройки\",\"type\":\"action\",\"ui_type\":\"button\",\"read_only\":false},"
                "{\"id\":\"ota_save\",\"title\":\"Сохранить Wi-Fi/OTA настройки\",\"type\":\"action\",\"ui_type\":\"button\",\"read_only\":false},"
                "{\"id\":\"ota_start\",\"title\":\"Запустить OTA\",\"type\":\"action\",\"ui_type\":\"button\",\"read_only\":false}]}]}",
                cfg.phone_rssi_threshold, cfg.phone_rssi_hold_threshold,
                (unsigned long)cfg.long_open_ms, (unsigned long)cfg.timer_a_ms,
                (unsigned long)cfg.timer_b_s,
                cfg.lock_open_level ? "true" : "false", cfg.lock_gpio);
            if (len < 0 || (size_t)len >= sizeof(json)) {
                event_log_add("BLE admin: схема настроек переполнена");
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)json, (uint32_t)len);
            uint16_t count = (uint16_t)((len + ADMIN_SCHEMA_CHUNK_BYTES - 1) /
                                        ADMIN_SCHEMA_CHUNK_BYTES);
            if (s_admin_schema_chunk_index >= count) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            size_t offset = (size_t)s_admin_schema_chunk_index * ADMIN_SCHEMA_CHUNK_BYTES;
            size_t chunk_len = (size_t)len - offset;
            if (chunk_len > ADMIN_SCHEMA_CHUNK_BYTES) chunk_len = ADMIN_SCHEMA_CHUNK_BYTES;
            size_t encoded_len = 0;
            if (mbedtls_base64_encode((unsigned char *)encoded, sizeof(encoded),
                                      &encoded_len, (const unsigned char *)json + offset,
                                      chunk_len) != 0) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            int framed_len = snprintf(value, sizeof(value),
                "schema=1;revision=1;index=%u;count=%u;length=%d;crc32=%08" PRIx32 "\n%s",
                s_admin_schema_chunk_index, count, len, crc, encoded);
            if (framed_len < 0 || (size_t)framed_len >= sizeof(value)) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            event_log_add("BLE admin: фрагмент схемы прочитан");
            return append_value(ctxt->om, value, (size_t)framed_len);
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_info_uuid.u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_LOCK_INFO,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = &s_command_uuid.u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_LOCK_COMMAND,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_LOCK_STATUS,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x06,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_ADMIN_CONFIG,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x07,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_ADMIN_STATE,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x08,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_ADMIN_BONDS,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x09,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_ADMIN_CONTROL,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x0a,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_ADMIN_EVENTS,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x0e,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_ADMIN_SCHEMA,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x0b,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_PHONE_NAME,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x0c,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_OTA_CONFIG,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &((ble_uuid128_t)BLE_UUID128_INIT(
                    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x0d,0x00,0x4f,0x6f)).u,
                .access_cb = gatt_access,
                .arg = (void *)(intptr_t)CHR_OTA_STATUS,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {0},
        },
    },
    {0},
};

static int advertising_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ble_phone_apply_tx_power();
                event_log_add("BLE GATT: телефон подключён");
                portENTER_CRITICAL(&s_connection_lock);
                s_conn_handle = event->connect.conn_handle;
                s_rssi_valid = false;
                s_encrypted = false;
                s_bonded = false;
                portEXIT_CRITICAL(&s_connection_lock);
                bool registration_allowed;
                ble_phone_get_registration(&registration_allowed, NULL);
                int bond_count = 0;
                bool bonded_peer = peer_has_bond(event->connect.conn_handle, &bond_count);
                portENTER_CRITICAL(&s_connection_lock);
                s_peer_was_bonded = bonded_peer;
                portEXIT_CRITICAL(&s_connection_lock);
                if (!bonded_peer && !registration_allowed) {
                    event_log_add("BLE security: новый телефон отклонён, регистрация закрыта");
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    return 0;
                }
                if (!bonded_peer && bond_count >= MAX_BONDED_PHONES) {
                    event_log_add("BLE security: лимит 20 bonds, удалите телефон через Web UI");
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    return 0;
                }
                // Не инициируем security проактивно здесь: на Android это создаёт
                // гонку с его собственным реактивным pairing, который стек телефона
                // сам запускает, как только GATT-клиент во время service discovery
                // натыкается на характеристику с флагом READ_ENC/WRITE_ENC
                // (например CHR_ADMIN_CONFIG). Два независимых инициатора
                // security на одном SMP-канале приводили к тому, что Android
                // показывал диалог подтверждения дважды и один раз сбрасывал
                // внутреннее состояние bonding (dm_reset_sec_dev_pending), прежде
                // чем шифрование всё же завершалось. Оставляем только реактивный
                // путь: телефон сам инициирует шифрование, когда попытается
                // прочитать/записать защищённую характеристику, и получит
                // BLE_ATT_ERR_INSUFFICIENT_AUTHEN от стека NimBLE.
                event_log_add("BLE security: ожидание запроса от телефона (%s)",
                              bonded_peer ? "известный bond" : "новый Pairing");
            } else {
                ble_phone_start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            event_log_add("BLE GATT: телефон отключён");
            portENTER_CRITICAL(&s_connection_lock);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_rssi_valid = false;
            s_encrypted = false;
            s_bonded = false;
            s_peer_was_bonded = false;
            portEXIT_CRITICAL(&s_connection_lock);
            lock_control_report_disconnected();
            ble_phone_start_advertising();
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE: {
            struct ble_gap_conn_desc desc;
            if (event->enc_change.status == 0 &&
                ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                event_log_add("BLE security: encrypted=%u bonded=%u key=%u",
                              desc.sec_state.encrypted, desc.sec_state.bonded,
                              desc.sec_state.key_size);
                bool disconnect_new_bond;
                portENTER_CRITICAL(&s_connection_lock);
                s_encrypted = desc.sec_state.encrypted;
                s_bonded = desc.sec_state.bonded;
                disconnect_new_bond = desc.sec_state.encrypted && desc.sec_state.bonded &&
                                      !s_peer_was_bonded && ap_mode_is_configuration();
                if (disconnect_new_bond) s_peer_was_bonded = true;
                portEXIT_CRITICAL(&s_connection_lock);
                if (disconnect_new_bond) {
                    ble_addr_t owner;
                    esp_err_t owner_err = owner_load(&owner);
                    if (owner_err == ESP_ERR_NVS_NOT_FOUND) {
                        if (owner_save(&desc.peer_id_addr) == ESP_OK) {
                            char address[18];
                            address_to_string(&desc.peer_id_addr, address);
                            event_log_add("BLE owner назначен при первом pairing: %s",
                                          address);
                        } else {
                            event_log_add("BLE owner: ошибка сохранения владельца");
                        }
                    }
                }
                if (disconnect_new_bond &&
                    xTaskCreate(disconnect_after_bond_task, "ble_bond_done", 3072,
                                (void *)(uintptr_t)event->enc_change.conn_handle,
                                5, NULL) != pdPASS) {
                    event_log_add("BLE registration: не удалось запустить автоотключение");
                }
            } else {
                event_log_add("BLE security failed: %d", event->enc_change.status);
            }
            return 0;
        }
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            event_log_add("BLE security: повторный Pairing отклонён; удалите bond через Web UI");
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            event_log_add("BLE security: ожидается IO action=%u",
                          event->passkey.params.action);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_phone_start_advertising();
            return 0;
        default:
            return 0;
    }
}

static void rssi_task(void *arg)
{
    (void)arg;
    uint16_t tracked_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    int64_t weak_ble_rssi_since_ms = 0;
    int64_t advertising_policy_check_ms = 0;
    for (;;) {
        uint16_t conn_handle;
        portENTER_CRITICAL(&s_connection_lock);
        conn_handle = s_conn_handle;
        portEXIT_CRITICAL(&s_connection_lock);
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - advertising_policy_check_ms >= 1000) {
            advertising_policy_check_ms = now_ms;
            bool registration_allowed;
            ble_phone_get_registration(&registration_allowed, NULL);
            if (conn_handle == BLE_HS_CONN_HANDLE_NONE && ble_gap_adv_active()) {
                ble_addr_t peers[MAX_BONDED_PHONES];
                int bond_count = ble_phone_get_bonded_peers(peers, MAX_BONDED_PHONES);
                bool should_restrict = !registration_allowed && bond_count > 0 &&
                                       bond_count <= CONFIG_BT_NIMBLE_WHITELIST_SIZE;
                if (bond_count >= 0 && should_restrict != s_advertising_restricted) {
                    event_log_add("BLE: окно регистрации %s, обновление списка допуска",
                                  registration_allowed ? "открыто" : "закрыто");
                    ble_phone_start_advertising();
                }
            }
        }
        if (conn_handle != tracked_conn_handle) {
            tracked_conn_handle = conn_handle;
            weak_ble_rssi_since_ms = 0;
        }
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            struct ble_gap_conn_desc desc;
            int8_t rssi;
            if (ble_gap_conn_find(conn_handle, &desc) == 0 &&
                ble_gap_conn_rssi(conn_handle, &rssi) == 0) {
                portENTER_CRITICAL(&s_connection_lock);
                if (s_conn_handle == conn_handle) {
                    s_last_rssi = rssi;
                    s_rssi_valid = true;
                }
                portEXIT_CRITICAL(&s_connection_lock);
                app_config_t cfg;
                app_config_get(&cfg);
                bool secured = desc.sec_state.encrypted && desc.sec_state.bonded;
                int8_t ble_threshold = secured
                    ? cfg.ble_disconnect_rssi_threshold
                    : cfg.ble_connect_rssi_threshold;
                if (rssi < ble_threshold) {
                    if (weak_ble_rssi_since_ms == 0) {
                        weak_ble_rssi_since_ms = esp_timer_get_time() / 1000;
                    } else if ((esp_timer_get_time() / 1000) - weak_ble_rssi_since_ms >=
                               BLE_RSSI_DISCONNECT_GRACE_MS) {
                        event_log_add("BLE RSSI слабый: %d < %d dBm, разрыв %s после %d мс",
                                      rssi, ble_threshold,
                                      secured ? "соединения" : "до pairing",
                                      BLE_RSSI_DISCONNECT_GRACE_MS);
                        weak_ble_rssi_since_ms = 0;
                        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    }
                } else {
                    weak_ble_rssi_since_ms = 0;
                }
                if (!secured) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                // Hysteresis: getting the door open requires the strict threshold;
                // once it's open, only the weaker hold threshold is required to keep
                // it from starting the Timer A countdown. Using a single shared
                // threshold for both directions made RSSI noise right at the
                // boundary flip open/hold state rapidly (chatter/дребезг).
                //
                // While reopen_blocked (anti-flap guard after "long open"), the lock
                // is physically closed, so is_open() alone would wrongly select the
                // strict open threshold here. The 3-consecutive-weak-RSSI release
                // must be measured against the weaker hold threshold too, since that
                // is the "слабый порог" the block release is meant to react to.
                bool use_hold_threshold = lock_control_is_open() || lock_control_is_blocked();
                int8_t effective_threshold = use_hold_threshold
                    ? cfg.phone_rssi_hold_threshold
                    : cfg.phone_rssi_threshold;
                bool sufficient = rssi >= effective_threshold;
                lock_control_report_rssi(sufficient);
            }
        } else {
            weak_ble_rssi_since_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t ble_phone_gatt_init(void)
{
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 0;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(BLE_PHONE_NAME);
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_count_cfg(s_services);
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) return ESP_FAIL;
    return xTaskCreate(rssi_task, "ble_phone_rssi", 3072, NULL, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t ble_phone_start_advertising(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) return ESP_FAIL;

    bool registration_allowed;
    ble_phone_get_registration(&registration_allowed, NULL);
    int bond_count = 0;
    bool restricted = !registration_allowed;
    if (ble_gap_adv_active()) {
        rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) return ESP_FAIL;
    }
    if (restricted && bond_count > CONFIG_BT_NIMBLE_WHITELIST_SIZE) {
        // The ESP32-S3 controller has a 15-entry accept list in this build.
        // Keep advertising open so bonds 16..20 remain usable; the existing
        // peer_has_bond() check still rejects unknown phones after connect.
        restricted = false;
    }
    if (ble_phone_refresh_accept_list(restricted, &bond_count) != ESP_OK) return ESP_FAIL;
    restricted = restricted && bond_count > 0;
    s_advertising_restricted = restricted;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)BLE_PHONE_NAME;
    fields.name_len = strlen(BLE_PHONE_NAME);
    fields.name_is_complete = 1;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return ESP_FAIL;

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.filter_policy = restricted ? BLE_HCI_ADV_FILT_CONN : BLE_HCI_ADV_FILT_NONE;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params,
                           advertising_event, NULL);
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        event_log_add("BLE GATT advertising запущен");
        return ESP_OK;
    }
    event_log_add("BLE GATT advertising error: %d", rc);
    return ESP_FAIL;
}

static void on_sync(void)
{
    event_log_add("NimBLE phone-only host синхронизирован");
    if (ble_phone_start_advertising() != ESP_OK) {
        event_log_add("BLE phone advertising не запущен");
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_phone_init(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    err = ble_phone_apply_tx_power();
    if (err != ESP_OK) {
        event_log_add("BLE: мощность не применена, запуск с уровнем контроллера по умолчанию");
    }
    err = ble_phone_gatt_init();
    if (err != ESP_OK) return err;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
