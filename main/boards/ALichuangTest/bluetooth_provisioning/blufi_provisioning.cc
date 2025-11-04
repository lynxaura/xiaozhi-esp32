// BluFi provisioning implementation integrated with project services

#include "blufi_provisioning.h"

#include <cstring>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>
#include <esp_netif_types.h>
#include <esp_mac.h>

extern "C" {
#include "esp_blufi.h" // for esp_blufi_adv_start()
}

#include <ssid_manager.h>

extern "C" {
    // From local security/init glue
    void blufi_security_deinit(void);
    int blufi_security_init(void);
    void blufi_dh_negotiate_data_handler(uint8_t *data, int len, uint8_t **output_data, int *output_len, bool *need_free);
    int blufi_aes_encrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
    int blufi_aes_decrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
    uint16_t blufi_crc_checksum(uint8_t iv8, uint8_t *data, int len);

    esp_err_t esp_blufi_controller_init(void);
    esp_err_t esp_blufi_controller_deinit(void);
    esp_err_t esp_blufi_host_and_cb_init(esp_blufi_callbacks_t *example_callbacks);
    esp_err_t esp_blufi_host_deinit(void);
    esp_err_t esp_blufi_gap_register_callback(void);
}

static const char *TAG = "BlufiProvisioning";

static BlufiProvisioning* s_instance = nullptr;

static esp_blufi_callbacks_t s_blufi_callbacks = {
    .event_cb = BlufiProvisioning::EventCallback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

BlufiProvisioning& BlufiProvisioning::GetInstance() {
    static BlufiProvisioning instance;
    return instance;
}

BlufiProvisioning::BlufiProvisioning() {
    s_instance = this;
    event_group_ = xEventGroupCreate();

    // Create scan timeout timer (one-shot, 30 seconds)
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &BlufiProvisioning::ScanTimeoutCallback;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "blufi_scan_timeout";
    esp_timer_create(&timer_args, &scan_timeout_timer_);
}

BlufiProvisioning::~BlufiProvisioning() {
    if (scan_timeout_timer_) {
        esp_timer_stop(scan_timeout_timer_);
        esp_timer_delete(scan_timeout_timer_);
        scan_timeout_timer_ = nullptr;
    }
    if (event_group_) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

void BlufiProvisioning::Start() {
    if (started_) return;
    ESP_LOGI(TAG, "Starting BluFi provisioning");

    // Init security and controller/host
    if (blufi_security_init() != 0) {
        ESP_LOGE(TAG, "blufi_security_init failed");
        return;
    }
    if (esp_blufi_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "esp_blufi_controller_init failed");
        blufi_security_deinit();
        return;
    }
    if (esp_blufi_host_and_cb_init(&s_blufi_callbacks) != ESP_OK) {
        ESP_LOGE(TAG, "esp_blufi_host_and_cb_init failed");
        esp_blufi_controller_deinit();
        blufi_security_deinit();
        return;
    }

    // Advertising starts upon ESP_BLUFI_EVENT_INIT_FINISH in EventCallback
    started_ = true;
}

void BlufiProvisioning::Stop() {
    if (!started_) return;
    ESP_LOGI(TAG, "Stopping BluFi provisioning");
    esp_blufi_host_deinit();
    esp_blufi_controller_deinit();
    blufi_security_deinit();
    started_ = false;
    SetState(BlufiState::IDLE);
}

bool BlufiProvisioning::WaitForConfigured(int timeout_ms) {
    if (configured_) return true;
    EventBits_t bits = xEventGroupWaitBits(event_group_, BIT0, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT0) != 0;
}

void BlufiProvisioning::SetState(BlufiState s) {
    if (on_state_changed_) on_state_changed_(s);
}

void BlufiProvisioning::InitWifiIfNeeded() {
    // Create default netif STA and init Wi-Fi driver (idempotent across reboots of provisioning)
    static bool wifi_inited = false;
    if (wifi_inited) return;

    esp_netif_init();
    static esp_netif_t* s_sta = nullptr;
    if (!s_sta) {
        // Reuse existing default STA netif if present to avoid duplicate if_key
        s_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!s_sta) {
            s_sta = esp_netif_create_default_wifi_sta();
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // Tighten WiFi memory usage during BluFi to avoid OOM with NimBLE
    // These aggressive settings save ~5-8KB of internal RAM during scanning
    cfg.static_rx_buf_num = 2;              // default 6 → 2 (force minimum)
    cfg.dynamic_rx_buf_num = 6;             // default 32 → 6 (reduce dynamic allocation)
    cfg.rx_mgmt_buf_num = 4;                // default 5 → 4 (save ~1.5KB per buffer)
    cfg.mgmt_sbuf_num = 6;                  // default 32 → 6 (minimum allowed, saves ~10KB)
    cfg.ampdu_rx_enable = 0;                // disable AMPDU RX (saves ~2KB)
    cfg.ampdu_tx_enable = 0;                // disable AMPDU TX
    cfg.amsdu_tx_enable = 0;                // disable AMSDU TX
    cfg.rx_ba_win = 0;                      // disable block ack window
    cfg.nvs_enable = 0;                     // we use WIFI_STORAGE_RAM below

    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Free internal heap before WiFi init: %u", (unsigned)free_int);

    esp_err_t err;
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        return; // do not abort
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(err));
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        return;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        return;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        return;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &BlufiProvisioning::WifiEventHandler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &BlufiProvisioning::WifiEventHandler, this, NULL));

    wifi_inited = true;
}

