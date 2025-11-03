/*
 * Minimal BLUFI init glue, adapted from Espressif example.
 */

#include <stdio.h>
#include "esp_err.h"
#include "esp_blufi_api.h"
#include "esp_log.h"
#include "esp_blufi.h"
#include "esp_mac.h"

#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
#include "esp_bt.h"
#endif

#ifdef CONFIG_BT_BLUEDROID_ENABLED
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#endif

#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "console/console.h"
// For ble_store_config_init and ble_store_util_status_rr
#include "store/config/ble_store_config.h"
// Some IDF setups may not expose the header in include paths; provide extern as fallback.
extern void ble_store_config_init(void);
#endif

// Generate BluFi device name with MAC address suffix
static void get_blufi_device_name(char *name, size_t max_len) {
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret == ESP_OK) {
        // Use last 2 bytes (4 hex digits) of MAC address
        snprintf(name, max_len, "magic-%02X%02X", mac[4], mac[5]);
    } else {
        // Fallback if MAC read fails
        snprintf(name, max_len, "magic-0000");
    }
}

// Use prototypes from esp_blufi.h; no manual declarations needed

#ifdef CONFIG_BT_BLUEDROID_ENABLED
esp_err_t esp_blufi_host_init(void)
{
    int ret;
    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&cfg);
    if (ret) {
        return ESP_FAIL;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        return ESP_FAIL;
    }

    // Generate device name dynamically with MAC address
    char device_name[32];
    get_blufi_device_name(device_name, sizeof(device_name));
    ESP_LOGI("BLUFI_INIT", "Setting BluFi device name: %s", device_name);
    ret = esp_ble_gap_set_device_name(device_name);
    if (ret) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_blufi_host_deinit(void)
{
    int ret;
    ret = esp_blufi_profile_deinit();
    if (ret != ESP_OK) return ret;
    ret = esp_bluedroid_disable();
    if (ret) return ESP_FAIL;
    ret = esp_bluedroid_deinit();
    if (ret) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t esp_blufi_gap_register_callback(void)
{
    int rc;
    rc = esp_ble_gap_register_callback(esp_blufi_gap_event_handler);
    if (rc) return rc;
    return esp_blufi_profile_init();
}

esp_err_t esp_blufi_host_and_cb_init(esp_blufi_callbacks_t *example_callbacks)
{
    esp_err_t ret = ESP_OK;
    ret = esp_blufi_host_init();
    if (ret) return ret;
    ret = esp_blufi_register_callbacks(example_callbacks);
    if (ret) return ret;
    ret = esp_blufi_gap_register_callback();
    if (ret) return ret;
    return ESP_OK;
}
#endif // CONFIG_BT_BLUEDROID_ENABLED

#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
esp_err_t esp_blufi_controller_init()
{
    esp_err_t ret = ESP_OK;
#if CONFIG_IDF_TARGET_ESP32
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
#endif
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) return ret;
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    return ret;
}

esp_err_t esp_blufi_controller_deinit()
{
    esp_err_t ret = ESP_OK;
    ret = esp_bt_controller_disable();
    if (ret) return ret;
    return esp_bt_controller_deinit();
}
#endif // controller

#ifdef CONFIG_BT_NIMBLE_ENABLED
static void blufi_on_reset(int reason)
{
    ESP_LOGW("BLUFI_INIT", "Resetting state; reason=%d", reason);
}

static void blufi_on_sync(void)
{
    // Initialize BluFi GATT profile after BLE stack is synced
    // This will trigger ESP_BLUFI_EVENT_INIT_FINISH event
    esp_blufi_profile_init();
}

void bleprph_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t esp_blufi_host_init(void)
{
    esp_err_t err = esp_nimble_init();
    if (err) {
        ESP_LOGE("BLUFI_INIT", "esp_nimble_init failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    // Configure NimBLE host callbacks
    ble_hs_cfg.reset_cb = blufi_on_reset;
    ble_hs_cfg.sync_cb = blufi_on_sync;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Configure security parameters
    ble_hs_cfg.sm_io_cap = 4;  // No input/output capability

    // Initialize BluFi GATT services BEFORE enabling the host
    int rc = esp_blufi_gatt_svr_init();
    assert(rc == 0);

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    // Generate device name dynamically with MAC address
    char device_name[32];
    get_blufi_device_name(device_name, sizeof(device_name));
    ESP_LOGI("BLUFI_INIT", "Setting BluFi device name: %s", device_name);
    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);
#endif

    // Initialize BLE storage configuration
    ble_store_config_init();

    // Initialize BluFi Bluetooth Controller integration
    esp_blufi_btc_init();

    // Enable NimBLE host (creates host task and runs event loop)
    err = esp_nimble_enable(bleprph_host_task);
    if (err) {
        ESP_LOGE("BLUFI_INIT", "esp_nimble_enable failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t esp_blufi_host_deinit(void)
{
    esp_err_t ret = ESP_OK;
    esp_blufi_gatt_svr_deinit();
    ret = nimble_port_stop();
    if (ret != ESP_OK) return ret;
    esp_nimble_deinit();
    ret = esp_blufi_profile_deinit();
    if (ret != ESP_OK) return ret;
    esp_blufi_btc_deinit();
    return ret;
}

esp_err_t esp_blufi_gap_register_callback(void)
{
    return ESP_OK;
}

esp_err_t esp_blufi_host_and_cb_init(esp_blufi_callbacks_t *example_callbacks)
{
    esp_err_t ret = ESP_OK;
    ret = esp_blufi_register_callbacks(example_callbacks);
    if (ret) return ret;
    ret = esp_blufi_gap_register_callback();
    if (ret) return ret;
    ret = esp_blufi_host_init();
    if (ret) return ret;
    return ret;
}
#endif // NimBLE
