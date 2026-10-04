#include "ble_phone.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
#include "ap_mode.h"
#include "app_config.h"
#include "event_log.h"
#include "led.h"
#include "lock_control.h"
#include "wifi_ota.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/hci_common.h"
#include "esp_bt.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *BLE_PHONE_TAG = "ble_phone";
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define BLE_PHONE_NAME "BLE Lock"
#define REGISTRATION_WINDOW_US (60LL * 1000000LL)
#define MAX_BONDED_PHONES 20
#define BLE_LINK_PROBE_FAILURE_GRACE_MS 2000
#define BLE_GUEST_SLOTS 5
#define BLE_TOTAL_SLOTS (BLE_GUEST_SLOTS + 1)
#define BLE_CONNECTION_SUPERVISION_TIMEOUT 300
#define CENTRAL_CONNECT_TIMEOUT_MS 10000
#define CENTRAL_SECURITY_TIMEOUT_MS 10000
#define CENTRAL_REGISTRATION_SECURITY_TIMEOUT_MS 55000
#define CENTRAL_GATT_TIMEOUT_MS 8000
#define CENTRAL_NEW_BOND_SETTLE_MS 5000
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

/* Android phone beacon: public discovery marker, protected GATT proof. */
static const ble_uuid128_t s_phone_beacon_service_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x10, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_beacon_proof_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x11, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_beacon_owner_status_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x12, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_beacon_wifi_setup_chunk_uuids[] = {
    BLE_UUID128_INIT(0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
                     0x21, 0x4c, 0x7b, 0x9a, 0x13, 0x00, 0x4f, 0x6f),
    BLE_UUID128_INIT(0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
                     0x21, 0x4c, 0x7b, 0x9a, 0x16, 0x00, 0x4f, 0x6f),
    BLE_UUID128_INIT(0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
                     0x21, 0x4c, 0x7b, 0x9a, 0x17, 0x00, 0x4f, 0x6f),
    BLE_UUID128_INIT(0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
                     0x21, 0x4c, 0x7b, 0x9a, 0x18, 0x00, 0x4f, 0x6f),
    BLE_UUID128_INIT(0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
                     0x21, 0x4c, 0x7b, 0x9a, 0x19, 0x00, 0x4f, 0x6f),
};
static const ble_uuid128_t s_phone_beacon_hotspot_ip_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x14, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_beacon_name_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x15, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_beacon_web_ui_ip_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x1a, 0x00, 0x4f, 0x6f);
#define PHONE_BEACON_COMPANY_ID 0xf0e1
#define PHONE_BEACON_RETRY_MS 1000

static uint8_t s_addr_type;
static _Atomic uint32_t s_central_suppressed_device_id;
static _Atomic bool s_central_suppressed_device_id_valid;
static portMUX_TYPE s_registration_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_registration_deadline_us;
static portMUX_TYPE s_connection_lock = portMUX_INITIALIZER_UNLOCKED;
typedef struct {
    uint16_t conn_handle;
    bool is_owner_slot;
    int8_t rssi_samples[3];
    unsigned rssi_count, rssi_next;
    int8_t last_rssi;
    bool rssi_valid;
    bool encrypted;
    bool bonded;
    bool peer_was_bonded;
    bool peer_registration_candidate;
    int64_t link_probe_failure_since_ms;
    int64_t link_terminate_retry_ms;
} ble_slot_t;
static ble_slot_t s_slots[BLE_TOTAL_SLOTS] = {
    { .conn_handle = BLE_HS_CONN_HANDLE_NONE },
    { .conn_handle = BLE_HS_CONN_HANDLE_NONE },
    { .conn_handle = BLE_HS_CONN_HANDLE_NONE },
    { .conn_handle = BLE_HS_CONN_HANDLE_NONE },
    { .conn_handle = BLE_HS_CONN_HANDLE_NONE },
    { .conn_handle = BLE_HS_CONN_HANDLE_NONE },
};
static atomic_bool s_advertising_restricted;
static struct ble_npl_event s_advertising_event;
static atomic_bool s_advertising_ready;
static TaskHandle_t s_rssi_task;
unsigned ble_phone_stack_free(void)
{
    return s_rssi_task ? (unsigned)uxTaskGetStackHighWaterMark(s_rssi_task) : 0;
}

#define ADMIN_SCHEMA_MAX_BYTES 2048
#define ADMIN_SCHEMA_CHUNK_BYTES 240
#define ADMIN_SCHEMA_CHUNK_B64_MAX ((ADMIN_SCHEMA_CHUNK_BYTES + 2) / 3 * 4 + 1)
static uint16_t s_admin_schema_chunk_index;
static bool s_reboot_pending;

static int slot_index_for_handle_locked(uint16_t conn_handle)
{
    for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
        if (s_slots[i].conn_handle == conn_handle) return i;
    }
    return -1;
}

static int free_slot_locked(void)
{
    for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
        if (s_slots[i].conn_handle == BLE_HS_CONN_HANDLE_NONE) return i;
    }
    return -1;
}

static bool any_slot_free(void)
{
    bool free_slot;
    portENTER_CRITICAL(&s_connection_lock);
    free_slot = free_slot_locked() >= 0;
    portEXIT_CRITICAL(&s_connection_lock);
    return free_slot;
}

static bool any_secured_slot_locked(void)
{
    for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
        if (s_slots[i].conn_handle != BLE_HS_CONN_HANDLE_NONE &&
            s_slots[i].encrypted && s_slots[i].bonded) return true;
    }
    return false;
}

static void clear_slot_locked(ble_slot_t *slot)
{
    memset(slot, 0, sizeof(*slot));
    slot->conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

static esp_err_t ble_phone_start_advertising(void);
static void central_cancel_for_bond_reset(void);
static void central_finish_bond_reset(void);

esp_err_t ble_phone_apply_tx_power(void)
{
    const esp_power_level_t level = (esp_power_level_t)((APP_BLE_TX_POWER_DBM + 12) / 3);
    esp_err_t adv_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, level);
    esp_err_t default_err = ESP_OK;
    if (adv_err == ESP_OK) {
        default_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, level);
    }
    if (adv_err != ESP_OK) {
        /* While an administrative phone is connected, advertising is
         * stopped, and the controller can reject an ADV power update. The
         * compile-time power constant remains unchanged. */
        event_log_add("BLE: мощность рекламы %d dBm отложена до запуска рекламы: %s",
                      APP_BLE_TX_POWER_DBM, esp_err_to_name(adv_err));
        return ESP_OK;
    }
    if (default_err != ESP_OK) {
        /* Advertising power is the user-facing setting. Some controller
         * states may reject the independent default level while advertising
         * power has already been applied successfully. */
        event_log_add("BLE: мощность рекламы установлена: %d dBm; общий уровень не изменён: %s",
                      APP_BLE_TX_POWER_DBM, esp_err_to_name(default_err));
    } else {
        event_log_add("BLE: мощность передатчика установлена: %d dBm",
                      APP_BLE_TX_POWER_DBM);
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

static esp_err_t ble_phone_refresh_accept_list(bool restricted, const ble_addr_t *peers, int count)
{
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
    // Leave enough time for bond-store/NVS writes and final BLE security
    // responses to settle before resetting the controller.
    vTaskDelay(pdMS_TO_TICKS(1000));
    wifi_ota_prepare_restart();
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

void ble_phone_get_status(ble_phone_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_connection_lock);
    out->connected = false;
    out->encrypted = false;
    out->bonded = false;
    out->rssi_valid = false;
    out->rssi = 0;
    int selected = s_slots[0].conn_handle != BLE_HS_CONN_HANDLE_NONE ? 0 : -1;
    if (selected < 0) {
        for (int i = 1; i < BLE_TOTAL_SLOTS; i++) {
            if (s_slots[i].conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                selected = i;
                break;
            }
        }
    }
    if (selected >= 0) {
        const ble_slot_t *slot = &s_slots[selected];
        out->connected = true;
        out->encrypted = slot->encrypted;
        out->bonded = slot->bonded;
        out->rssi_valid = slot->rssi_valid;
        out->rssi = slot->last_rssi;
    }
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

static void disconnect_deleted_peer(const ble_addr_t *peer, bool disconnect_any)
{
    uint16_t handles[BLE_TOTAL_SLOTS];
    size_t count = 0;
    portENTER_CRITICAL(&s_connection_lock);
    for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
        if (s_slots[i].conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            handles[count++] = s_slots[i].conn_handle;
        }
    }
    portEXIT_CRITICAL(&s_connection_lock);
    for (size_t i = 0; i < count; i++) {
        uint16_t conn_handle = handles[i];
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
            (!disconnect_any && !address_equal(peer, &desc.peer_id_addr))) continue;

        bool report_disconnected = false;
        portENTER_CRITICAL(&s_connection_lock);
        int slot_index = slot_index_for_handle_locked(conn_handle);
        if (slot_index >= 0) {
            bool was_secured = s_slots[slot_index].encrypted && s_slots[slot_index].bonded;
            clear_slot_locked(&s_slots[slot_index]);
            report_disconnected = was_secured && !any_secured_slot_locked();
        }
        portEXIT_CRITICAL(&s_connection_lock);
        if (report_disconnected) lock_control_report_disconnected();

        int rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        event_log_add("BLE: сессия удалённого bond завершается, rc=%d", rc);
    }
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
                disconnect_deleted_peer(&peers[i], false);
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
    central_cancel_for_bond_reset();

    ble_addr_t peers[MAX_BONDED_PHONES];
    int count = 0;
    esp_err_t result = ESP_OK;
    if (ble_store_util_bonded_peers(peers, &count, MAX_BONDED_PHONES) != 0) result = ESP_FAIL;
    for (int i = 0; i < count; i++) {
        if (ble_store_util_delete_peer(&peers[i]) != 0) {
            result = ESP_FAIL;
            break;
        }
        bond_name_delete(&peers[i]);
    }
    if (result == ESP_OK) result = owner_clear();
    if (result == ESP_OK) {
        disconnect_deleted_peer(NULL, true);
        event_log_add("Все BLE bonds удалены: %d", count);
        if (led_flash_bond_reset() != ESP_OK) {
            event_log_add("LED: не удалось запустить индикацию сброса bonds");
        }
    } else {
        event_log_add("BLE reset: не удалось полностью удалить bonds/owner: %s",
                      esp_err_to_name(result));
    }

    central_finish_bond_reset();
    return result;
}