void BlufiProvisioning::ScanTimeoutCallback(void* arg) {
    auto self = static_cast<BlufiProvisioning*>(arg);
    if (!self) return;

    ESP_LOGE(TAG, "WiFi scan timeout (30s) - no SCAN_DONE event received!");
    ESP_LOGE(TAG, "This usually indicates insufficient memory during scan");

    // Log current heap status for debugging
    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGE(TAG, "Free internal heap at timeout: %u bytes", (unsigned)free_int);

    // Send error to BluFi client
    esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
}

void BlufiProvisioning::StopScanTimer() {
    if (scan_timeout_timer_ && esp_timer_is_active(scan_timeout_timer_)) {
        esp_timer_stop(scan_timeout_timer_);
        ESP_LOGD(TAG, "Scan timeout timer stopped");
    }
}

void BlufiProvisioning::StartScan() {
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.ssid = NULL;
    scan_cfg.bssid = NULL;
    scan_cfg.channel = 0; // all channels
    scan_cfg.show_hidden = false;
    // Do NOT set scan_time when Bluetooth is enabled!
    // Let the system use default scan time for proper BT/WiFi coexistence
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi scan started");
        // Start 30-second timeout timer to detect scan hang
        if (scan_timeout_timer_) {
            esp_timer_start_once(scan_timeout_timer_, 30 * 1000000); // 30 seconds in microseconds
            ESP_LOGI(TAG, "Scan timeout timer started (30s)");
        }
    } else {
        ESP_LOGE(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
    }
}

void BlufiProvisioning::ConnectSta() {
    wifi_config_t cfg = {};
    strncpy((char*)cfg.sta.ssid, recv_ssid_.c_str(), sizeof(cfg.sta.ssid)-1);
    strncpy((char*)cfg.sta.password, recv_passwd_.c_str(), sizeof(cfg.sta.password)-1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // safe default

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        return;
    }
}

