#include "ble_server.h"

#include <cstring>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_nimble_hci.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#define TAG "BleServer"
#define DEVICE_NAME "Xiaozhi-Compass"

namespace {

BleServer* g_instance = nullptr;
uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
bool g_advertising = false;
bool g_paused = false;

static const ble_uuid128_t k_svc_uuid =
    BLE_UUID128_INIT(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                     0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0);
static const ble_uuid16_t k_chr_uuid = BLE_UUID16_INIT(0x2A00);

static void StartAdvertising();

static int GattAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &k_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &k_chr_uuid.u,
                .access_cb = GattAccessCb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    { 0 },
};

static int GapEvent(struct ble_gap_event* event, void* arg)
{
    auto* server = static_cast<BleServer*>(arg);
    if (server == nullptr) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            g_advertising = false;
            ESP_LOGI(TAG, "BLE connected, handle=%d", g_conn_handle);
            server->NotifyStatus(BleStatus::CONNECTED);
        } else {
            ESP_LOGW(TAG, "BLE connect failed, status=%d", event->connect.status);
            StartAdvertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        StartAdvertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        g_advertising = false;
        if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE && server->IsRunning()) {
            server->NotifyStatus(BleStatus::ADVERTISING);
        }
        break;

    default:
        break;
    }

    return 0;
}

static void StartAdvertising()
{
    if (g_advertising) {
        ESP_LOGI(TAG, "Advertising already active, skip");
        return;
    }
    if (g_paused) {
        ESP_LOGI(TAG, "BLE is paused, skip advertising");
        return;
    }

    ESP_LOGI(TAG, "Configuring advertising data...");
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t*>(DEVICE_NAME);
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.uuids128 = &k_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "  FAILED: ble_gap_adv_set_fields: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "  OK: Adv fields set (name=%s, 128bit UUID included)", DEVICE_NAME);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ESP_LOGI(TAG, "Starting advertising (undirected, general discoverable)...");
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                           &adv_params, GapEvent, g_instance);
    if (rc != 0) {
        ESP_LOGE(TAG, "  FAILED: ble_gap_adv_start: %d", rc);
        return;
    }

    g_advertising = true;
    ESP_LOGI(TAG, "  OK: Advertising started successfully!");
    ESP_LOGI(TAG, "  Device: %s (public address)", DEVICE_NAME);
    ESP_LOGI(TAG, "  Mode: General Discoverable, Connectable");
    if (g_instance != nullptr) {
        g_instance->NotifyStatus(BleStatus::ADVERTISING);
    }
}

static void OnReset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

static void OnSync()
{
    ESP_LOGI(TAG, "NimBLE host synchronized, configuring device...");

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "  FAILED: ble_hs_util_ensure_addr: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "  OK: BLE address ensured");

    ble_svc_gap_device_name_set(DEVICE_NAME);
    ESP_LOGI(TAG, "  OK: Device name set to '%s'", DEVICE_NAME);

    ESP_LOGI(TAG, "  Starting advertising...");
    StartAdvertising();
    ESP_LOGI(TAG, "OnSync complete - BLE is now discoverable");
}

static void HostTask(void* param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

extern "C" void ble_store_config_init(void);

BleServer& BleServer::GetInstance()
{
    static BleServer instance;
    g_instance = &instance;
    return instance;
}

void BleServer::SetStatusCallback(StatusCallback callback)
{
    status_callback_ = std::move(callback);
}

void BleServer::NotifyStatus(BleStatus status)
{
    status_ = status;
    if (status_callback_) {
        status_callback_(status);
    }
}

void BleServer::Pause()
{
    if (!running_) {
        return;
    }
    g_paused = true;
    paused_ = true;
    if (g_advertising) {
        ble_gap_adv_stop();
        g_advertising = false;
    }
    ESP_LOGI(TAG, "BLE paused");
}

void BleServer::Resume()
{
    if (!running_ || !paused_) {
        return;
    }
    g_paused = false;
    paused_ = false;
    StartAdvertising();
    ESP_LOGI(TAG, "BLE resumed");
}

esp_err_t BleServer::Start()
{
    if (running_) {
        ESP_LOGI(TAG, "BLE already running, skip start");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "===== BLE Initialization Start =====");

    ESP_LOGI(TAG, "[1/7] Initializing BT controller...");
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "  FAILED: esp_bt_controller_init: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "  OK: BT controller initialized");

    ESP_LOGI(TAG, "[2/7] Enabling BLE controller mode...");
    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "  FAILED: esp_bt_controller_enable: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "  OK: BLE controller enabled");

    ESP_LOGI(TAG, "[3/7] Initializing NimBLE host stack...");
    err = esp_nimble_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "  FAILED: esp_nimble_init: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "  OK: NimBLE host initialized");

    ESP_LOGI(TAG, "[4/7] Configuring NimBLE host callbacks...");
    ble_hs_cfg.reset_cb = OnReset;
    ble_hs_cfg.sync_cb = OnSync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ESP_LOGI(TAG, "  OK: Host callbacks configured (reset_cb, sync_cb)");

    ESP_LOGI(TAG, "[5/7] Registering GATT services...");
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "  FAILED: ble_gatts_count_cfg: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "  FAILED: ble_gatts_add_svcs: %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  OK: GATT services registered (1 primary service, 1 characteristic)");

    ESP_LOGI(TAG, "[6/7] Initializing GAP/GATT services and store...");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_store_config_init();
    ESP_LOGI(TAG, "  OK: GAP/GATT/Store initialized");

    ESP_LOGI(TAG, "[7/7] Starting NimBLE host task...");
    nimble_port_freertos_init(HostTask);
    ESP_LOGI(TAG, "  OK: Host task started");

    running_ = true;
    status_ = BleStatus::DISABLED;
    ESP_LOGI(TAG, "===== BLE Initialization Complete =====");
    ESP_LOGI(TAG, "Device name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Advertising will start after host sync (OnSync callback)");
    return ESP_OK;
}

esp_err_t BleServer::Stop()
{
    if (!running_) {
        NotifyStatus(BleStatus::DISABLED);
        return ESP_OK;
    }

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }

    if (g_advertising) {
        ble_gap_adv_stop();
        g_advertising = false;
    }

    esp_err_t err = nimble_port_stop();
    if (err == ESP_OK) {
        esp_nimble_deinit();
    }

    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    running_ = false;
    NotifyStatus(BleStatus::DISABLED);
    ESP_LOGI(TAG, "BLE server stopped");
    return ESP_OK;
}