void ble_phone_allow_registration(void)
{
    atomic_store(&s_central_suppressed_device_id_valid, false);
    portENTER_CRITICAL(&s_registration_lock);
    s_registration_deadline_us = esp_timer_get_time() + REGISTRATION_WINDOW_US;
    portEXIT_CRITICAL(&s_registration_lock);
    event_log_add("BLE registration: окно открыто на 60 секунд");
    led_set_configuration(true);

    // Release active phones so a new handset can reach Pairing/Bonding.
    uint16_t handles[BLE_TOTAL_SLOTS];
    size_t count = 0;
    portENTER_CRITICAL(&s_connection_lock);
    for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
        if (s_slots[i].conn_handle != BLE_HS_CONN_HANDLE_NONE)
            handles[count++] = s_slots[i].conn_handle;
    }
    portEXIT_CRITICAL(&s_connection_lock);
    if (count != 0) {
        event_log_add("BLE registration: существующее соединение разорвано для регистрации");
        for (size_t i = 0; i < count; i++)
            ble_gap_terminate(handles[i], BLE_ERR_REM_USER_CONN_TERM);
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
        uint16_t handles[BLE_TOTAL_SLOTS];
        size_t count = 0;
        portENTER_CRITICAL(&s_connection_lock);
        for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
            if (s_slots[i].conn_handle != BLE_HS_CONN_HANDLE_NONE)
                handles[count++] = s_slots[i].conn_handle;
        }
        portEXIT_CRITICAL(&s_connection_lock);
        if (count != 0) {
            for (size_t i = 0; i < count; i++)
                ble_gap_terminate(handles[i], BLE_ERR_REM_USER_CONN_TERM);
        } else {
            ble_phone_start_advertising();
        }
    } else {
        uint16_t handles[BLE_TOTAL_SLOTS];
        size_t count = 0;
        portENTER_CRITICAL(&s_connection_lock);
        for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
            if (s_slots[i].conn_handle != BLE_HS_CONN_HANDLE_NONE)
                handles[count++] = s_slots[i].conn_handle;
        }
        portEXIT_CRITICAL(&s_connection_lock);
        if (count != 0) {
            event_log_add("BLE: выход из режима настройки, соединение отключается");
            for (size_t i = 0; i < count; i++)
                ble_gap_terminate(handles[i], BLE_ERR_REM_USER_CONN_TERM);
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

static bool connection_is_admin(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
        !desc.sec_state.encrypted || !desc.sec_state.bonded) {
        return false;
    }

    ble_addr_t owner;
    esp_err_t err = owner_load(&owner);
    return err == ESP_OK && address_equal(&owner, &desc.peer_id_addr);
}

static bool connection_is_encrypted_bonded(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(conn_handle, &desc) == 0 &&
           desc.sec_state.encrypted && desc.sec_state.bonded;
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    intptr_t characteristic = (intptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && characteristic == CHR_LOCK_INFO) {
        static const char prefix[] = "BLE Lock ";
        static const char build_separator[] = " | ";
        int rc = append_value(ctxt->om, prefix, sizeof(prefix) - 1);
        if (rc == 0) rc = append_value(ctxt->om, APP_VERSION, strlen(APP_VERSION));
        if (rc == 0) rc = append_value(ctxt->om, build_separator, sizeof(build_separator) - 1);
        return rc == 0 ? append_value(ctxt->om, APP_BUILD_TIMESTAMP,
                                      strlen(APP_BUILD_TIMESTAMP)) : rc;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && characteristic == CHR_LOCK_STATUS) {
        const char *status = lock_control_status_name();
        return append_value(ctxt->om, status, strlen(status));
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && characteristic == CHR_LOCK_COMMAND) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        char command[20] = {0};
        if (len >= sizeof(command) || ble_hs_mbuf_to_flat(ctxt->om, command,
                                                          sizeof(command) - 1, NULL) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (len == 10 && memcmp(command, "FORCE_OPEN", 10) == 0) {
            if (!connection_is_admin(conn_handle)) {
                event_log_add("BLE FORCE_OPEN отклонён: требуется владелец/admin");
                return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
            }
            if (lock_request_manual_open() != ESP_OK) {
                event_log_add("BLE FORCE_OPEN: очередь автомата недоступна");
                return BLE_ATT_ERR_UNLIKELY;
            }
            event_log_add("BLE FORCE_OPEN принят: открытие без RSSI-порога");
            return 0;
        }
        if ((len == 9 && memcmp(command, "MODE_AUTO", 9) == 0) ||
            (len == 11 && memcmp(command, "MODE_ALWAYS", 11) == 0)) {
            if (!connection_is_encrypted_bonded(conn_handle)) {
                event_log_add("BLE MODE отклонён: требуется encrypted/bonded соединение");
                return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
            }
            bool always_open = len == 11;
            if (lock_request_set_hold_open(always_open) != ESP_OK) {
                event_log_add("BLE MODE: очередь автомата недоступна");
                return BLE_ATT_ERR_UNLIKELY;
            }
            event_log_add("BLE MODE принят: %s", always_open ? "ALWAYS_OPEN" : "AUTO");
            return 0;
        }
        if (len == 14 && memcmp(command, "AUTO_OFF_CLOSE", 14) == 0) {
            if (!connection_is_encrypted_bonded(conn_handle)) {
                event_log_add("BLE AUTO_OFF_CLOSE отклонён: требуется encrypted/bonded соединение");
                return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
            }
            if (lock_request_disable_auto_and_close() != ESP_OK) {
                event_log_add("BLE AUTO_OFF_CLOSE: очередь автомата недоступна");
                return BLE_ATT_ERR_UNLIKELY;
            }
            event_log_add("BLE AUTO_OFF_CLOSE принят: автоматика выключается, замок закрывается");
            return 0;
        }
        if (len == 9 && memcmp(command, "KEEP_OPEN", 9) == 0) {
            if (!connection_is_admin(conn_handle)) {
                event_log_add("BLE KEEP_OPEN отклонён: требуется владелец/admin");
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
            if (lock_request_open() != ESP_OK) {
                event_log_add("BLE OPEN: очередь автомата недоступна");
                return BLE_ATT_ERR_UNLIKELY;
            }
            event_log_add("BLE OPEN принят: соединение encrypted/bonded");
            return 0;
        }
        event_log_add("BLE GATT: неизвестная команда");
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && characteristic == CHR_PHONE_NAME) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) != 0 ||
            !desc.sec_state.encrypted || !desc.sec_state.bonded) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
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
        esp_err_t err = bond_name_save_once(&desc.peer_id_addr, name);
        if (err != ESP_OK) return BLE_ATT_ERR_UNLIKELY;
        event_log_add("BLE phone name принято для bonded MAC");
        return 0;
    }

    if (characteristic == CHR_OTA_CONFIG) {
        static const char persistent_prefix[] = "PERSIST\n";
        if (!connection_is_admin(conn_handle)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            char value[sizeof(persistent_prefix) - 1 + WIFI_OTA_SSID_MAX +
                       WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 4];
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
            if (len > sizeof(persistent_prefix) - 1 &&
                memcmp(value, persistent_prefix, sizeof(persistent_prefix) - 1) == 0) {
                const size_t payload_offset = sizeof(persistent_prefix) - 1;
                esp_err_t err = wifi_ota_set_config(value + payload_offset, len - payload_offset);
                if (err == ESP_OK) {
                    event_log_add("Wi-Fi/OTA config сохранена через BLE");
                    ESP_LOGI(BLE_PHONE_TAG, "Persistent Wi-Fi/OTA configuration saved through BLE");
                    return 0;
                }
                ESP_LOGE(BLE_PHONE_TAG, "Persistent Wi-Fi/OTA BLE save failed: %s", esp_err_to_name(err));
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            }
            return wifi_ota_set_runtime_config(value, len) == ESP_OK ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
    }

    if (characteristic == CHR_OTA_STATUS) {
        if (!connection_is_admin(conn_handle)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            char value[32];
            esp_err_t err = wifi_ota_get_status(value, sizeof(value));
            return err == ESP_OK ? append_value(ctxt->om, value, strlen(value))
                                 : BLE_ATT_ERR_UNLIKELY;
        }
    }

    if (characteristic >= CHR_ADMIN_CONFIG && characteristic <= CHR_ADMIN_EVENTS) {
        if (!connection_is_admin(conn_handle)) {
            // A bonded guest can read lock status, but must not enter the
            // Android security retry loop while probing owner-only settings.
            if (characteristic == CHR_ADMIN_STATE &&
                ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
                return BLE_ATT_ERR_READ_NOT_PERMITTED;
            }
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
        if (characteristic == CHR_ADMIN_CONFIG && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            app_config_t cfg;
            char value[96];
            app_config_get(&cfg);
            /* Keep the legacy five-field wire layout; fields 2 and 4 are reserved. */
            int len = snprintf(value, sizeof(value), "%d,%d,0,%lu,%lu",
                               cfg.phone_rssi_threshold,
                               cfg.phone_rssi_threshold,
                               (unsigned long)cfg.timer_a_ms,
                               0UL);
            return append_value(ctxt->om, value, (size_t)len);
        }
        if (characteristic == CHR_ADMIN_CONFIG && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            char value[96] = {0};
            long fields[6] = {0};
            int field_count = 0;
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len == 0 || len >= sizeof(value) ||
                ble_hs_mbuf_to_flat(ctxt->om, value, sizeof(value) - 1, NULL) != 0 ||
                memchr(value, 0, len)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            char *cursor = value;
            for (int i = 0; i < 6; i++) {
                char *end;
                errno = 0;
                fields[i] = strtol(cursor, &end, 10);
                if (errno || end == cursor ||
                    (i < 2 && (fields[i] < APP_RSSI_THRESHOLD_MIN ||
                               fields[i] > APP_RSSI_THRESHOLD_MAX)) ||
                    (i >= 2 && i < 5 && fields[i] < 0))
                    return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
                if (*end == '\0') {
                    field_count = i + 1;
                    break;
                }
                if (*end != ',' || i == 5) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
                cursor = end + 1;
            }
            if (field_count != 5 && field_count != 6) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            esp_err_t err = app_config_set_logic((int)fields[0],
                                                  (uint32_t)fields[3]);
            if (err != ESP_OK) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            err = lock_control_config_changed();
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
                               lock_control_status_name(),
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
        if (!connection_is_admin(conn_handle)) {
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
                "{\"id\":\"phone_rssi_threshold\",\"title\":\"RSSI-порог зоны открытия и ухода (телефон → ESP32)\",\"type\":\"integer\",\"ui_type\":\"slider\",\"value\":%d,\"min\":-127,\"max\":0,\"step\":1,\"unit\":\"dBm\",\"read_only\":false},"
                "{\"id\":\"timer_a_ms\",\"title\":\"Таймер A\",\"type\":\"integer\",\"ui_type\":\"slider\",\"value\":%lu,\"min\":0,\"max\":600000,\"step\":1,\"unit\":\"мс\",\"read_only\":false}]},"
                "{\"id\":\"wifi_ota\",\"title\":\"Wi-Fi и OTA\",\"items\":["
                "{\"id\":\"ota_wifi_ssid\",\"title\":\"SSID Wi-Fi\",\"type\":\"string\",\"ui_type\":\"text\",\"value\":\"\",\"read_only\":false},"
                "{\"id\":\"ota_wifi_password\",\"title\":\"Пароль Wi-Fi\",\"type\":\"string\",\"ui_type\":\"text\",\"value\":\"\",\"read_only\":false},"
                "{\"id\":\"ota_firmware_url\",\"title\":\"HTTPS URL прошивки\",\"type\":\"string\",\"ui_type\":\"text\",\"value\":\"\",\"read_only\":false},"
                "{\"id\":\"ota_read\",\"title\":\"Прочитать Wi-Fi/OTA настройки\",\"type\":\"action\",\"ui_type\":\"button\",\"read_only\":false},"
                "{\"id\":\"ota_save\",\"title\":\"Сохранить Wi-Fi/OTA настройки\",\"type\":\"action\",\"ui_type\":\"button\",\"read_only\":false},"
                "{\"id\":\"ota_start\",\"title\":\"Запустить OTA\",\"type\":\"action\",\"ui_type\":\"button\",\"read_only\":false}]}]}",
                cfg.phone_rssi_threshold,
                (unsigned long)cfg.timer_a_ms);
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

/* One outgoing connection at a time. The existing inbound admin slots remain intact. */
static struct ble_npl_event s_central_event;
static ble_addr_t s_central_target;
static uint16_t s_central_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_central_svc_start, s_central_svc_end;
static uint16_t s_central_proof_handle, s_central_owner_status_handle;
static uint16_t s_central_name_handle, s_central_web_ui_ip_handle;
static char s_central_name_value[BLE_PHONE_NAME_MAX_LEN + 1];
static size_t s_central_name_value_len;
#define CENTRAL_WIFI_SETUP_CHUNK_SIZE 20
#define CENTRAL_WIFI_SETUP_CHUNK_COUNT \
    (sizeof(s_phone_beacon_wifi_setup_chunk_uuids) / sizeof(s_phone_beacon_wifi_setup_chunk_uuids[0]))
static uint16_t s_central_wifi_setup_handles[CENTRAL_WIFI_SETUP_CHUNK_COUNT];
static uint16_t s_central_hotspot_ip_handle;
static uint8_t s_central_wifi_setup_value[2 + WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX];
static uint16_t s_central_wifi_setup_value_len;
static uint16_t s_central_wifi_setup_expected_len;
static char s_central_runtime_credentials[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + 2];
static size_t s_central_runtime_credentials_len;
static uint32_t s_central_device_id, s_central_session_id;
static uint32_t s_central_logged_session_id;
static int8_t s_central_rssi;
static portMUX_TYPE s_central_low_rssi_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_central_low_rssi_count;
static int8_t s_central_low_rssi_min = INT8_MAX;
static int8_t s_central_low_rssi_max = INT8_MIN;
static _Atomic int64_t s_central_retry_ms;
static int64_t s_central_last_beacon_log_ms, s_central_last_success_log_ms;
static bool s_central_scanning, s_central_connecting, s_central_target_pending;
static bool s_central_registration_candidate;
static bool s_central_existing_bond_before_connect;
static bool s_central_existing_ltk_before_connect;
static bool s_central_existing_ltk_restored;
static bool s_central_new_bond_settling;
static bool s_central_wifi_provision_candidate;
static bool s_central_webui_ip_candidate;
static _Atomic bool s_central_reset_pending;
static _Atomic bool s_central_timeout_pending;
static _Atomic int s_central_phase;
static _Atomic int64_t s_central_phase_deadline_ms;
static struct ble_npl_event s_central_timeout_event;

enum {
    CENTRAL_PHASE_IDLE = 0,
    CENTRAL_PHASE_CONNECTING,
    CENTRAL_PHASE_SECURITY,
    CENTRAL_PHASE_SERVICE_DISCOVERY,
    CENTRAL_PHASE_CHARACTERISTIC_DISCOVERY,
    CENTRAL_PHASE_PROOF_READ,
    CENTRAL_PHASE_NAME_READ,
    CENTRAL_PHASE_OWNER_STATUS_WRITE,
    CENTRAL_PHASE_WEB_UI_IP_WRITE,
    CENTRAL_PHASE_WIFI_SETUP_READ,
    CENTRAL_PHASE_HOTSPOT_IP_READ,
    CENTRAL_PHASE_NEW_BOND_SETTLE,
};

static void central_set_phase(int phase, uint32_t timeout_ms)
{
    atomic_store(&s_central_phase, phase);
    atomic_store(&s_central_phase_deadline_ms,
                 timeout_ms ? esp_timer_get_time() / 1000 + timeout_ms : 0);
}

static void central_cancel_for_bond_reset(void)
{
    atomic_store(&s_central_reset_pending, true);
    portENTER_CRITICAL(&s_registration_lock);
    s_registration_deadline_us = 0;
    portEXIT_CRITICAL(&s_registration_lock);
    s_central_target_pending = false;
    s_central_registration_candidate = false;
    s_central_existing_bond_before_connect = false;
    s_central_existing_ltk_before_connect = false;
    s_central_existing_ltk_restored = false;
    s_central_new_bond_settling = false;
    central_set_phase(CENTRAL_PHASE_IDLE, 0);
    if (s_central_scanning) {
        int rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY)
            event_log_add("BLE reset: scan cancel error=%d", rc);
        s_central_scanning = false;
    }
    if (s_central_connecting) {
        int rc = ble_gap_conn_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY)
            event_log_add("BLE reset: connection cancel error=%d", rc);
        s_central_connecting = false;
    }
    if (s_central_handle != BLE_HS_CONN_HANDLE_NONE) {
        int rc = ble_gap_terminate(s_central_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN)
            event_log_add("BLE reset: central disconnect error=%d", rc);
    }
}

static void central_finish_bond_reset(void)
{
    atomic_store(&s_central_reset_pending, false);
    s_central_retry_ms = esp_timer_get_time() / 1000 + PHONE_BEACON_RETRY_MS;
    ble_phone_start_advertising();
}

static void central_schedule_retry(void)
{
    s_central_scanning = false;
    s_central_connecting = false;
    s_central_target_pending = false;
    s_central_registration_candidate = false;
    s_central_existing_bond_before_connect = false;
    s_central_existing_ltk_before_connect = false;
    s_central_existing_ltk_restored = false;
    s_central_new_bond_settling = false;
    s_central_wifi_provision_candidate = false;
    s_central_webui_ip_candidate = false;
    memset(s_central_wifi_setup_handles, 0, sizeof(s_central_wifi_setup_handles));
    s_central_name_handle = 0;
    s_central_web_ui_ip_handle = 0;
    s_central_name_value_len = 0;
    s_central_hotspot_ip_handle = 0;
    s_central_runtime_credentials_len = 0;
    s_central_handle = BLE_HS_CONN_HANDLE_NONE;
    central_set_phase(CENTRAL_PHASE_IDLE, 0);
    atomic_store(&s_central_timeout_pending, false);
    s_central_retry_ms = esp_timer_get_time() / 1000 + PHONE_BEACON_RETRY_MS;
}

static void central_disconnect(uint16_t handle)
{
    central_set_phase(CENTRAL_PHASE_IDLE, 0);
    int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) central_schedule_retry();
}

static void central_reject_stale_pairing(uint16_t handle, const char *cause)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(handle, &desc) != 0) {
        event_log_add("BLE central: %s; peer descriptor недоступен", cause);
        central_disconnect(handle);
        return;
    }

    char address[18];
    address_to_string(&desc.peer_id_addr, address);
    atomic_store(&s_central_suppressed_device_id, s_central_device_id);
    atomic_store(&s_central_suppressed_device_id_valid, true);
    if (s_central_existing_ltk_restored) {
        event_log_add("BLE central: existing LTK restored; keep bond, suppress peer");
    } else {
        esp_err_t delete_err = ble_phone_delete_bond(address);
        if (delete_err == ESP_OK) {
            event_log_add("BLE central: %s; stale bond %s удалён, peer подавлен до BOOT window",
                          cause, address);
        } else {
            event_log_add("BLE central: %s; stale bond %s не удалён (%s), peer подавлен до BOOT window",
                          cause, address, esp_err_to_name(delete_err));
        }
    }
    s_central_registration_candidate = false;
    central_disconnect(handle);
}

