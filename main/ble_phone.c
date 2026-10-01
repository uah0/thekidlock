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
    ble_addr_t peers[MAX_BONDED_PHONES];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, MAX_BONDED_PHONES) != 0) return ESP_FAIL;
    for (int i = 0; i < count; i++) {
        if (ble_store_util_delete_peer(&peers[i]) != 0) return ESP_FAIL;
        bond_name_delete(&peers[i]);
    }
    if (owner_clear() != ESP_OK) return ESP_FAIL;
    disconnect_deleted_peer(NULL, true);
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
            if (!connection_is_admin(conn_handle)) {
                event_log_add("BLE MODE отклонён: требуется владелец/admin");
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
            if (!connection_is_admin(conn_handle)) {
                event_log_add("BLE AUTO_OFF_CLOSE отклонён: требуется владелец/admin");
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
        if (!connection_is_admin(conn_handle)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
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
                    (i >= 2 && i < 5 && fields[i] < 0) ||
                    (i == 5 && (fields[i] < APP_BLE_TX_POWER_MIN_DBM ||
                                fields[i] > APP_BLE_TX_POWER_MAX_DBM ||
                                (fields[i] - APP_BLE_TX_POWER_MIN_DBM) % 3 != 0)))
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
            if (err == ESP_OK && field_count == 6)
                err = app_config_set_ble_tx_power((int)fields[5]);
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
            event_log_add("BLE GATT: телефон отключён");
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
    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
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
                    event_log_add("BLE watchdog: RSSI недоступен %lld мс, разрыв handle=%u rc=%d",
                                  now_ms - failure_since, conn_handle, terminate_rc);
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
    ble_npl_event_init(&s_advertising_event, advertising_work, NULL);
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
