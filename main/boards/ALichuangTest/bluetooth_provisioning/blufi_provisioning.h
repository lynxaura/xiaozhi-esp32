// Minimal BluFi provisioning interface for ALichuangTest board
#pragma once

#include <string>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>

extern "C" {
#include <esp_blufi_api.h>
}

enum class BlufiState {
    IDLE,
    ADVERTISING,
    CONNECTED,
    SCANNING_WIFI,
    CONFIGURING,
    CONNECTING,
    CONNECTED_WIFI,
    FAILED,
};

class BlufiProvisioning {
public:
    static BlufiProvisioning& GetInstance();

    void Start();
    void Stop();

    bool IsConfigured() const { return configured_; }
    bool WaitForConfigured(int timeout_ms);

    void OnConfigured(std::function<void(const std::string& ssid, const std::string& password)> cb) { on_configured_ = std::move(cb); }
    void OnStateChanged(std::function<void(BlufiState)> cb) { on_state_changed_ = std::move(cb); }

    // BLUFI callbacks need external visibility for registration
    static void EventCallback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

private:
    BlufiProvisioning();
    ~BlufiProvisioning();
    BlufiProvisioning(const BlufiProvisioning&) = delete;
    BlufiProvisioning& operator=(const BlufiProvisioning&) = delete;

    // Internal helpers
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void ScanTimeoutCallback(void* arg);

    void SetState(BlufiState s);
    void InitWifiIfNeeded();
    void StartScan();
    void StopScanTimer();
    void ConnectSta();

private:
    bool configured_ = false;
    bool started_ = false;
    bool ble_connected_ = false;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t scan_timeout_timer_ = nullptr;
    std::function<void(const std::string&, const std::string&)> on_configured_;
    std::function<void(BlufiState)> on_state_changed_;

    // temp creds
    std::string recv_ssid_;
    std::string recv_passwd_;
};