static void central_finish_proof_session(uint16_t handle)
{
    if (s_central_new_bond_settling) {
        event_log_add("BLE central: первый bond, выдерживаю соединение 5 с для Android SMP");
        central_set_phase(CENTRAL_PHASE_NEW_BOND_SETTLE, CENTRAL_NEW_BOND_SETTLE_MS);
        return;
    }
    central_disconnect(handle);
}

static bool central_connection_secured(uint16_t handle);

static int central_web_ui_ip_write_cb(uint16_t handle, const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    if (error->status == 0)
        event_log_add("BLE central: ESP Web UI IPv4 передан bonded owner");
    else
        event_log_add("BLE central: Web UI IPv4 write failed=%d", error->status);
    central_finish_proof_session(handle);
    return 0;
}

static bool central_try_send_web_ui_ip(uint16_t handle)
{
    if (!s_central_web_ui_ip_handle) return false;

    struct ble_gap_conn_desc desc;
    ble_addr_t owner;
    if (!central_connection_secured(handle) || ble_gap_conn_find(handle, &desc) != 0 ||
        owner_load(&owner) != ESP_OK || !address_equal(&owner, &desc.peer_id_addr)) return false;

    static const char prefix[] = "WEB_READY;IP=";
    char status[32];
    if (wifi_ota_get_status(status, sizeof(status)) != ESP_OK ||
        strncmp(status, prefix, sizeof(prefix) - 1) != 0) return false;

    const char *ip = status + sizeof(prefix) - 1;
    unsigned octets[4];
    char trailing;
    if (strlen(ip) > 15 || sscanf(ip, "%u.%u.%u.%u%c", &octets[0], &octets[1],
                                  &octets[2], &octets[3], &trailing) != 4 ||
        octets[0] > 255 || octets[1] > 255 || octets[2] > 255 || octets[3] > 255 ||
        (octets[0] == 0 && octets[1] == 0 && octets[2] == 0 && octets[3] == 0)) return false;

    central_set_phase(CENTRAL_PHASE_WEB_UI_IP_WRITE, CENTRAL_GATT_TIMEOUT_MS);
    if (ble_gattc_write_flat(handle, s_central_web_ui_ip_handle, ip, strlen(ip),
                             central_web_ui_ip_write_cb, NULL) == 0) return true;
    event_log_add("BLE central: Web UI IPv4 write could not start");
    return false;
}

static int central_owner_status_write_cb(uint16_t handle, const struct ble_gatt_error *error,
                                         struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    if (error->status != 0)
        event_log_add("BLE central: owner status notification failed=%d", error->status);
    if (central_try_send_web_ui_ip(handle)) return 0;
    central_finish_proof_session(handle);
    return 0;
}

static void central_send_owner_status(uint16_t handle)
{
    if (!s_central_owner_status_handle) {
        if (central_try_send_web_ui_ip(handle)) return;
        central_finish_proof_session(handle); // Older PhoneBeacon versions have no owner status characteristic.
        return;
    }
    struct ble_gap_conn_desc desc;
    ble_addr_t owner;
    uint8_t is_owner = ble_gap_conn_find(handle, &desc) == 0 &&
                       owner_load(&owner) == ESP_OK &&
                       address_equal(&owner, &desc.peer_id_addr);
    central_set_phase(CENTRAL_PHASE_OWNER_STATUS_WRITE, CENTRAL_GATT_TIMEOUT_MS);
    if (ble_gattc_write_flat(handle, s_central_owner_status_handle, &is_owner, sizeof(is_owner),
                             central_owner_status_write_cb, NULL) != 0)
        central_finish_proof_session(handle);
}

