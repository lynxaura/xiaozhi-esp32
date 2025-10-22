/*
 * Minimal BLUFI init glue, adapted from Espressif example.
 */

#include <stdio.h>
#include "esp_err.h"
#include "esp_blufi_api.h"
#include "esp_log.h"
#include "esp_blufi.h"

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
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "console/console.h"
// For ble_store_config_init
#include "store/config/ble_store_config.h"
// Some IDF setups may not expose the header in include paths; provide extern as fallback.
extern void ble_store_config_init(void);
#endif

#ifndef BLUFI_DEVICE_NAME
#define BLUFI_DEVICE_NAME "Xiaozhi-BluFi"
#endif

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
    ret = esp_ble_gap_set_device_name(BLUFI_DEVICE_NAME);
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
    int rc;
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_infer_auto(0, &addr_val[0]);
    assert(rc == 0);
    rc = ble_hs_id_copy_addr(0, addr_val, NULL);
    assert(rc == 0);
    ble_svc_gap_init();
    ble_svc_gatt_init();
}

void bleprph_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t esp_blufi_host_init(void)
{
    esp_err_t err = esp_nimble_init();
    if (err) return ESP_FAIL;

    ble_hs_cfg.reset_cb = blufi_on_reset;
    ble_hs_cfg.sync_cb = blufi_on_sync;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    int rc = ble_svc_gap_device_name_set(BLUFI_DEVICE_NAME);
    assert(rc == 0);
#endif
    ble_store_config_init();
    // Register BLUFI GATT services before enabling the host
    rc = esp_blufi_gatt_svr_init();
    if (rc) {
        return ESP_FAIL;
    }
    esp_blufi_btc_init();
    err = esp_nimble_enable(bleprph_host_task);
    if (err) return ESP_FAIL;
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
