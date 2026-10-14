#include "ble_phone.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "ap_mode.h"
#include "app_config.h"
#include "event_log.h"
#include "led.h"
#include "lock_control.h"
#include "mqtt_config.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "mbedtls/platform_util.h"
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
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define REGISTRATION_WINDOW_US (60LL * 1000000LL)
#define MAX_BONDED_PHONES 20
#define CENTRAL_CONNECT_TIMEOUT_MS 10000
#define CENTRAL_SECURITY_TIMEOUT_MS 10000
#define CENTRAL_REGISTRATION_SECURITY_TIMEOUT_MS 55000
#define CENTRAL_GATT_TIMEOUT_MS 8000
#define CENTRAL_NEW_BOND_SETTLE_MS 5000
#define BOND_NAMES_NVS_NS "ble_names"
#define OWNER_NVS_NS "ble_owner"
#define OWNER_NVS_KEY "identity"

void ble_store_config_init(void);

/* Android phone beacon: public discovery marker, protected GATT proof. */
static const ble_uuid128_t s_phone_beacon_service_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x10, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_beacon_proof_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x11, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_mqtt_uuid = BLE_UUID128_INIT(
    0xa3,0x92,0x81,0x70,0x6f,0x5e,0x32,0x8d,0x21,0x4c,0x7b,0x9a,0x1d,0x00,0x4f,0x6f);
static uint16_t s_central_mqtt_handle;
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
static const ble_uuid128_t s_phone_service_request_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x1b, 0x00, 0x4f, 0x6f);
static const ble_uuid128_t s_phone_service_result_uuid = BLE_UUID128_INIT(
    0xa3, 0x92, 0x81, 0x70, 0x6f, 0x5e, 0x32, 0x8d,
    0x21, 0x4c, 0x7b, 0x9a, 0x1c, 0x00, 0x4f, 0x6f);
#define PHONE_SERVICE_MODE 0x83
#define PHONE_SERVICE_WIFI 1
#define PHONE_SERVICE_WEB_IP 2
#define PHONE_SERVICE_HOLD 4
#define PHONE_SERVICE_HOME_WIFI 8
#define PHONE_BEACON_COMPANY_ID 0xf0e1
#define PHONE_BEACON_RETRY_MS 1000

static uint8_t s_addr_type;
static _Atomic uint32_t s_central_suppressed_device_id;
static _Atomic bool s_central_suppressed_device_id_valid;
static portMUX_TYPE s_registration_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_registration_deadline_us;
static atomic_bool s_host_ready;
static TaskHandle_t s_rssi_task;
unsigned ble_phone_stack_free(void)
{
    return s_rssi_task ? (unsigned)uxTaskGetStackHighWaterMark(s_rssi_task) : 0;
}

static void central_cancel_for_bond_reset(void);
static void central_finish_bond_reset(void);
static void disconnect_deleted_peer(const ble_addr_t *peer, bool disconnect_any);

static esp_err_t ble_phone_apply_tx_power(void)
{
    const esp_power_level_t level = (esp_power_level_t)((APP_BLE_TX_POWER_DBM + 12) / 3);
    esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, level);
    event_log_add("BLE: мощность передатчика %d dBm: %s",
                  APP_BLE_TX_POWER_DBM, esp_err_to_name(err));
    return err;
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

}

void ble_phone_set_registration_mode(bool enabled)
{
    portENTER_CRITICAL(&s_registration_lock);
    s_registration_deadline_us = enabled ? INT64_MAX : 0;
    portEXIT_CRITICAL(&s_registration_lock);
    event_log_add("BLE registration режим настройки: %s", enabled ? "открыта" : "закрыта");
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
static bool s_central_service_candidate;
static uint16_t s_central_service_request_handle, s_central_service_result_handle;
static uint8_t s_central_service_flags, s_central_service_command;
static uint8_t s_central_service_request_id[4];
static int64_t s_central_hold_deadline_ms;
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
    CENTRAL_PHASE_SERVICE_REQUEST_READ,
    CENTRAL_PHASE_HOLD_WAIT,
    CENTRAL_PHASE_SERVICE_RESULT_WRITE,
};