static int central_name_read_cb(uint16_t handle, const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (atomic_load(&s_central_reset_pending)) {
        central_disconnect(handle);
        return 0;
    }
    if (error->status == 0 && attr && attr->om) {
        size_t chunk_len = OS_MBUF_PKTLEN(attr->om);
        if (attr->offset != s_central_name_value_len || chunk_len == 0 ||
            chunk_len > BLE_PHONE_NAME_MAX_LEN - s_central_name_value_len ||
            ble_hs_mbuf_to_flat(attr->om, s_central_name_value + s_central_name_value_len,
                                chunk_len, NULL) != 0) {
            event_log_add("BLE central: некорректный фрагмент имени телефона");
            s_central_name_value_len = 0;
            return 0;
        }
        s_central_name_value_len += chunk_len;
        return 0;
    }
    struct ble_gap_conn_desc desc;
    bool secured = ble_gap_conn_find(handle, &desc) == 0 &&
                   desc.sec_state.encrypted && desc.sec_state.bonded &&
                   peer_has_bond(handle, NULL);
    bool valid = (error->status == BLE_HS_EDONE || error->status == 0) &&
                 secured && s_central_name_value_len > 0;
    for (size_t i = 0; valid && i < s_central_name_value_len; i++) {
        unsigned char ch = (unsigned char)s_central_name_value[i];
        if (ch < 0x20 || ch == 0x7f) valid = false;
        else if (ch == ',') s_central_name_value[i] = ' ';
    }
    if (valid) {
        s_central_name_value[s_central_name_value_len] = '\0';
        esp_err_t err = bond_name_save_once(&desc.peer_id_addr, s_central_name_value);
        event_log_add("BLE central: имя bonded телефона %s", err == ESP_OK ? "сохранено в NVS" : "не сохранено");
    } else {
        event_log_add("BLE central: имя телефона недоступно status=%d secure=%d",
                      error->status, secured);
    }
    central_send_owner_status(handle);
    return 0;
}

static int central_read_cb(uint16_t handle, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (atomic_load(&s_central_reset_pending)) {
        central_disconnect(handle);
        return 0;
    }
    static const char expected[] = "BLE_LOCK_V1";
    struct ble_gap_conn_desc desc;
    bool secured = ble_gap_conn_find(handle, &desc) == 0 &&
                   desc.sec_state.encrypted && desc.sec_state.bonded &&
                   peer_has_bond(handle, NULL);
    bool valid = error->status == 0 && attr && attr->om &&
                 OS_MBUF_PKTLEN(attr->om) == sizeof(expected) - 1 &&
                 os_mbuf_cmpf(attr->om, 0, expected, sizeof(expected) - 1) == 0;
    event_log_add("BLE central: proofRsp h=%u st=%d enc=%d bond=%d valid=%d",
                  (unsigned)handle, error->status, secured,
                  secured && desc.sec_state.bonded, valid);
    if (secured && valid) {
        app_config_t cfg;
        app_config_get(&cfg);
        if (s_central_rssi >= cfg.phone_rssi_threshold) {
            lock_feedback_t feedback;
            lock_control_get_feedback(&feedback);
            if (!feedback.requested_open) lock_request_open();
            lock_control_report_rssi(true);
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (!feedback.requested_open || now_ms - s_central_last_success_log_ms >= 30000) {
                s_central_last_success_log_ms = now_ms;
                event_log_add("BLE central: защищённый телефон %08" PRIX32 " сессия %08" PRIX32 " RSSI=%d, открыть/удержать",
                              s_central_device_id, s_central_session_id, s_central_rssi);
            }
        }
        char existing_name[BLE_PHONE_NAME_MAX_LEN + 1];
        if (s_central_name_handle && !bond_name_load(&desc.peer_id_addr, existing_name)) {
            s_central_name_value_len = 0;
            central_set_phase(CENTRAL_PHASE_NAME_READ, CENTRAL_GATT_TIMEOUT_MS);
            if (ble_gattc_read_long(handle, s_central_name_handle, 0,
                                    central_name_read_cb, NULL) == 0) return 0;
            event_log_add("BLE central: чтение имени телефона не запущено");
        }
        central_send_owner_status(handle);
        return 0;
    } else {
        event_log_add("BLE central: GATT proof отклонён status=%d secure=%d",
                      error->status, secured);
        bool registration_allowed = false;
        ble_phone_get_registration(&registration_allowed, NULL);
        if (secured && !registration_allowed && !s_central_registration_candidate &&
            error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INVALID_HANDLE)) {
            atomic_store(&s_central_suppressed_device_id, s_central_device_id);
            atomic_store(&s_central_suppressed_device_id_valid, true);
            event_log_add("BLE central: proof отклонён status=257; bond/owner сохранены, peer подавлен до BOOT");
            central_disconnect(handle);
            return 0;
        }
    }
    central_disconnect(handle);
    return 0;
}

static bool central_connection_secured(uint16_t handle)
{
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(handle, &desc) == 0 && desc.sec_state.encrypted &&
           desc.sec_state.bonded && peer_has_bond(handle, NULL);
}

static void central_apply_runtime_wifi_config(uint16_t handle, const uint8_t *phone_ip)
{
    esp_err_t ip_err = wifi_ota_set_runtime_phone_ip(phone_ip);
    if (ip_err != ESP_OK) {
        event_log_add("BLE provisioning: hotspot IPv4 rejected: %s", esp_err_to_name(ip_err));
        wifi_ota_set_runtime_phone_ip(NULL);
    } else if (phone_ip) {
        event_log_add("BLE provisioning: phone hotspot IPv4=%u.%u.%u.%u stored in RAM",
                      phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
    } else {
        event_log_add("BLE provisioning: hotspot IPv4 unavailable; using existing DHCP fallback");
    }

    esp_err_t wifi_err = wifi_ota_set_runtime_config(s_central_runtime_credentials,
                                                      s_central_runtime_credentials_len);
    if (wifi_err == ESP_OK) {
        event_log_add("BLE provisioning: protected hotspot settings accepted; ESP joins Wi-Fi by DHCP");
    } else {
        wifi_ota_set_runtime_phone_ip(NULL);
        event_log_add("BLE provisioning: runtime Wi-Fi config rejected: %s",
                      esp_err_to_name(wifi_err));
    }
    central_disconnect(handle);
}

static int central_hotspot_ip_read_cb(uint16_t handle, const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (atomic_load(&s_central_reset_pending)) {
        central_disconnect(handle);
        return 0;
    }
    uint8_t address[4] = {0};
    bool secured = central_connection_secured(handle);
    bool read_ok = error->status == 0 && attr && attr->om &&
                   OS_MBUF_PKTLEN(attr->om) == sizeof(address) &&
                   ble_hs_mbuf_to_flat(attr->om, address, sizeof(address), NULL) == 0;
    bool all_zero = address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0;
    bool all_broadcast = address[0] == 255 && address[1] == 255 &&
                         address[2] == 255 && address[3] == 255;
    bool valid_ip = read_ok && !all_zero && !all_broadcast && address[0] != 127 &&
                    address[0] < 224 && !(address[0] == 169 && address[1] == 254);
    if (!secured) {
        event_log_add("BLE provisioning: hotspot IPv4 denied on unsecured peer");
        central_disconnect(handle);
        return 0;
    }
    if (valid_ip) central_apply_runtime_wifi_config(handle, address);
    else {
        wifi_ota_set_runtime_phone_ip(NULL);
        event_log_add("BLE provisioning: hotspot IPv4 absent/invalid status=%d; continuing without it",
                      error->status);
        central_apply_runtime_wifi_config(handle, NULL);
    }
    return 0;
}

static int central_wifi_setup_read_cb(uint16_t handle, const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (atomic_load(&s_central_reset_pending)) {
        central_disconnect(handle);
        return 0;
    }
    if (error->status != 0 || !attr || !attr->om || !central_connection_secured(handle)) {
        event_log_add("BLE provisioning: Wi-Fi setup chunk read failed status=%d", error->status);
        central_disconnect(handle);
        return 0;
    }
    size_t chunk_len = OS_MBUF_PKTLEN(attr->om);
    size_t value_offset = s_central_wifi_setup_value_len;
    if (attr->offset != 0 || chunk_len == 0 || chunk_len > CENTRAL_WIFI_SETUP_CHUNK_SIZE ||
        value_offset + chunk_len > sizeof(s_central_wifi_setup_value) ||
        ble_hs_mbuf_to_flat(attr->om, s_central_wifi_setup_value + value_offset,
                            chunk_len, NULL) != 0) {
        event_log_add("BLE provisioning: invalid Wi-Fi setup chunk");
        central_disconnect(handle);
        return 0;
    }
    s_central_wifi_setup_value_len += chunk_len;

    if (value_offset == 0) {
        if (s_central_wifi_setup_value_len < 2) {
            event_log_add("BLE provisioning: Wi-Fi header is incomplete");
            central_disconnect(handle);
            return 0;
        }
        size_t ssid_len = s_central_wifi_setup_value[0];
        size_t password_len = s_central_wifi_setup_value[1];
        if (ssid_len == 0 || ssid_len > WIFI_OTA_SSID_MAX ||
            password_len > WIFI_OTA_PASSWORD_MAX) {
            event_log_add("BLE provisioning: invalid Wi-Fi credential lengths");
            central_disconnect(handle);
            return 0;
        }
        s_central_wifi_setup_expected_len = 2 + ssid_len + password_len;
    }

    size_t expected_len = s_central_wifi_setup_expected_len;
    size_t expected_chunk_len = expected_len > value_offset ?
        expected_len - value_offset : 0;
    if (expected_chunk_len > CENTRAL_WIFI_SETUP_CHUNK_SIZE)
        expected_chunk_len = CENTRAL_WIFI_SETUP_CHUNK_SIZE;
    if (chunk_len != expected_chunk_len) {
        event_log_add("BLE provisioning: Wi-Fi chunk length mismatch got=%u expected=%u",
                      (unsigned)chunk_len, (unsigned)expected_chunk_len);
        central_disconnect(handle);
        return 0;
    }
    if (s_central_wifi_setup_value_len < expected_len) {
        size_t next_index = s_central_wifi_setup_value_len / CENTRAL_WIFI_SETUP_CHUNK_SIZE;
        if (next_index >= CENTRAL_WIFI_SETUP_CHUNK_COUNT ||
            !s_central_wifi_setup_handles[next_index] ||
            ble_gattc_read(handle, s_central_wifi_setup_handles[next_index],
                           central_wifi_setup_read_cb, NULL) != 0) {
            event_log_add("BLE provisioning: next Wi-Fi chunk unavailable index=%u",
                          (unsigned)next_index);
            central_disconnect(handle);
        }
        return 0;
    }

    bool secured = central_connection_secured(handle);
    size_t value_len = s_central_wifi_setup_value_len;
    size_t ssid_len = s_central_wifi_setup_value[0];
    size_t password_len = s_central_wifi_setup_value[1];
    const uint8_t *nul_ptr = value_len > 2 ?
        memchr(s_central_wifi_setup_value + 2, 0, value_len - 2) : NULL;
    int nul_at = nul_ptr ? (int)(nul_ptr - (s_central_wifi_setup_value + 2)) : -1;
    bool contains_nul = nul_at >= 0;
    bool valid = secured && value_len == expected_len && !contains_nul;
    if (!valid) {
        event_log_add("BLE prov reject sec=%d len=%u ssid=%u pass=%u exp=%u nul_at=%d",
                      secured, (unsigned)value_len, (unsigned)ssid_len,
                      (unsigned)password_len, (unsigned)expected_len, nul_at);
        central_disconnect(handle);
        return 0;
    }

    memcpy(s_central_runtime_credentials, s_central_wifi_setup_value + 2, ssid_len);
    s_central_runtime_credentials[ssid_len] = '\n';
    memcpy(s_central_runtime_credentials + ssid_len + 1,
           s_central_wifi_setup_value + 2 + ssid_len, password_len);
    s_central_runtime_credentials_len = ssid_len + 1 + password_len;

    if (!s_central_hotspot_ip_handle) {
        event_log_add("BLE provisioning: HOTSPOT_IP characteristic missing; retaining Wi-Fi fallback");
        central_apply_runtime_wifi_config(handle, NULL);
        return 0;
    }
    central_set_phase(CENTRAL_PHASE_HOTSPOT_IP_READ, CENTRAL_GATT_TIMEOUT_MS);
    if (ble_gattc_read(handle, s_central_hotspot_ip_handle,
                       central_hotspot_ip_read_cb, NULL) == 0) return 0;
    event_log_add("BLE provisioning: HOTSPOT_IP read could not start; continuing without IP");
    central_apply_runtime_wifi_config(handle, NULL);
    return 0;
}

static int central_proof_for_provision_cb(uint16_t handle,
                                          const struct ble_gatt_error *error,
                                          struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    static const char expected[] = "BLE_LOCK_V1";
    bool valid = error->status == 0 && attr && attr->om &&
                 OS_MBUF_PKTLEN(attr->om) == sizeof(expected) - 1 &&
                 os_mbuf_cmpf(attr->om, 0, expected, sizeof(expected) - 1) == 0;
    if (atomic_load(&s_central_reset_pending) || !central_connection_secured(handle) || !valid) {
        event_log_add("BLE provisioning: encrypted proof failed status=%d", error->status);
        central_disconnect(handle);
        return 0;
    }
    if (central_try_send_web_ui_ip(handle)) return 0;
    central_set_phase(CENTRAL_PHASE_WIFI_SETUP_READ, CENTRAL_GATT_TIMEOUT_MS);
    s_central_wifi_setup_value_len = 0;
    s_central_wifi_setup_expected_len = 0;
    if (ble_gattc_read(handle, s_central_wifi_setup_handles[0],
                       central_wifi_setup_read_cb, NULL) == 0) return 0;
    event_log_add("BLE provisioning: Wi-Fi setup read could not start");
    central_disconnect(handle);
    return 0;
}

static int central_web_ui_ip_proof_cb(uint16_t handle, const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    static const char expected[] = "BLE_LOCK_V1";
    bool valid = error->status == 0 && attr && attr->om &&
                 OS_MBUF_PKTLEN(attr->om) == sizeof(expected) - 1 &&
                 os_mbuf_cmpf(attr->om, 0, expected, sizeof(expected) - 1) == 0;
    if (atomic_load(&s_central_reset_pending) || !central_connection_secured(handle) || !valid) {
        event_log_add("BLE Web UI request: encrypted proof failed status=%d", error->status);
        central_disconnect(handle);
        return 0;
    }
    if (!central_try_send_web_ui_ip(handle)) {
        event_log_add("BLE Web UI request: owner IPv4 не готов");
        central_disconnect(handle);
    }
    return 0;
}

static int central_chr_cb(uint16_t handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (atomic_load(&s_central_reset_pending)) {
        central_disconnect(handle);
        return 0;
    }
    if (error->status == 0 && chr) {
        if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_proof_uuid.u) == 0)
            s_central_proof_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_owner_status_uuid.u) == 0)
            s_central_owner_status_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_hotspot_ip_uuid.u) == 0)
            s_central_hotspot_ip_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_name_uuid.u) == 0)
            s_central_name_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_web_ui_ip_uuid.u) == 0)
            s_central_web_ui_ip_handle = chr->val_handle;
        else {
            for (size_t i = 0; i < CENTRAL_WIFI_SETUP_CHUNK_COUNT; ++i) {
                if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_wifi_setup_chunk_uuids[i].u) == 0) {
                    s_central_wifi_setup_handles[i] = chr->val_handle;
                    break;
                }
            }
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE && s_central_wifi_provision_candidate) {
        if (!s_central_proof_handle || !s_central_wifi_setup_handles[0]) {
            event_log_add("BLE provisioning: proof or Wi-Fi setup characteristic missing");
            central_disconnect(handle);
            return 0;
        }
        central_set_phase(CENTRAL_PHASE_PROOF_READ, CENTRAL_GATT_TIMEOUT_MS);
        if (ble_gattc_read(handle, s_central_proof_handle,
                           central_proof_for_provision_cb, NULL) == 0) return 0;
        event_log_add("BLE provisioning: proof read could not start");
        central_disconnect(handle);
        return 0;
    }
    if (error->status == BLE_HS_EDONE && s_central_webui_ip_candidate) {
        if (!s_central_proof_handle) {
            event_log_add("BLE Web UI request: proof characteristic missing");
            central_disconnect(handle);
            return 0;
        }
        central_set_phase(CENTRAL_PHASE_PROOF_READ, CENTRAL_GATT_TIMEOUT_MS);
        if (ble_gattc_read(handle, s_central_proof_handle,
                           central_web_ui_ip_proof_cb, NULL) == 0) return 0;
        event_log_add("BLE Web UI request: proof read could not start");
        central_disconnect(handle);
        return 0;
    }
    if (error->status == BLE_HS_EDONE && s_central_proof_handle) {
        central_set_phase(CENTRAL_PHASE_PROOF_READ, CENTRAL_GATT_TIMEOUT_MS);
        event_log_add("BLE central: proof ATT request conn=%u attr=%u",
                      (unsigned)handle, (unsigned)s_central_proof_handle);
        if (ble_gattc_read(handle, s_central_proof_handle, central_read_cb, NULL) == 0) return 0;
        event_log_add("BLE central: proof ATT request start failed conn=%u",
                      (unsigned)handle);
    }
    event_log_add("BLE central: proof characteristic отсутствует status=%d", error->status);
    central_disconnect(handle);
    return 0;
}

