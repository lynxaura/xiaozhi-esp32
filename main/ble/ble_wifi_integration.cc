/*
 * 蓝牙WiFi配网集成示例
 * 
 * 本文件展示了如何将蓝牙配网功能集成到xiaozhi-esp32项目中
 * 将此代码集成到application.cc中，可以在设备启动时自动启用蓝牙配网
 */

#include "ble_wifi_integration.h"
#include "ble_wifi_config.h"
// #include "ble_ota.h"  // 禁用BLE OTA以节省内存
#include "esp_log.h"
#include "wifi_configuration_ap.h"
#include "board.h"
#include "esp_timer.h"
static const char* TAG = "BLE_WIFI_INTEGRATION";

/*
 * 建议的集成方案：
 * 
 * 1. 在Application类中添加蓝牙配网相关的成员函数和变量
 * 2. 在Application::Start()中启动蓝牙配网功能
 * 3. 当WiFi配置改变时，触发WiFi连接
 */

namespace BleWifiIntegration {

// 静态变量，用于跟踪蓝牙配网状态
static bool ble_wifi_config_active = false;

static esp_timer_handle_t clock_timer_handle_ = nullptr;
// 前向声明
void StopBleWifiConfig();

// WiFi配置改变回调函数
static void OnWifiConfigChanged(const std::string& ssid, const std::string& password) {
    ESP_LOGI(TAG, "BLE WiFi config changed - SSID: %s", ssid.c_str());

    // 现在WiFi已经通过board.StartNetwork()初始化，可以直接连接
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    bool connected = wifi_ap.ConnectToWifi(ssid, password);

    if (connected) {
        ESP_LOGI(TAG, "Successfully connected to WiFi: %s", ssid.c_str());

        // 连接成功后，停止蓝牙配网以节省资源
        StopBleWifiConfig();

        // BLE OTA服务已禁用以节省内存
        ESP_LOGI(TAG, "BLE OTA service disabled to save memory");

        // 恢复非关键任务
        auto& board = Board::GetInstance();
        ESP_LOGI(TAG, "Resuming non-essential tasks after successful WiFi connection");
        esp_err_t resume_ret = board.ResumeNonEssentialTasks();
        if (resume_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to resume some tasks: %d", resume_ret);
        }

        ESP_LOGI(TAG, "WiFi configuration completed successfully");
    } else {
        ESP_LOGW(TAG, "Failed to connect to WiFi: %s", ssid.c_str());
    }
}

static void update_adv(void){
    auto& ble_wifi_config = BleWifiConfig::GetInstance();
    if(!ble_wifi_config_active || ble_wifi_config.IsConnected()){
        return;
    }
    static int last_battery_level = -1;
    static bool last_charging = false;

    int battery_level = 0;
    bool charging = false, discharging = false;
    auto& board = Board::GetInstance();
    board.GetBatteryLevel(battery_level, charging, discharging);
    
    if(battery_level == last_battery_level && charging == last_charging){
        return;
    }

    last_battery_level = battery_level;
    last_charging = charging;

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    std::string ap_ssid = wifi_ap.GetSsid();
    
    ble_wifi_config.StopAdvertising();
    vTaskDelay(pdMS_TO_TICKS(100));
    ble_wifi_config.StartAdvertising(ap_ssid, battery_level, charging);

    ESP_LOGI(TAG, "Advertising name: lxzn_wificfg-%s", ap_ssid.c_str());
}

// 启动蓝牙配网功能
bool StartBleWifiConfig() {
    if (ble_wifi_config_active) {
        ESP_LOGW(TAG, "BLE WiFi config already active");
        return true;
    }

    ESP_LOGI(TAG, "Starting BLE WiFi configuration service");

    // 注意：任务暂停已在application.cc中预防性调用，此处不重复执行
    ESP_LOGI(TAG, "Tasks already suspended by application, proceeding with BLE initialization");

    // 获取BLE WiFi配置实例
    auto& ble_wifi_config = BleWifiConfig::GetInstance();

    // 初始化蓝牙配网功能
    if (!ble_wifi_config.Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize BLE WiFi config");

        // BLE初始化失败，恢复已暂停的任务
        auto& board = Board::GetInstance();
        ESP_LOGI(TAG, "Resuming non-essential tasks due to BLE initialization failure");
        esp_err_t resume_ret = board.ResumeNonEssentialTasks();
        if (resume_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to resume some tasks after BLE failure: %d", resume_ret);
        }

        return false;
    }
    
    // 设置WiFi配置改变回调
    ble_wifi_config.SetOnWifiConfigChanged(OnWifiConfigChanged);

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            update_adv();
        },
        .name = "update_adv",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
    
    ble_wifi_config_active = true;

    update_adv();

    esp_timer_start_periodic(clock_timer_handle_, 5000000); // 每10秒

    ESP_LOGI(TAG, "BLE WiFi configuration started successfully");
    
    
    return true;
}

// 停止蓝牙配网功能
void StopBleWifiConfig() {
    if (!ble_wifi_config_active) {
        return;
    }

    ESP_LOGI(TAG, "Stopping BLE WiFi configuration service");

    auto& ble_wifi_config = BleWifiConfig::GetInstance();
    ble_wifi_config.Disconnect();
    ble_wifi_config.StopAdvertising();
    ble_wifi_config.Deinitialize();

    // BLE OTA服务已禁用以节省内存
    ESP_LOGI(TAG, "BLE OTA service was disabled to save memory");

    ble_wifi_config_active = false;

    // 等待BLE栈完全清理释放内存，避免内存碎片影响后续任务
    ESP_LOGI(TAG, "Waiting for BLE stack cleanup to complete...");
    vTaskDelay(pdMS_TO_TICKS(500)); // 等待500ms让BLE栈完全清理

    // 强制进行一次垃圾回收以减少内存碎片
    ESP_LOGI(TAG, "Triggering memory compaction to reduce fragmentation");

    // 恢复非关键任务
    auto& board = Board::GetInstance();
    ESP_LOGI(TAG, "Resuming non-essential tasks after BLE WiFi config stopped");
    esp_err_t resume_ret = board.ResumeNonEssentialTasks();
    if (resume_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to resume some tasks: %d", resume_ret);
    }

    ESP_LOGI(TAG, "BLE WiFi configuration stopped");
}

// 检查蓝牙配网是否活跃
bool IsBleWifiConfigActive() {
    return ble_wifi_config_active;
}

} // namespace BleWifiIntegration