void ble_phone_get_status(ble_phone_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    struct ble_gap_conn_desc desc;
    uint16_t handle = s_central_handle;
    if (!s_host_ready || handle == BLE_HS_CONN_HANDLE_NONE ||
        ble_gap_conn_find(handle, &desc) != 0) return;
    out->connected = true;
    out->encrypted = desc.sec_state.encrypted;
    out->bonded = desc.sec_state.bonded;
    out->rssi_valid = ble_gap_conn_rssi(handle, &out->rssi) == 0;
}

static void disconnect_deleted_peer(const ble_addr_t *peer, bool disconnect_any)
{
    uint16_t handle = s_central_handle;
    struct ble_gap_conn_desc desc;
    if (handle == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(handle, &desc) != 0 ||
        (!disconnect_any && !address_equal(peer, &desc.peer_id_addr))) return;
    int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    event_log_add("BLE: сессия удалённого bond завершается, rc=%d", rc);
}

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
    s_central_service_candidate = false;
    s_central_service_request_handle = 0;
    s_central_service_result_handle = 0;
    s_central_service_flags = 0;
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

/* Called by the version-pinned NimBLE guard, before sending any Pairing Request. */
int ble_lock_pairing_allowed(uint16_t handle);
int ble_lock_pairing_allowed(uint16_t handle)
{
    bool allowed = false;
    uint32_t remaining_s = 0;
    ble_phone_get_registration(&allowed, &remaining_s);
    if (handle != s_central_handle || atomic_load(&s_central_reset_pending)) return 0;
    if (!allowed) {
        atomic_store(&s_central_suppressed_device_id, s_central_device_id);
        atomic_store(&s_central_suppressed_device_id_valid, true);
        event_log_add("BLE central: SMP запрещён вне регистрации; ключи сохранены, peer подавлен до BOOT window");
        return 0;
    }
    if (!s_central_registration_candidate) {
        s_central_registration_candidate = true;
        s_central_existing_ltk_restored = false;
        s_central_existing_ltk_before_connect = false;
        uint32_t timeout_ms = CENTRAL_REGISTRATION_SECURITY_TIMEOUT_MS;
        if (remaining_s != UINT32_MAX && remaining_s * 1000U < timeout_ms)
            timeout_ms = remaining_s * 1000U;
        central_set_phase(CENTRAL_PHASE_SECURITY, timeout_ms);
        event_log_add("BLE central: восстановление SMP разрешено в окне регистрации; ожидаю Android до %lu мс",
                      (unsigned long)timeout_ms);
    }
    return 1;
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
    event_log_add("BLE central: %s; bond %s сохранён, peer подавлен до BOOT window",
                  cause, address);
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

bool ble_phone_is_owner(const char *phone)
{
    if (!phone || strlen(phone) != 13) return false;
    ble_addr_t owner;
    if (owner_load(&owner) != ESP_OK || owner.type > 1) return false;
    char id[14];
    snprintf(id, sizeof(id), "%x%02x%02x%02x%02x%02x%02x", (unsigned)owner.type,
             owner.val[5], owner.val[4], owner.val[3], owner.val[2], owner.val[1], owner.val[0]);
    return strcmp(id, phone) == 0;
}

esp_err_t ble_phone_owner_id(char out[14])
{
    if (!out) return ESP_ERR_INVALID_ARG;
    ble_addr_t owner;
    esp_err_t err = owner_load(&owner);
    if (err != ESP_OK || owner.type > 1) return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    snprintf(out, 14, "%x%02x%02x%02x%02x%02x%02x", (unsigned)owner.type,
             owner.val[5], owner.val[4], owner.val[3], owner.val[2], owner.val[1], owner.val[0]);
    return ESP_OK;
}

esp_err_t ble_phone_mqtt_key(const char *phone, uint8_t out[16])
{
    if (!phone || strlen(phone)!=13 || !out) return ESP_ERR_INVALID_ARG;
    ble_addr_t peers[MAX_BONDED_PHONES]; int count=0;
    if(ble_store_util_bonded_peers(peers,&count,MAX_BONDED_PHONES)!=0) return ESP_FAIL;
    for(int i=0;i<count;i++) {
        char id[14];
        snprintf(id,sizeof(id),"%x%02x%02x%02x%02x%02x%02x",(unsigned)(peers[i].type & 1),
                 peers[i].val[5],peers[i].val[4],peers[i].val[3],peers[i].val[2],peers[i].val[1],peers[i].val[0]);
        if(strcmp(id,phone)) continue;
        struct ble_store_key_sec key={.peer_addr=peers[i]};
        struct ble_store_value_sec value={0};
        int rc=ble_store_read_peer_sec(&key,&value);
        if(rc==0 && value.ltk_present) memcpy(out,value.ltk,16);
        bool ok=rc==0 && value.ltk_present;
        mbedtls_platform_zeroize(&value,sizeof(value));
        return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
    return ESP_ERR_NOT_FOUND;
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

static void central_send_owner_status_after_mqtt(uint16_t handle)
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

static void (*s_mqtt_continue)(uint16_t);
static int central_mqtt_write_cb(uint16_t handle,const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr,void *arg)
{
    (void)error; (void)attr; (void)arg;
    if(s_mqtt_continue) s_mqtt_continue(handle);
    return 0;
}
static void central_export_mqtt(uint16_t handle,void (*next)(uint16_t))
{
    s_mqtt_continue=next;
    struct ble_gap_conn_desc desc;
    uint8_t ltk[16]={0}, mac[6]; char phone[14], target[18], hex[33];
    mqtt_config_t cfg;
    if(!s_central_mqtt_handle || ble_gap_conn_find(handle,&desc)!=0 ||
       !desc.sec_state.encrypted || !desc.sec_state.bonded) {
        next(handle); return;
    }
    snprintf(phone,sizeof(phone),"%x%02x%02x%02x%02x%02x%02x",(unsigned)(desc.peer_id_addr.type & 1),
             desc.peer_id_addr.val[5],desc.peer_id_addr.val[4],desc.peer_id_addr.val[3],
             desc.peer_id_addr.val[2],desc.peer_id_addr.val[1],desc.peer_id_addr.val[0]);
    if(!ble_phone_is_owner(phone) || ble_phone_mqtt_key(phone,ltk)!=ESP_OK || mqtt_config_get(&cfg)!=ESP_OK ||
       esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK) {
        mbedtls_platform_zeroize(ltk,sizeof(ltk)); next(handle); return;
    }
    snprintf(target,sizeof(target),"%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    for(unsigned i=0;i<16;i++) snprintf(hex+i*2,3,"%02x",ltk[i]);
    cJSON *json=cJSON_CreateObject();
    cJSON_AddStringToObject(json,"target",target); cJSON_AddStringToObject(json,"phone",phone);
    cJSON_AddStringToObject(json,"ltk",hex); cJSON_AddStringToObject(json,"host",cfg.host);
    cJSON_AddBoolToObject(json,"enabled",cfg.enabled);
    uint64_t last_id=0; nvs_handle_t nh;
    if(nvs_open("mqtt_open",NVS_READONLY,&nh)==ESP_OK) { nvs_get_u64(nh,phone,&last_id); nvs_close(nh); }
    char floor[24]; snprintf(floor,sizeof(floor),"%" PRIu64,last_id);
    cJSON_AddStringToObject(json,"last_id",floor);
    char *text=cJSON_PrintUnformatted(json);
    cJSON *secret=cJSON_GetObjectItemCaseSensitive(json,"ltk");
    if(secret && secret->valuestring) mbedtls_platform_zeroize(secret->valuestring,strlen(secret->valuestring));
    cJSON_Delete(json);
    mbedtls_platform_zeroize(ltk,sizeof(ltk)); mbedtls_platform_zeroize(hex,sizeof(hex));
    struct os_mbuf *om=text ? ble_hs_mbuf_from_flat(text,strlen(text)) : NULL;
    if(text) { mbedtls_platform_zeroize(text,strlen(text)); free(text); }
    central_set_phase(CENTRAL_PHASE_OWNER_STATUS_WRITE,CENTRAL_GATT_TIMEOUT_MS);
    if(om && ble_gattc_write_long(handle,s_central_mqtt_handle,0,om,central_mqtt_write_cb,NULL)==0) return;
    next(handle);
}

static void central_send_owner_status(uint16_t handle)
{
    central_export_mqtt(handle,central_send_owner_status_after_mqtt);
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
        char identity[18];
        address_to_string(&desc.peer_id_addr, identity);
        app_config_t cfg;
        app_config_get(&cfg);
        if (s_central_rssi >= cfg.phone_rssi_threshold) {
            lock_feedback_t feedback;
            lock_control_get_feedback(&feedback);
            char actor[18];
            address_to_string(&desc.peer_id_addr, actor);
            if (!feedback.requested_open) lock_request_attributed("open", "ble", actor);
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

static bool central_service_owner(uint16_t handle)
{
    struct ble_gap_conn_desc desc;
    ble_addr_t owner;
    return !atomic_load(&s_central_reset_pending) && central_connection_secured(handle) &&
           ble_gap_conn_find(handle, &desc) == 0 && owner_load(&owner) == ESP_OK &&
           address_equal(&owner, &desc.peer_id_addr);
}

static void central_service_continue(uint16_t handle)
{
    if (!central_service_owner(handle)) {
        central_disconnect(handle);
        return;
    }
    if (s_central_service_flags & PHONE_SERVICE_WIFI) {
        if (s_central_wifi_setup_handles[0]) {
            central_set_phase(CENTRAL_PHASE_WIFI_SETUP_READ, CENTRAL_GATT_TIMEOUT_MS);
            s_central_wifi_setup_value_len = 0;
            s_central_wifi_setup_expected_len = 0;
            if (ble_gattc_read(handle, s_central_wifi_setup_handles[0],
                               central_wifi_setup_read_cb, NULL) == 0) return;
        }
    } else if ((s_central_service_flags & PHONE_SERVICE_WEB_IP) && central_try_send_web_ui_ip(handle)) {
        return;
    }
    central_disconnect(handle);
}

static int central_service_result_cb(uint16_t handle, const struct ble_gatt_error *error,
                                     struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    event_log_add("BLE service: command result write status=%d", error->status);
    if (error->status == 0) central_service_continue(handle);
    else central_disconnect(handle);
    return 0;
}

static void central_service_send_result(uint16_t handle, uint8_t result)
{
    if (atomic_load(&s_central_reset_pending) || !central_connection_secured(handle)) {
        central_disconnect(handle);
        return;
    }
    uint8_t response[7] = {1};
    memcpy(response + 1, s_central_service_request_id, 4);
    response[5] = result;
    response[6] = s_central_service_flags == PHONE_SERVICE_HOME_WIFI
        ? (wifi_ota_home_network_configured() ? 1 : 0)
        : (lock_control_is_hold_open() ? 1 : 0);
    central_set_phase(CENTRAL_PHASE_SERVICE_RESULT_WRITE, CENTRAL_GATT_TIMEOUT_MS);
    event_log_add("BLE service: hold result=%u hold=%u", result, response[6]);
    if (!s_central_service_result_handle ||
        ble_gattc_write_flat(handle, s_central_service_result_handle, response, sizeof(response),
                             central_service_result_cb, NULL) != 0) central_disconnect(handle);
}

static void central_service_check_hold(uint16_t handle)
{
    if (atomic_load(&s_central_reset_pending) || !central_connection_secured(handle)) {
        central_disconnect(handle);
        return;
    }
    lock_feedback_t feedback;
    lock_control_get_feedback(&feedback);
    bool desired = s_central_service_command == 1;
    bool confirmed = feedback.valid && feedback.hold_open == desired &&
                     (!desired || feedback.state == LOCK_STATE_UNLOCKED);
    if (feedback.fault_flags) central_service_send_result(handle, 3);
    else if (confirmed) central_service_send_result(handle, 0);
    else if (esp_timer_get_time() / 1000 >= s_central_hold_deadline_ms)
        central_service_send_result(handle, 3);
    else central_set_phase(CENTRAL_PHASE_HOLD_WAIT, 100);
}

static int central_service_request_cb(uint16_t handle, const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    uint8_t request[7];
    bool valid = error->status == 0 && attr && attr->om &&
                 OS_MBUF_PKTLEN(attr->om) == sizeof(request) &&
                 ble_hs_mbuf_to_flat(attr->om, request, sizeof(request), NULL) == 0 &&
                 request[0] == 1 && request[1] != 0 && (request[1] & ~15) == 0 &&
                 request[2] <= 2;
    if (!valid || !central_connection_secured(handle) || atomic_load(&s_central_reset_pending)) {
        event_log_add("BLE service: invalid/unsecured request status=%d", error->status);
        central_disconnect(handle);
        return 0;
    }
    s_central_service_flags = request[1];
    s_central_service_command = request[2];
    memcpy(s_central_service_request_id, request + 3, 4);
    if ((request[1] & PHONE_SERVICE_HOME_WIFI) &&
        (request[1] != PHONE_SERVICE_HOME_WIFI || request[2] != 0 ||
         !s_central_service_result_handle || !(request[3] || request[4] || request[5] || request[6]))) {
        central_disconnect(handle);
        return 0;
    }
    if (!(request[1] & (PHONE_SERVICE_HOLD | PHONE_SERVICE_HOME_WIFI)) &&
        (request[2] != 0 || request[3] || request[4] || request[5] || request[6])) {
        central_disconnect(handle);
        return 0;
    }
    if ((request[1] & PHONE_SERVICE_HOLD) && (!s_central_service_result_handle ||
        !(request[3] || request[4] || request[5] || request[6]))) {
        central_disconnect(handle);
        return 0;
    }
    if (request[1] != PHONE_SERVICE_HOME_WIFI && !central_service_owner(handle)) {
        event_log_add("BLE service: owner access denied for settings request");
        if (request[1] & PHONE_SERVICE_HOLD) central_service_send_result(handle, 1);
        else central_disconnect(handle);
        return 0;
    }
    struct ble_gap_conn_desc cloud_desc;
    char actor[18] = "";
    if (ble_gap_conn_find(handle, &cloud_desc) == 0) address_to_string(&cloud_desc.peer_id_addr, actor);
    event_log_add("BLE service: request flags=%u command=%u", request[1], request[2]);
    if (request[1] == PHONE_SERVICE_HOME_WIFI) {
        esp_err_t err = wifi_ota_request_home_wifi();
        event_log_add("BLE service: home Wi-Fi request %s", esp_err_to_name(err));
        central_service_send_result(handle, err == ESP_OK ? 0 : 2);
    } else if (request[1] & PHONE_SERVICE_HOLD) {
        if (request[2] == 0) central_service_send_result(handle, 0);
        else if (lock_request_attributed(request[2] == 1 ? "hold_on" : "hold_off", "ble", actor) != ESP_OK)
            central_service_send_result(handle, 2);
        else {
            s_central_hold_deadline_ms = esp_timer_get_time() / 1000 + 5000;
            central_set_phase(CENTRAL_PHASE_HOLD_WAIT, 100);
        }
    } else central_service_continue(handle);
    return 0;
}

static void central_service_after_mqtt(uint16_t handle)
{
    if(!central_connection_secured(handle) || atomic_load(&s_central_reset_pending)) { central_disconnect(handle); return; }
    central_set_phase(CENTRAL_PHASE_SERVICE_REQUEST_READ,CENTRAL_GATT_TIMEOUT_MS);
    if(ble_gattc_read(handle,s_central_service_request_handle,central_service_request_cb,NULL)!=0) central_disconnect(handle);
}

static int central_service_proof_cb(uint16_t handle, const struct ble_gatt_error *error,
                                    struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    static const char expected[] = "BLE_LOCK_V1";
    bool valid = error->status == 0 && attr && attr->om &&
                 OS_MBUF_PKTLEN(attr->om) == sizeof(expected) - 1 &&
                 os_mbuf_cmpf(attr->om, 0, expected, sizeof(expected) - 1) == 0;
    if (atomic_load(&s_central_reset_pending) || !central_connection_secured(handle) || !valid) {
        central_disconnect(handle);
        return 0;
    }
    central_export_mqtt(handle,central_service_after_mqtt);
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
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_mqtt_uuid.u) == 0)
            s_central_mqtt_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_owner_status_uuid.u) == 0)
            s_central_owner_status_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_hotspot_ip_uuid.u) == 0)
            s_central_hotspot_ip_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_name_uuid.u) == 0)
            s_central_name_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_beacon_web_ui_ip_uuid.u) == 0)
            s_central_web_ui_ip_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_service_request_uuid.u) == 0)
            s_central_service_request_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_phone_service_result_uuid.u) == 0)
            s_central_service_result_handle = chr->val_handle;
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
    if (error->status == BLE_HS_EDONE && s_central_service_candidate) {
        if (!s_central_proof_handle || !s_central_service_request_handle) {
            event_log_add("BLE service: required characteristic missing");
            central_disconnect(handle);
            return 0;
        }
        central_set_phase(CENTRAL_PHASE_PROOF_READ, CENTRAL_GATT_TIMEOUT_MS);
        if (ble_gattc_read(handle, s_central_proof_handle, central_service_proof_cb, NULL) != 0)
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
        s_central_mqtt_handle = 0;
        memset(s_central_wifi_setup_handles, 0, sizeof(s_central_wifi_setup_handles));
        s_central_hotspot_ip_handle = 0;
        s_central_name_handle = 0;
        s_central_web_ui_ip_handle = 0;
        s_central_service_request_handle = 0;
        s_central_service_result_handle = 0;
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
                (m[6] != 1 && m[6] != PHONE_SERVICE_MODE)) return 0;
            uint32_t device_id = ((uint32_t)m[7] << 24) | ((uint32_t)m[8] << 16) |
                                 ((uint32_t)m[9] << 8) | m[10];
            bool service_beacon = m[6] == PHONE_SERVICE_MODE;
            bool registration_allowed = false;
            ble_phone_get_registration(&registration_allowed, NULL);
            if (registration_allowed) {
                atomic_store(&s_central_suppressed_device_id_valid, false);
            } else if (atomic_load(&s_central_suppressed_device_id_valid) &&
                       atomic_load(&s_central_suppressed_device_id) == device_id) {
                return 0;
            }
            app_config_t cfg;
            app_config_get(&cfg);
            if (!service_beacon && disc->rssi < cfg.phone_rssi_threshold) {
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
            s_central_service_candidate = m[6] == PHONE_SERVICE_MODE;
            s_central_service_flags = 0;
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
    if (atomic_load(&s_central_reset_pending) || !s_host_ready || s_central_scanning || s_central_connecting ||
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
    if (phase == CENTRAL_PHASE_HOLD_WAIT) {
        central_service_check_hold(s_central_handle);
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

static void rssi_task(void *arg)
{
    (void)arg;
    int64_t configuration_check_ms = 0;
    int64_t central_rssi_diagnostic_ms = 0;
    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (s_host_ready && now_ms >= s_central_retry_ms) {
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_central_event);
            s_central_retry_ms = now_ms + PHONE_BEACON_RETRY_MS;
        }
        if (now_ms - configuration_check_ms >= 1000) {
            configuration_check_ms = now_ms;
            bool registration_allowed;
            ble_phone_get_registration(&registration_allowed, NULL);
            led_set_configuration(registration_allowed || ap_mode_is_configuration());
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

        int64_t central_deadline = atomic_load(&s_central_phase_deadline_ms);
        int64_t central_now = esp_timer_get_time() / 1000;
        if (central_deadline > 0 && central_now >= central_deadline &&
            !atomic_exchange(&s_central_timeout_pending, true)) {
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_central_timeout_event);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t ble_phone_security_init(void)
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

    return xTaskCreate(rssi_task, "ble_phone_rssi", 4096, NULL, 5, &s_rssi_task) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        event_log_add("BLE central: local address error=%d", rc);
        return;
    }
    s_host_ready = true;
    event_log_add("NimBLE phone scanner/Central синхронизирован");
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
    ble_npl_event_init(&s_central_event, central_scan_work, NULL);
    ble_npl_event_init(&s_central_timeout_event, central_timeout_work, NULL);
    err = ble_phone_apply_tx_power();
    if (err != ESP_OK) {
        event_log_add("BLE: мощность не применена, запуск с уровнем контроллера по умолчанию");
    }
    err = ble_phone_security_init();
    if (err != ESP_OK) return err;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