static int central_svc_cb(uint16_t handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;
    if (atomic_load(&s_central_reset_pending)) {
        central_disconnect(handle);
        return 0;
    }
    if (error->status == 0 && service) {
        s_central_svc_start = service->start_handle;
        s_central_svc_end = service->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE && s_central_svc_start) {
        s_central_proof_handle = 0;
        s_central_owner_status_handle = 0;
        memset(s_central_wifi_setup_handles, 0, sizeof(s_central_wifi_setup_handles));
        s_central_hotspot_ip_handle = 0;
        s_central_name_handle = 0;
        s_central_web_ui_ip_handle = 0;
        s_central_runtime_credentials_len = 0;
        central_set_phase(CENTRAL_PHASE_CHARACTERISTIC_DISCOVERY, CENTRAL_GATT_TIMEOUT_MS);
        if (ble_gattc_disc_all_chrs(handle, s_central_svc_start, s_central_svc_end,
                                    central_chr_cb, NULL) == 0) return 0;
    }
    event_log_add("BLE central: beacon service отсутствует status=%d", error->status);
    central_disconnect(handle);
    return 0;
}

static void central_connect_target(void);

static int central_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc desc;
        int desc_rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        char peer[18] = "unknown";
        char identity[18] = "unknown";
        if (desc_rc == 0) {
            address_to_string(&desc.peer_ota_addr, peer);
            address_to_string(&desc.peer_id_addr, identity);
        }
        event_log_add("BLE central: ENC h=%u st=%d rc=%d e=%d a=%d b=%d ks=%u",
                      (unsigned)event->enc_change.conn_handle, event->enc_change.status,
                      desc_rc, desc_rc == 0 && desc.sec_state.encrypted,
                      desc_rc == 0 && desc.sec_state.authenticated,
                      desc_rc == 0 && desc.sec_state.bonded,
                      desc_rc == 0 ? (unsigned)desc.sec_state.key_size : 0);
        event_log_add("BLE central: ADDR ota=%s/%u id=%s/%u", peer,
                      desc_rc == 0 ? (unsigned)desc.peer_ota_addr.type : 0,
                      identity, desc_rc == 0 ? (unsigned)desc.peer_id_addr.type : 0);
    } else if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING) {
        bool registration_allowed = false;
        ble_phone_get_registration(&registration_allowed, NULL);
        event_log_add("BLE central: REPEAT h=%u reg=%d",
                      (unsigned)event->repeat_pairing.conn_handle, registration_allowed);
    } else if (event->type == BLE_GAP_EVENT_PASSKEY_ACTION) {
        bool registration_allowed = false;
        ble_phone_get_registration(&registration_allowed, NULL);
        event_log_add("BLE central: PASSKEY h=%u act=%u reg=%d",
                      (unsigned)event->passkey.conn_handle,
                      (unsigned)event->passkey.params.action, registration_allowed);
    }
    if (atomic_load(&s_central_reset_pending) && event->type != BLE_GAP_EVENT_DISCONNECT) {
        if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0) {
            s_central_handle = event->connect.conn_handle;
            ble_gap_terminate(s_central_handle, BLE_ERR_REM_USER_CONN_TERM);
        } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE && event->enc_change.status == 0) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
                ble_store_util_delete_peer(&desc.peer_id_addr);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            const struct ble_gap_disc_desc *disc = &event->disc;
            struct ble_hs_adv_fields fields;
            if (s_central_target_pending || s_central_connecting ||
                ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) return 0;
            const uint8_t *m = fields.mfg_data;
            if (!m || fields.mfg_data_len != 15 || m[0] != (PHONE_BEACON_COMPANY_ID & 0xff) ||
                m[1] != (PHONE_BEACON_COMPANY_ID >> 8) || memcmp(m + 2, "LOCK", 4) ||
                (m[6] != 1 && m[6] != 0x81 && m[6] != 0x82)) return 0;
            uint32_t device_id = ((uint32_t)m[7] << 24) | ((uint32_t)m[8] << 16) |
                                 ((uint32_t)m[9] << 8) | m[10];
            bool webui_or_provisioning_beacon = m[6] == 0x81 || m[6] == 0x82;
            bool registration_allowed = false;
            ble_phone_get_registration(&registration_allowed, NULL);
            if (registration_allowed) {
                atomic_store(&s_central_suppressed_device_id_valid, false);
            } else if (atomic_load(&s_central_suppressed_device_id_valid) &&
                       atomic_load(&s_central_suppressed_device_id) == device_id &&
                       !webui_or_provisioning_beacon) {
                return 0;
            }
            app_config_t cfg;
            app_config_get(&cfg);
            if (!webui_or_provisioning_beacon && disc->rssi < cfg.phone_rssi_threshold) {
                portENTER_CRITICAL(&s_central_low_rssi_lock);
                s_central_low_rssi_count++;
                if (disc->rssi < s_central_low_rssi_min)
                    s_central_low_rssi_min = disc->rssi;
                if (disc->rssi > s_central_low_rssi_max)
                    s_central_low_rssi_max = disc->rssi;
                portEXIT_CRITICAL(&s_central_low_rssi_lock);
                return 0;
            }
            s_central_target = disc->addr;
            s_central_device_id = device_id;
            s_central_session_id = ((uint32_t)m[11] << 24) | ((uint32_t)m[12] << 16) |
                                   ((uint32_t)m[13] << 8) | m[14];
            s_central_rssi = disc->rssi;
            s_central_wifi_provision_candidate = m[6] == 0x81;
            s_central_webui_ip_candidate = m[6] == 0x82;
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (s_central_session_id != s_central_logged_session_id ||
                now_ms - s_central_last_beacon_log_ms >= 30000) {
                s_central_logged_session_id = s_central_session_id;
                s_central_last_beacon_log_ms = now_ms;
                event_log_add("BLE central: маяк LOCK %08" PRIX32 " session=%08" PRIX32 " mode=0x%02X RSSI=%d threshold=%d",
                              s_central_device_id, s_central_session_id, m[6],
                              s_central_rssi, cfg.phone_rssi_threshold);
            }
            s_central_target_pending = true;
            int cancel_rc = ble_gap_disc_cancel();
            if (cancel_rc == 0) {
                s_central_scanning = false;
                central_connect_target();
            } else {
                event_log_add("BLE central: scan cancel error=%d", cancel_rc);
                central_schedule_retry();
            }
            return 0;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_central_scanning = false;
            central_schedule_retry();
            return 0;
        case BLE_GAP_EVENT_CONNECT:
            s_central_connecting = false;
            if (event->connect.status != 0) {
                event_log_add("BLE central: connect failed=%d", event->connect.status);
                central_schedule_retry();
                return 0;
            }
            s_central_handle = event->connect.conn_handle;
            s_central_existing_bond_before_connect = false;
            s_central_existing_ltk_before_connect = false;
            s_central_existing_ltk_restored = false;
            if (atomic_load(&s_central_phase) != CENTRAL_PHASE_CONNECTING) {
                event_log_add("BLE central: поздний connect отклонён после отмены/timeout");
                central_disconnect(s_central_handle);
                return 0;
            }
            {
                int bond_count = 0;
                bool bonded = peer_has_bond(s_central_handle, &bond_count);
                if (bonded) {
                    struct ble_gap_conn_desc desc;
                    struct ble_store_key_sec key = {0};
                    struct ble_store_value_sec value = {0};
                    int key_rc = ble_gap_conn_find(s_central_handle, &desc);
                    if (key_rc == 0) {
                        key.peer_addr = desc.peer_id_addr;
                        key_rc = ble_store_read_peer_sec(&key, &value);
                    }
                    s_central_existing_ltk_before_connect =
                        key_rc == 0 && value.ltk_present;
                    event_log_add("BLE central: PEER_SEC rc=%d LTK=%d SC=%d AUTH=%d",
                                  key_rc, key_rc == 0 && value.ltk_present,
                                  key_rc == 0 && value.sc,
                                  key_rc == 0 && value.authenticated);
                }
                s_central_existing_bond_before_connect = bonded;
                bool registration_allowed = false;
                ble_phone_get_registration(&registration_allowed, NULL);
                if (!bonded && bond_count >= MAX_BONDED_PHONES) {
                    event_log_add("BLE central: достигнут лимит %d bonds, новый телефон отклонён",
                                  MAX_BONDED_PHONES);
                    central_disconnect(s_central_handle);
                    return 0;
                }
                if (!bonded && !registration_allowed) {
                    atomic_store(&s_central_suppressed_device_id, s_central_device_id);
                    atomic_store(&s_central_suppressed_device_id_valid, true);
                    event_log_add("BLE central: неизвестный peer отклонён вне окна регистрации и подавлен до BOOT window");
                    central_disconnect(s_central_handle);
                    return 0;
                }
                s_central_registration_candidate = !bonded && registration_allowed;
                if (s_central_registration_candidate) {
                    event_log_add("BLE central: новый peer принят в окне регистрации; запускаю SMP bonding");
                }
            }
            uint32_t security_timeout_ms = CENTRAL_SECURITY_TIMEOUT_MS;
            if (s_central_registration_candidate) {
                bool registration_allowed = false;
                uint32_t registration_remaining_s = 0;
                ble_phone_get_registration(&registration_allowed, &registration_remaining_s);
                if (!registration_allowed) {
                    event_log_add("BLE central: pairing window истекло до SMP");
                    central_disconnect(s_central_handle);
                    return 0;
                }
                if (registration_remaining_s != UINT32_MAX &&
                    registration_remaining_s * 1000U < CENTRAL_REGISTRATION_SECURITY_TIMEOUT_MS) {
                    security_timeout_ms = registration_remaining_s * 1000U;
                } else {
                    security_timeout_ms = CENTRAL_REGISTRATION_SECURITY_TIMEOUT_MS;
                }
                event_log_add("BLE central: ожидаю подтверждение pairing Android до %lu мс",
                              (unsigned long)security_timeout_ms);
            }
            central_set_phase(CENTRAL_PHASE_SECURITY, security_timeout_ms);
            int security_rc = ble_gap_security_initiate(s_central_handle);
            const char *security_path = s_central_registration_candidate ? "NEW_PAIRING" :
                (s_central_existing_ltk_before_connect ? "RESTORE_EXISTING" : "PAIR_OR_RESTORE");
            event_log_add("BLE central: SEC_START h=%u path=%s bond=%d cand=%d rc=%d t=%lu",
                          (unsigned)s_central_handle,
                          security_path,
                          peer_has_bond(s_central_handle, NULL),
                          s_central_registration_candidate, security_rc,
                          (unsigned long)security_timeout_ms);
            if (security_rc != 0) {
                central_disconnect(s_central_handle);
            }
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE: {
            struct ble_gap_conn_desc desc;
            if (event->enc_change.status != 0 ||
                ble_gap_conn_find(event->enc_change.conn_handle, &desc) != 0 ||
                !desc.sec_state.encrypted || !desc.sec_state.bonded ||
                !peer_has_bond(event->enc_change.conn_handle, NULL)) {
                event_log_add("BLE central: encryption/bond rejected after ENC_CHANGE conn=%u",
                              (unsigned)event->enc_change.conn_handle);
                central_disconnect(event->enc_change.conn_handle);
                return 0;
            }
            if (s_central_existing_bond_before_connect &&
                s_central_existing_ltk_before_connect) {
                s_central_existing_ltk_restored = true;
                event_log_add("BLE central: existing LTK restore confirmed auth=%d",
                              desc.sec_state.authenticated);
            }
            if (s_central_registration_candidate) {
                bool registration_allowed = false;
                ble_phone_get_registration(&registration_allowed, NULL);
                if (!registration_allowed || !peer_has_bond(event->enc_change.conn_handle, NULL)) {
                    event_log_add("BLE central: pairing завершился вне окна или bond не сохранён");
                    ble_store_util_delete_peer(&desc.peer_id_addr);
                    s_central_registration_candidate = false;
                    central_disconnect(event->enc_change.conn_handle);
                    return 0;
                }
                ble_addr_t owner;
                esp_err_t owner_err = owner_load(&owner);
                if (owner_err == ESP_ERR_NVS_NOT_FOUND) {
                    owner_err = owner_save(&desc.peer_id_addr);
                    if (owner_err == ESP_OK) {
                        char address[18];
                        address_to_string(&desc.peer_id_addr, address);
                        event_log_add("BLE owner назначен при первом pairing: %s", address);
                    }
                }
                if (owner_err != ESP_OK) {
                    event_log_add("BLE central: bond сохранён, но owner NVS недоступен: %s",
                                  esp_err_to_name(owner_err));
                    ble_store_util_delete_peer(&desc.peer_id_addr);
                    central_disconnect(event->enc_change.conn_handle);
                    return 0;
                }
                portENTER_CRITICAL(&s_registration_lock);
                s_registration_deadline_us = 0;
                portEXIT_CRITICAL(&s_registration_lock);
                s_central_registration_candidate = false;
                s_central_new_bond_settling = true;
                ap_mode_set_configuration(false);
                led_set_configuration(false);
                event_log_add("BLE central: новый bond проверен; pairing и режим настройки закрыты без reboot");
                ble_phone_start_advertising();
            }
            if (s_central_wifi_provision_candidate) {
                event_log_add("BLE central: Wi-Fi provisioning marker detected; protected credentials read only");
            }
            s_central_svc_start = 0;
            central_set_phase(CENTRAL_PHASE_SERVICE_DISCOVERY, CENTRAL_GATT_TIMEOUT_MS);
            if (ble_gattc_disc_svc_by_uuid(event->enc_change.conn_handle,
                                           &s_phone_beacon_service_uuid.u, central_svc_cb, NULL) != 0) {
                central_disconnect(event->enc_change.conn_handle);
            }
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT:
            if (event->disconnect.reason != BLE_HS_HCI_ERR(BLE_ERR_REM_USER_CONN_TERM)) {
                event_log_add("BLE central: неожиданный disconnect reason=%d",
                              event->disconnect.reason);
            }
            central_schedule_retry();
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            bool registration_allowed = false;
            ble_phone_get_registration(&registration_allowed, NULL);
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) != 0) {
                event_log_add("BLE central: repeat pairing без connection descriptor; отклоняю");
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            char address[18];
            address_to_string(&desc.peer_id_addr, address);
            if (registration_allowed) {
                if (ble_phone_delete_bond(address) == ESP_OK) {
                    s_central_registration_candidate = true;
                    event_log_add("BLE central: устаревший bond удалён в registration window; SMP повторяется");
                    return BLE_GAP_REPEAT_PAIRING_RETRY;
                }
                event_log_add("BLE central: не удалось удалить устаревший bond %s; repeat pairing отклонён",
                              address);
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            central_reject_stale_pairing(event->repeat_pairing.conn_handle,
                                         "повторный SMP вне активной регистрации");
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            bool registration_allowed = false;
            ble_phone_get_registration(&registration_allowed, NULL);
            if (!registration_allowed && !s_central_registration_candidate) {
                event_log_add("BLE central: повторный pairing action=%u отклонён вне BOOT window",
                              event->passkey.params.action);
                central_reject_stale_pairing(event->passkey.conn_handle,
                                             "pairing action запрошен вне BOOT window");
                return 0;
            }
            event_log_add("BLE central: pairing action=%u ожидает системное подтверждение Android",
                          event->passkey.params.action);
            return 0;
        }
        default:
            return 0;
    }
}