void BlufiProvisioning::EventCallback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param) {
    auto self = s_instance;
    if (!self) return;

    switch (event) {
    case ESP_BLUFI_EVENT_RECV_NEGOTIATION_DATA:
        ESP_LOGI(TAG, "Recv negotiation data (%d bytes)", param ? param->negotiate_data.data_len : -1);
        break;
    case ESP_BLUFI_EVENT_NEGOTIATION_DONE:
        ESP_LOGI(TAG, "BLUFI negotiation done");
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        ESP_LOGE(TAG, "BLUFI report error: state=%d", param ? param->report_error.state : -1);
        break;
    case ESP_BLUFI_EVENT_GET_VERSION:
        ESP_LOGI(TAG, "BLUFI get version");
        break;
    case ESP_BLUFI_EVENT_INIT_FINISH:
        ESP_LOGI(TAG, "BLUFI init finished, start advertising");
        esp_blufi_adv_start();
        self->SetState(BlufiState::ADVERTISING);
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        ESP_LOGI(TAG, "BLUFI deinit finished");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        ESP_LOGI(TAG, "BLE connected");
        self->ble_connected_ = true;
        self->SetState(BlufiState::CONNECTED);

        // Send MAC address to miniprogram for device binding check
        {
            uint8_t mac[6];
            if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
                // Format: XX:XX:XX:XX:XX:XX (17 bytes with colons)
                char mac_str[18];
                snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                esp_err_t ret = esp_blufi_send_custom_data((uint8_t*)mac_str, strlen(mac_str));
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Sent MAC address via custom data (0x13): %s", mac_str);
                } else {
                    ESP_LOGE(TAG, "Failed to send MAC address: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGE(TAG, "Failed to read MAC address");
            }
        }
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected");
        self->ble_connected_ = false;
        esp_blufi_adv_start();
        self->SetState(BlufiState::ADVERTISING);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST:
        ESP_LOGI(TAG, "Get WiFi list request");
        self->SetState(BlufiState::SCANNING_WIFI);
        // Lazy init Wi-Fi only when needed to save RAM before BT init
        self->InitWifiIfNeeded();
        self->StartScan();
        break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (param && param->sta_ssid.ssid && param->sta_ssid.ssid_len > 0) {
            self->recv_ssid_.assign((const char*)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
            ESP_LOGI(TAG, "Received SSID: %s", self->recv_ssid_.c_str());
        }
        break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (param && param->sta_passwd.passwd && param->sta_passwd.passwd_len >= 0) {
            self->recv_passwd_.assign((const char*)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
            ESP_LOGI(TAG, "Received password length: %d", param->sta_passwd.passwd_len);
        }
        break;
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        if (param && param->custom_data.data && param->custom_data.data_len > 0) {
            // Log the incoming custom data (hex print limited to info)
            const uint8_t* d = param->custom_data.data;
            int n = param->custom_data.data_len;
            char hexbuf[128];
            int pos = 0;
            for (int i = 0; i < n && pos < (int)sizeof(hexbuf) - 3; ++i) {
                pos += snprintf(&hexbuf[pos], sizeof(hexbuf) - pos, "%02X ", d[i]);
            }
            ESP_LOGI(TAG, "Recv custom data (%dB): %s", n, hexbuf);

            // Reply with STA MAC address as ASCII 'XX:XX:XX:XX:XX:XX'
            uint8_t mac[6];
#if CONFIG_IDF_TARGET_ESP32P4
            esp_err_t mac_ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
#else
            esp_err_t mac_ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
            if (mac_ret == ESP_OK) {
                char mac_str[18];
                snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                esp_err_t ret = esp_blufi_send_custom_data((uint8_t*)mac_str, strlen(mac_str));
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Sent MAC via BLUFI custom data: %s", mac_str);
                } else {
                    ESP_LOGE(TAG, "Failed to send MAC via BLUFI: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGE(TAG, "Failed to read MAC: %s", esp_err_to_name(mac_ret));
            }
        } else {
            ESP_LOGW(TAG, "Recv custom data with empty payload");
        }
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        ESP_LOGI(TAG, "Request connect to AP");
        self->SetState(BlufiState::CONNECTING);
        // Save into NVS via SsidManager
        if (!self->recv_ssid_.empty()) {
            SsidManager::GetInstance().AddSsid(self->recv_ssid_, self->recv_passwd_);
        }
        // Ensure Wi-Fi initialized before connecting
        self->InitWifiIfNeeded();
        self->ConnectSta();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        ESP_LOGI(TAG, "Request disconnect from AP");
        esp_wifi_disconnect();
        break;
    default:
        break;
    }
}

void BlufiProvisioning::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto self = static_cast<BlufiProvisioning*>(arg);
    if (!self) return;
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_SCAN_DONE) {
            ESP_LOGI(TAG, "WiFi scan done");
            // Stop the timeout timer since scan completed successfully
            self->StopScanTimer();

            uint16_t ap_num = 0;
            esp_err_t num_ret = esp_wifi_scan_get_ap_num(&ap_num);
            if (num_ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(num_ret));
                esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
                return;
            }
            ESP_LOGI(TAG, "WiFi scan AP count: %u", (unsigned)ap_num);

            if (ap_num == 0) {
                ESP_LOGW(TAG, "No AP found");
                esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
                return;
            }
            // Limit the number of APs to send to keep response size reasonable for clients
            uint16_t send_num = ap_num > 16 ? 16 : ap_num;
            wifi_ap_record_t *ap_list = (wifi_ap_record_t*)calloc(send_num, sizeof(wifi_ap_record_t));
            if (!ap_list) {
                ESP_LOGE(TAG, "malloc ap_list failed");
                esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
                return;
            }
            uint16_t fetched = send_num;
            esp_err_t get_ret = esp_wifi_scan_get_ap_records(&fetched, ap_list);
            if (get_ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(get_ret));
                free(ap_list);
                esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
                return;
            }
            ESP_LOGI(TAG, "Preparing to send %u AP records (from %u total)", (unsigned)fetched, (unsigned)ap_num);

            // Build blufi list
            esp_blufi_ap_record_t *blufi_list = (esp_blufi_ap_record_t*)calloc(fetched, sizeof(esp_blufi_ap_record_t));
            if (!blufi_list) {
                ESP_LOGE(TAG, "malloc blufi_list failed");
                free(ap_list);
                esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
                return;
            }
            for (uint16_t i = 0; i < fetched; ++i) {
                // Ensure null-terminated SSID copy
                memset(blufi_list[i].ssid, 0, sizeof(blufi_list[i].ssid));
                memcpy(blufi_list[i].ssid, ap_list[i].ssid, sizeof(blufi_list[i].ssid));
                blufi_list[i].ssid[sizeof(blufi_list[i].ssid) - 1] = 0;
                blufi_list[i].rssi = ap_list[i].rssi;
#ifdef WIFI_AUTH_OPEN
                // Fill auth mode if the struct supports it (present on most IDF versions)
                blufi_list[i].auth_mode = (uint8_t)ap_list[i].authmode;
#endif
            }

            ESP_LOGI(TAG, "Sending WiFi list via BLUFI (%u entries)", (unsigned)fetched);
            esp_err_t send_ret = esp_blufi_send_wifi_list(fetched, blufi_list);
            if (send_ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_blufi_send_wifi_list failed: %s, attempting per-record fallback", esp_err_to_name(send_ret));
                // Fallback: try sending one-by-one in case large packets cause issues
                for (uint16_t i = 0; i < fetched; ++i) {
                    esp_err_t send_one_ret = esp_blufi_send_wifi_list(1, &blufi_list[i]);
                    if (send_one_ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to send AP[%u] '%s': %s", (unsigned)i, (const char*)blufi_list[i].ssid, esp_err_to_name(send_one_ret));
                        break;
                    }
                    // Small gap to allow BLE notifications to flush
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }

            free(blufi_list);
            free(ap_list);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* e = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "STA disconnected, reason=%d", e ? e->reason : -1);
            esp_blufi_extra_info_t info = {};
            esp_blufi_send_wifi_conn_report(WIFI_MODE_STA, ESP_BLUFI_STA_CONN_FAIL, 0, &info);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP, provisioning success");
        self->configured_ = true;
        xEventGroupSetBits(self->event_group_, BIT0);
        self->SetState(BlufiState::CONNECTED_WIFI);
        esp_blufi_extra_info_t info = {};
        esp_blufi_send_wifi_conn_report(WIFI_MODE_STA, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        if (self->on_configured_) {
            self->on_configured_(self->recv_ssid_, self->recv_passwd_);
        }
    }
}