static void central_connect_target(void)
{
    if (!s_central_target_pending || atomic_load(&s_central_reset_pending)) return;
    s_central_target_pending = false;
    struct ble_gap_conn_params params = {
        .scan_itvl = 32, .scan_window = 32, .itvl_min = 12, .itvl_max = 24,
        .latency = 0, .supervision_timeout = 300,
    };
    s_central_connecting = true;
    central_set_phase(CENTRAL_PHASE_CONNECTING, CENTRAL_CONNECT_TIMEOUT_MS);
    int rc = ble_gap_connect(s_addr_type, &s_central_target, CENTRAL_CONNECT_TIMEOUT_MS,
                             &params, central_gap_event, NULL);
    if (rc != 0) {
        event_log_add("BLE central: connect request error=%d", rc);
        central_schedule_retry();
    }
}

static void central_scan_work(struct ble_npl_event *event)
{
    (void)event;
    if (atomic_load(&s_central_reset_pending) || !s_advertising_ready || s_central_scanning || s_central_connecting ||
        s_central_handle != BLE_HS_CONN_HANDLE_NONE) return;
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return;
    struct ble_gap_disc_params params = {
        .itvl = 160, .window = 160, .passive = 1, .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, central_gap_event, NULL);
    if (rc == 0) {
        s_central_scanning = true;
        static bool first_scan = true;
        if (first_scan) {
            first_scan = false;
            event_log_add("BLE central: passive scan запущен interval=100ms window=100ms");
        }
    }
    else {
        s_central_retry_ms = esp_timer_get_time() / 1000 + PHONE_BEACON_RETRY_MS;
        event_log_add("BLE central: scan start error=%d", rc);
    }
}

static void central_timeout_work(struct ble_npl_event *event)
{
    (void)event;
    atomic_store(&s_central_timeout_pending, false);
    if (atomic_load(&s_central_reset_pending)) return;
    int64_t deadline = atomic_load(&s_central_phase_deadline_ms);
    int phase = atomic_load(&s_central_phase);
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (phase == CENTRAL_PHASE_IDLE || deadline == 0 || now_ms < deadline) return;

    if (phase == CENTRAL_PHASE_NEW_BOND_SETTLE) {
        event_log_add("BLE central: выдержка первого bond завершена; разрываю соединение");
        central_disconnect(s_central_handle);
        return;
    }
    event_log_add("BLE central: timeout phase=%d, возвращаюсь к scan", phase);
    central_set_phase(CENTRAL_PHASE_IDLE, 0);
    s_central_target_pending = false;
    s_central_registration_candidate = false;
    if (phase == CENTRAL_PHASE_CONNECTING) {
        s_central_connecting = false;
        ble_gap_conn_cancel();
        s_central_retry_ms = now_ms + PHONE_BEACON_RETRY_MS;
        return;
    }
    if (s_central_handle != BLE_HS_CONN_HANDLE_NONE) {
        central_disconnect(s_central_handle);
    } else {
        central_schedule_retry();
    }
}

static int advertising_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ble_phone_apply_tx_power();
                event_log_add("BLE GATT: телефон подключён");
                // A phone can turn its Bluetooth radio off without sending a
                // link-layer disconnect. Keep the controller supervision
                // timeout short so stale slots are restored promptly.
                struct ble_gap_upd_params connection_params = {
                    .itvl_min = 12,                 // 15 ms
                    .itvl_max = 24,                 // 30 ms
                    .latency = 0,
                    .supervision_timeout = BLE_CONNECTION_SUPERVISION_TIMEOUT,
                    .min_ce_len = 0,
                    .max_ce_len = 0,
                };
                int update_rc = ble_gap_update_params(event->connect.conn_handle,
                                                       &connection_params);
                event_log_add("BLE: параметры соединения, supervision=3 с rc=%d",
                              update_rc);
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) != 0) {
                    ble_gap_terminate(event->connect.conn_handle,
                                      BLE_ERR_REM_USER_CONN_TERM);
                    ble_phone_start_advertising();
                    return 0;
                }
                bool registration_allowed;
                ble_phone_get_registration(&registration_allowed, NULL);
                int bond_count = 0;
                bool bonded_peer = peer_has_bond(event->connect.conn_handle, &bond_count);
                ble_addr_t owner;
                bool is_owner = owner_load(&owner) == ESP_OK &&
                                address_equal(&owner, &desc.peer_id_addr);
                if (bonded_peer && registration_allowed) {
                    event_log_add("BLE registration: старый bonded peer отклонён");
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    ble_phone_start_advertising();
                    return 0;
                }
                if (!bonded_peer && !registration_allowed) {
                    event_log_add("BLE security: новый телефон отклонён, регистрация закрыта");
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    ble_phone_start_advertising();
                    return 0;
                }
                if (!bonded_peer && bond_count >= MAX_BONDED_PHONES) {
                    event_log_add("BLE security: лимит 20 bonds, удалите телефон через Web UI");
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    ble_phone_start_advertising();
                    return 0;
                }
                if (!bonded_peer) {
                    event_log_add("BLE registration: новый peer начал pairing");
                }
                int slot_index;
                portENTER_CRITICAL(&s_connection_lock);
                if (is_owner) {
                    slot_index = s_slots[0].conn_handle == BLE_HS_CONN_HANDLE_NONE ? 0 : -1;
                } else {
                    slot_index = -1;
                    for (int i = 1; i < BLE_TOTAL_SLOTS; i++) {
                        if (s_slots[i].conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                            slot_index = i;
                            break;
                        }
                    }
                }
                if (slot_index >= 0) {
                    ble_slot_t *slot = &s_slots[slot_index];
                    clear_slot_locked(slot);
                    slot->conn_handle = event->connect.conn_handle;
                    slot->is_owner_slot = is_owner;
                    slot->peer_was_bonded = bonded_peer;
                    slot->peer_registration_candidate = registration_allowed && !bonded_peer;
                }
                portEXIT_CRITICAL(&s_connection_lock);
                if (slot_index < 0) {
                    if (is_owner) {
                        event_log_add("BLE: слот владельца уже занят; повторное owner-соединение отклонено");
                    } else {
                        event_log_add("BLE: гостевые слоты заняты (%d/%d), подключение отклонено",
                                      BLE_GUEST_SLOTS, BLE_GUEST_SLOTS);
                    }
                    ble_gap_terminate(event->connect.conn_handle,
                                      BLE_ERR_REM_USER_CONN_TERM);
                    ble_phone_start_advertising();
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
                ble_phone_start_advertising();
            } else {
                ble_phone_start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT: {
            event_log_add("BLE disconnect h=%u reason=%d",
                          event->disconnect.conn.conn_handle, event->disconnect.reason);
            bool report_disconnected = false;
            portENTER_CRITICAL(&s_connection_lock);
            int disconnected_slot = slot_index_for_handle_locked(event->disconnect.conn.conn_handle);
            if (disconnected_slot >= 0) {
                bool was_secured = s_slots[disconnected_slot].encrypted &&
                                   s_slots[disconnected_slot].bonded;
                clear_slot_locked(&s_slots[disconnected_slot]);
                report_disconnected = was_secured && !any_secured_slot_locked();
            }
            portEXIT_CRITICAL(&s_connection_lock);
            if (report_disconnected) lock_control_report_disconnected();
            ble_phone_start_advertising();
            return 0;
        }
        case BLE_GAP_EVENT_ENC_CHANGE: {
            struct ble_gap_conn_desc desc;
            if (event->enc_change.status == 0 &&
                ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                event_log_add("BLE security: encrypted=%u bonded=%u key=%u",
                              desc.sec_state.encrypted, desc.sec_state.bonded,
                              desc.sec_state.key_size);
                bool registration_candidate;
                bool report_disconnected = false;
                portENTER_CRITICAL(&s_connection_lock);
                int slot_index = slot_index_for_handle_locked(event->enc_change.conn_handle);
                if (slot_index >= 0) {
                    bool was_secured = s_slots[slot_index].encrypted &&
                                       s_slots[slot_index].bonded;
                    s_slots[slot_index].encrypted = desc.sec_state.encrypted;
                    s_slots[slot_index].bonded = desc.sec_state.bonded;
                    registration_candidate = s_slots[slot_index].peer_registration_candidate &&
                                             !s_slots[slot_index].peer_was_bonded;
                    report_disconnected = was_secured &&
                        !(desc.sec_state.encrypted && desc.sec_state.bonded) &&
                        !any_secured_slot_locked();
                } else {
                    registration_candidate = false;
                }
                portEXIT_CRITICAL(&s_connection_lock);
                if (report_disconnected) lock_control_report_disconnected();

                bool registration_allowed;
                ble_phone_get_registration(&registration_allowed, NULL);
                bool new_bond_saved = registration_candidate && registration_allowed &&
                                      desc.sec_state.encrypted && desc.sec_state.bonded &&
                                      peer_has_bond(event->enc_change.conn_handle, NULL);
                if (new_bond_saved) {
                    portENTER_CRITICAL(&s_registration_lock);
                    s_registration_deadline_us = 0;
                    portEXIT_CRITICAL(&s_registration_lock);
                    portENTER_CRITICAL(&s_connection_lock);
                    int slot_index = slot_index_for_handle_locked(event->enc_change.conn_handle);
                    if (slot_index >= 0) {
                        s_slots[slot_index].peer_was_bonded = true;
                        s_slots[slot_index].peer_registration_candidate = false;
                    }
                    portEXIT_CRITICAL(&s_connection_lock);
                    event_log_add("BLE registration: новый bond успешно создан и сохранён");
                    event_log_add("BLE registration: окно закрыто после успешного bond");
                    ble_addr_t owner;
                    esp_err_t owner_err = owner_load(&owner);
                    if (owner_err == ESP_ERR_NVS_NOT_FOUND) {
                        if (owner_save(&desc.peer_id_addr) == ESP_OK) {
                            char address[18];
                            address_to_string(&desc.peer_id_addr, address);
                            event_log_add("BLE owner назначен при первом pairing: %s",
                                          address);
                            bool promoted = false;
                            portENTER_CRITICAL(&s_connection_lock);
                            int slot_index = slot_index_for_handle_locked(
                                event->enc_change.conn_handle);
                            if (slot_index == 0) {
                                s_slots[0].is_owner_slot = true;
                                promoted = true;
                            } else if (slot_index > 0 &&
                                       s_slots[0].conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                                s_slots[0] = s_slots[slot_index];
                                s_slots[0].is_owner_slot = true;
                                clear_slot_locked(&s_slots[slot_index]);
                                promoted = true;
                            }
                            portEXIT_CRITICAL(&s_connection_lock);
                            if (promoted) {
                                event_log_add("BLE: активная сессия владельца перенесена в слот 0");
                            } else {
                                event_log_add("BLE: owner сохранён, но слот 0 не удалось назначить");
                            }
                        } else {
                            event_log_add("BLE owner: ошибка сохранения владельца");
                        }
                    }
                    event_log_add("BLE registration: bond сохранён без reboot");
                } else if (registration_candidate && !registration_allowed) {
                    event_log_add("BLE registration: pairing не завершён до закрытия окна");
                    ble_gap_terminate(event->enc_change.conn_handle,
                                      BLE_ERR_REM_USER_CONN_TERM);
                } else if (registration_candidate &&
                           (!desc.sec_state.encrypted || !desc.sec_state.bonded)) {
                    event_log_add("BLE registration: pairing/bonding завершился ошибкой");
                    ble_gap_terminate(event->enc_change.conn_handle,
                                      BLE_ERR_REM_USER_CONN_TERM);
                } else if (registration_candidate) {
                    event_log_add("BLE registration: bonding не сохранён, reboot отменён");
                    ble_gap_terminate(event->enc_change.conn_handle,
                                      BLE_ERR_REM_USER_CONN_TERM);
                }
            } else {
                bool registration_candidate;
                portENTER_CRITICAL(&s_connection_lock);
                int slot_index = slot_index_for_handle_locked(event->enc_change.conn_handle);
                registration_candidate = slot_index >= 0 &&
                    s_slots[slot_index].peer_registration_candidate;
                portEXIT_CRITICAL(&s_connection_lock);
                event_log_add("BLE security failed: %d", event->enc_change.status);
                if (registration_candidate) {
                    event_log_add("BLE registration: pairing/bonding завершился ошибкой: %d",
                                  event->enc_change.status);
                    ble_gap_terminate(event->enc_change.conn_handle,
                                      BLE_ERR_REM_USER_CONN_TERM);
                }
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
    int64_t advertising_policy_check_ms = 0;
    int64_t advertising_retry_ms = 0;
    int64_t slot_diagnostic_ms = 0;
    int64_t central_rssi_diagnostic_ms = 0;
    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (s_advertising_ready && now_ms >= s_central_retry_ms) {
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_central_event);
            s_central_retry_ms = now_ms + PHONE_BEACON_RETRY_MS;
        }
        bool has_free_slot = any_slot_free();
        if (now_ms - advertising_policy_check_ms >= 1000) {
            advertising_policy_check_ms = now_ms;
            bool registration_allowed;
            ble_phone_get_registration(&registration_allowed, NULL);
            led_set_configuration(registration_allowed || ap_mode_is_configuration());
            if (has_free_slot && !ble_gap_adv_active()) {
                if (now_ms >= advertising_retry_ms) {
                    event_log_add("BLE watchdog: реклама остановлена — перезапуск");
                    (void)ble_phone_start_advertising();
                    advertising_retry_ms = now_ms + 10000;
                }
            } else if (has_free_slot && ble_gap_adv_active()) {
                advertising_retry_ms = 0;
                ble_addr_t peers[MAX_BONDED_PHONES];
                int bond_count = ble_phone_get_bonded_peers(peers, MAX_BONDED_PHONES);
                bool should_restrict = !registration_allowed && bond_count > 0 &&
                                       bond_count <= CONFIG_BT_NIMBLE_WHITELIST_SIZE;
                if (bond_count >= 0 && should_restrict != s_advertising_restricted) {
                    event_log_add("BLE: окно регистрации %s, обновление списка допуска",
                                  registration_allowed ? "открыто" : "закрыто");
                    ble_phone_start_advertising();
                }
            } else if (!has_free_slot && ble_gap_adv_active()) {
                // Reuse the host-task work item to stop connectable advertising
                // once all six table entries are occupied.
                ble_phone_start_advertising();
            }
        }

        if (now_ms - central_rssi_diagnostic_ms >= 10000) {
            central_rssi_diagnostic_ms = now_ms;
            uint32_t low_count;
            int low_min, low_max;
            portENTER_CRITICAL(&s_central_low_rssi_lock);
            low_count = s_central_low_rssi_count;
            low_min = s_central_low_rssi_min;
            low_max = s_central_low_rssi_max;
            s_central_low_rssi_count = 0;
            s_central_low_rssi_min = INT8_MAX;
            s_central_low_rssi_max = INT8_MIN;
            portEXIT_CRITICAL(&s_central_low_rssi_lock);
            if (low_count > 0) {
                app_config_t cfg;
                app_config_get(&cfg);
                event_log_add("BLE LOCK RSSI low n=%" PRIu32
                              " min=%d max=%d threshold=%d/10s",
                              low_count, low_min, low_max, cfg.phone_rssi_threshold);
            }
        }

        bool have_valid_secured_rssi = false;
        bool any_sufficient = false;
        bool disconnected_last_secured = false;
        if (now_ms - slot_diagnostic_ms >= 10000) {
            slot_diagnostic_ms = now_ms;
            for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
                uint16_t handle;
                bool owner_slot, rssi_valid;
                int8_t last_rssi;
                portENTER_CRITICAL(&s_connection_lock);
                handle = s_slots[i].conn_handle;
                owner_slot = s_slots[i].is_owner_slot;
                rssi_valid = s_slots[i].rssi_valid;
                last_rssi = s_slots[i].last_rssi;
                portEXIT_CRITICAL(&s_connection_lock);
                ESP_LOGI(BLE_PHONE_TAG,
                         "slot[%d] owner=%u handle=%u rssi=%d valid=%u",
                         i, (unsigned)owner_slot, (unsigned)handle, last_rssi,
                         (unsigned)rssi_valid);
            }
        }
        for (int i = 0; i < BLE_TOTAL_SLOTS; i++) {
            uint16_t conn_handle;
            bool registration_candidate;
            portENTER_CRITICAL(&s_connection_lock);
            conn_handle = s_slots[i].conn_handle;
            registration_candidate = s_slots[i].peer_registration_candidate;
            portEXIT_CRITICAL(&s_connection_lock);
            if (conn_handle == BLE_HS_CONN_HANDLE_NONE) continue;

            if (registration_candidate) {
                bool registration_allowed;
                ble_phone_get_registration(&registration_allowed, NULL);
                if (!registration_allowed) {
                    portENTER_CRITICAL(&s_connection_lock);
                    int slot_index = slot_index_for_handle_locked(conn_handle);
                    if (slot_index >= 0) s_slots[slot_index].peer_registration_candidate = false;
                    portEXIT_CRITICAL(&s_connection_lock);
                    event_log_add("BLE registration: окно истекло до завершения pairing");
                    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                }
            }

            struct ble_gap_conn_desc desc;
            int8_t rssi;
            int find_rc = ble_gap_conn_find(conn_handle, &desc);
            int rssi_rc = find_rc == 0 ? ble_gap_conn_rssi(conn_handle, &rssi) : find_rc;
            if (find_rc != 0) {
                portENTER_CRITICAL(&s_connection_lock);
                int slot_index = slot_index_for_handle_locked(conn_handle);
                if (slot_index >= 0) {
                    bool was_secured = s_slots[slot_index].encrypted && s_slots[slot_index].bonded;
                    clear_slot_locked(&s_slots[slot_index]);
                    disconnected_last_secured |= was_secured && !any_secured_slot_locked();
                }
                portEXIT_CRITICAL(&s_connection_lock);
                event_log_add("BLE watchdog: устаревший handle=%u удалён (find=%d)",
                              conn_handle, find_rc);
                ble_phone_start_advertising();
                continue;
            }

            if (rssi_rc != 0) {
                int64_t failure_since;
                int64_t terminate_retry;
                portENTER_CRITICAL(&s_connection_lock);
                int slot_index = slot_index_for_handle_locked(conn_handle);
                if (slot_index < 0) {
                    portEXIT_CRITICAL(&s_connection_lock);
                    continue;
                }
                ble_slot_t *slot = &s_slots[slot_index];
                slot->rssi_valid = false;
                slot->rssi_count = slot->rssi_next = 0;
                if (slot->link_probe_failure_since_ms == 0)
                    slot->link_probe_failure_since_ms = now_ms;
                failure_since = slot->link_probe_failure_since_ms;
                terminate_retry = slot->link_terminate_retry_ms;
                portEXIT_CRITICAL(&s_connection_lock);
                if (now_ms - failure_since >= BLE_LINK_PROBE_FAILURE_GRACE_MS &&
                    now_ms - terminate_retry >= BLE_LINK_PROBE_FAILURE_GRACE_MS) {
                    int terminate_rc = ble_gap_terminate(conn_handle,
                                                         BLE_ERR_REM_USER_CONN_TERM);
                    portENTER_CRITICAL(&s_connection_lock);
                    int slot_index = slot_index_for_handle_locked(conn_handle);
                    if (slot_index >= 0) s_slots[slot_index].link_terminate_retry_ms = now_ms;
                    portEXIT_CRITICAL(&s_connection_lock);
                    event_log_add("BLE RSSI lost h=%u ms=%lld read=%d term=%d",
                                  conn_handle, now_ms - failure_since,
                                  rssi_rc, terminate_rc);
                }
                continue;
            }

            int8_t filtered_rssi = rssi;
            bool filtered_valid = false;
            bool secured = desc.sec_state.encrypted && desc.sec_state.bonded;
            portENTER_CRITICAL(&s_connection_lock);
            int slot_index = slot_index_for_handle_locked(conn_handle);
            if (slot_index >= 0) {
                ble_slot_t *slot = &s_slots[slot_index];
                slot->link_probe_failure_since_ms = 0;
                slot->link_terminate_retry_ms = 0;
                slot->rssi_samples[slot->rssi_next] = rssi;
                slot->rssi_next = (slot->rssi_next + 1) % 3;
                if (slot->rssi_count < 3) slot->rssi_count++;
                if (slot->rssi_count == 3) {
                    int a = slot->rssi_samples[0], b = slot->rssi_samples[1], c = slot->rssi_samples[2];
                    filtered_rssi = a > b ? (b > c ? b : a > c ? c : a) :
                                           (a > c ? a : b > c ? c : b);
                    slot->last_rssi = filtered_rssi;
                    slot->rssi_valid = filtered_valid = true;
                }
                slot->encrypted = desc.sec_state.encrypted;
                slot->bonded = desc.sec_state.bonded;
            }
            portEXIT_CRITICAL(&s_connection_lock);

            app_config_t cfg;
            app_config_get(&cfg);
            bool zone_sufficient = filtered_valid &&
                                    filtered_rssi >= cfg.phone_rssi_threshold;
            if (filtered_valid && !zone_sufficient) {
                event_log_add("BLE zone exit h=%u raw=%d med=%d lim=%d",
                              conn_handle, rssi, filtered_rssi,
                              cfg.phone_rssi_threshold);
                ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                continue;
            }

            if (!secured || !filtered_valid) continue;
            have_valid_secured_rssi = true;
            any_sufficient |= zone_sufficient;
        }
        if (disconnected_last_secured) lock_control_report_disconnected();
            if (have_valid_secured_rssi) lock_control_report_rssi(any_sufficient);
        int64_t central_deadline = atomic_load(&s_central_phase_deadline_ms);
        int64_t central_now = esp_timer_get_time() / 1000;
        if (central_deadline > 0 && central_now >= central_deadline &&
            !atomic_exchange(&s_central_timeout_pending, true)) {
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_central_timeout_event);
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
    return xTaskCreate(rssi_task, "ble_phone_rssi", 4096, NULL, 5, &s_rssi_task) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t start_advertising_on_host(void)
{
    if (!any_slot_free()) {
        if (ble_gap_adv_active()) {
            int stop_rc = ble_gap_adv_stop();
            if (stop_rc != 0 && stop_rc != BLE_HS_EALREADY) return ESP_FAIL;
        }
        return ESP_OK;
    }
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) return ESP_FAIL;

    bool registration_allowed;
    ble_phone_get_registration(&registration_allowed, NULL);
    ble_addr_t peers[MAX_BONDED_PHONES];
    int bond_count = ble_phone_get_bonded_peers(peers, MAX_BONDED_PHONES);
    if (bond_count < 0) return ESP_FAIL;
    bool restricted = !registration_allowed && bond_count > 0;
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
    if (ble_phone_refresh_accept_list(restricted, peers, bond_count) != ESP_OK) return ESP_FAIL;
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
    if (rc == 0) {
        event_log_add("BLE GATT advertising запущен");
        return ESP_OK;
    }
    if (rc == BLE_HS_EALREADY) {
        event_log_add("BLE GATT advertising запущен");
        return ESP_OK;
    }
    event_log_add("BLE GATT advertising error: %d", rc);
    return ESP_FAIL;
}

static void advertising_work(struct ble_npl_event *event)
{
    (void)event;
    (void)start_advertising_on_host();
}

static esp_err_t ble_phone_start_advertising(void)
{
    if (!s_advertising_ready) return ESP_ERR_INVALID_STATE;
    /* NPL coalesces an already queued event; all GAP advertising writes run
     * on the host task, including calls originating in HTTP and watchdog. */
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_advertising_event);
    return ESP_OK;
}

static void on_sync(void)
{
    s_advertising_ready = true;
    event_log_add("NimBLE GATT peripheral и phone scanner синхронизированы");
    if (ble_phone_start_advertising() != ESP_OK) {
        event_log_add("BLE phone advertising не запущен");
    }
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_central_event);
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
    ble_npl_event_init(&s_advertising_event, advertising_work, NULL);
    ble_npl_event_init(&s_central_event, central_scan_work, NULL);
    ble_npl_event_init(&s_central_timeout_event, central_timeout_work, NULL);
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
