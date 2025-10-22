# 蓝牙配网功能实现方案

## 项目背景

为AlichuangTest板型添加BluFi蓝牙配网功能，允许用户通过手机小程序通过蓝牙配置WiFi连接。

## 功能需求

1. **蓝牙配网流程**：
   - 手机小程序通过蓝牙连接到ESP32端侧
   - 端侧扫描WiFi网络，将SSID列表通过蓝牙发送给小程序
   - 用户在小程序选择SSID并输入密码
   - 小程序通过蓝牙将SSID和密码发送给端侧
   - 端侧保存WiFi配置并尝试连接

2. **配置选项**：
   - 通过menuconfig配置是否启用蓝牙配网
   - 可选择使用蓝牙配网或Web配网（或同时支持）

3. **安全性**：
   - 支持DH密钥协商和AES加密
   - 可选的安全认证机制

## 技术架构分析

### 1. BluFi示例代码架构

BluFi示例代码（`C:\Users\Administrator\Downloads\blufi`）包含以下关键模块：

#### 1.1 核心文件结构
```
blufi/main/
├── blufi_example.h           # 头文件：函数声明和宏定义
├── blufi_example_main.c      # 主程序：BluFi事件处理和WiFi管理
├── blufi_init.c              # 初始化：蓝牙控制器和协议栈初始化
└── blufi_security.c          # 安全：DH密钥协商和AES加密
```

#### 1.2 关键组件功能

**blufi_example_main.c (795行)**
- WiFi初始化和事件处理
- BluFi事件回调处理
- WiFi扫描和去重过滤（仅保留2.4GHz）
- WiFi连接状态管理和上报
- SSID/密码接收和配置

**blufi_init.c (521行)**
- 支持Bluedroid和NimBLE两种蓝牙协议栈
- 蓝牙控制器初始化（硬件层）
- 蓝牙主机初始化（协议栈）
- GAP事件回调注册
- BluFi配置文件初始化

**blufi_security.c (462行)**
- Diffie-Hellman密钥交换实现
- AES-CFB128加密/解密
- MD5哈希密钥派生
- CRC16校验

#### 1.3 BluFi工作流程

```
1. 初始化阶段：
   nvs_flash_init() → initialise_wifi() → esp_blufi_controller_init()
   → esp_blufi_host_and_cb_init() → esp_blufi_adv_start()

2. 连接阶段：
   手机APP扫描 → 连接ESP32 → DH密钥协商 → 建立安全通道

3. 配网阶段：
   手机APP请求WiFi列表 → ESP32扫描WiFi → 过滤5GHz → 去重
   → 发送列表 → 接收SSID/密码 → 连接WiFi → 上报连接结果

4. 事件处理：
   ESP_BLUFI_EVENT_INIT_FINISH      # BluFi初始化完成
   ESP_BLUFI_EVENT_BLE_CONNECT      # 蓝牙连接
   ESP_BLUFI_EVENT_GET_WIFI_LIST    # 请求WiFi列表
   ESP_BLUFI_EVENT_RECV_STA_SSID    # 接收SSID
   ESP_BLUFI_EVENT_RECV_STA_PASSWD  # 接收密码
   ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP # 请求连接WiFi
   IP_EVENT_STA_GOT_IP              # 获取IP地址
```

### 2. xiaozhi-esp32 WiFi架构

#### 2.1 核心组件

**WifiBoard** (`main/boards/common/wifi_board.cc`)
- WiFi板型基类
- 管理WiFi配置模式切换
- 处理网络启动流程
- 当前支持Web配网（WifiConfigurationAp）

**WifiStation** (`managed_components/78__esp-wifi-connect/`)
- WiFi连接管理器（单例模式）
- WiFi扫描和连接队列
- 事件回调（OnConnect, OnConnected, OnScanBegin）
- 功率管理和RSSI监控

**SsidManager** (`managed_components/78__esp-wifi-connect/`)
- SSID/密码持久化存储（NVS）
- 支持多SSID管理
- 添加、删除、设置默认SSID

#### 2.2 当前配网流程

```
WifiBoard::StartNetwork() {
    1. 检查是否强制进入配置模式（force_ap标志）
    2. 检查SsidManager是否有已保存的SSID
    3. 如果有SSID：
       - WifiStation::Start()
       - WaitForConnected(60s)
       - 成功：正常运行
       - 失败：进入Web配网模式
    4. 如果没有SSID：
       - 直接进入Web配网模式（EnterWifiConfigMode）
}

EnterWifiConfigMode() {
    1. 设置设备状态为kDeviceStateWifiConfiguring
    2. 启动WifiConfigurationAp（Web服务器AP）
    3. 显示配置提示（AP SSID和Web URL）
    4. 可选：启动声波配网（CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING）
    5. 等待配置完成（阻塞循环）
}
```

## 实现方案设计

### 3. 蓝牙配网模块设计

#### 3.1 目录结构

```
main/boards/ALichuangTest/
├── bluetooth_provisioning/
│   ├── blufi_provisioning.h        # BluFi配网接口定义
│   ├── blufi_provisioning.cc       # BluFi配网实现
│   ├── blufi_callbacks.cc          # BluFi事件回调处理
│   ├── blufi_security.cc           # 安全模块（从示例移植）
│   └── CMakeLists.txt              # 组件编译配置
├── config.h
├── config.json
├── ALichuangTest.cc
└── Docs/
    └── bluetooth_provisioning_implementation.md  # 本文档
```

#### 3.2 核心类设计

**BlufiProvisioning** - 主配网类
```cpp
class BlufiProvisioning {
public:
    static BlufiProvisioning& GetInstance();

    // 启动蓝牙配网
    void Start();

    // 停止蓝牙配网
    void Stop();

    // 检查是否已完成配网
    bool IsConfigured() const;

    // 等待配网完成（超时）
    bool WaitForConfigured(int timeout_ms);

    // 设置配网完成回调
    void OnConfigured(std::function<void(const std::string& ssid, const std::string& password)> callback);

    // 设置状态改变回调
    void OnStateChanged(std::function<void(BlufiState state)> callback);

private:
    BlufiProvisioning();
    ~BlufiProvisioning();

    // 初始化蓝牙控制器
    esp_err_t InitBtController();

    // 初始化BluFi主机
    esp_err_t InitBlufiHost();

    // 反初始化
    void Deinit();

    // BluFi事件处理
    static void EventCallback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

    // WiFi事件处理
    static void WifiEventHandler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data);

    bool configured_ = false;
    EventGroupHandle_t event_group_;
    std::function<void(const std::string&, const std::string&)> on_configured_;
    std::function<void(BlufiState)> on_state_changed_;
};

enum class BlufiState {
    IDLE,           // 未启动
    ADVERTISING,    // 正在广播
    CONNECTED,      // 已连接
    SCANNING_WIFI,  // 正在扫描WiFi
    CONFIGURING,    // 正在配置
    CONNECTING,     // 正在连接WiFi
    CONNECTED_WIFI, // WiFi已连接
    FAILED          // 配网失败
};
```

#### 3.3 配网流程设计

```cpp
// 在WifiBoard中集成蓝牙配网
void WifiBoard::StartNetwork() {
    #if CONFIG_ENABLE_BLUETOOTH_PROVISIONING
    // 如果启用蓝牙配网
    if (ShouldUseBluetooth()) {
        EnterBluetoothProvisioningMode();
        return;
    }
    #endif

    // 原有Web配网逻辑
    // ...
}

void WifiBoard::EnterBluetoothProvisioningMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& blufi = BlufiProvisioning::GetInstance();

    // 设置状态改变回调
    blufi.OnStateChanged([this](BlufiState state) {
        UpdateDisplayStatus(state);
    });

    // 设置配网完成回调
    blufi.OnConfigured([this](const std::string& ssid, const std::string& password) {
        ESP_LOGI(TAG, "BluFi configured: SSID=%s", ssid.c_str());

        // 保存到SsidManager
        auto& ssid_manager = SsidManager::GetInstance();
        ssid_manager.AddSsid(ssid, password);

        // 提示配网成功
        auto& application = Application::GetInstance();
        application.Alert("配网成功", "WiFi配置已保存，设备将重启连接...",
                         "check-circle", nullptr);

        vTaskDelay(pdMS_TO_TICKS(2000));

        // 重启设备以应用新配置
        esp_restart();
    });

    // 启动BluFi配网
    blufi.Start();

    // 显示配网提示
    std::string hint = "请打开小程序，通过蓝牙连接设备\n";
    hint += "设备名称: Xiaozhi-";
    hint += SystemInfo::GetMacAddress().substr(12, 5);
    application.Alert("蓝牙配网模式", hint.c_str(), "bluetooth", nullptr);

    // 等待配网完成（阻塞）
    if (!blufi.WaitForConfigured(300000)) {  // 5分钟超时
        ESP_LOGW(TAG, "BluFi provisioning timeout");
        blufi.Stop();

        // 超时后可选择进入Web配网
        #if CONFIG_BLUFI_FALLBACK_TO_WEB_CONFIG
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        #endif
    }
}
```

### 4. 详细实现步骤

#### 步骤1：创建BluFi配网模块基础结构

**文件**: `main/boards/ALichuangTest/bluetooth_provisioning/blufi_provisioning.h`

```cpp
#pragma once

#include <string>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_blufi_api.h>

enum class BlufiState {
    IDLE,
    ADVERTISING,
    CONNECTED,
    SCANNING_WIFI,
    CONFIGURING,
    CONNECTING,
    CONNECTED_WIFI,
    FAILED
};

class BlufiProvisioning {
public:
    static BlufiProvisioning& GetInstance();

    void Start();
    void Stop();
    bool IsConfigured() const { return configured_; }
    bool WaitForConfigured(int timeout_ms);

    void OnConfigured(std::function<void(const std::string& ssid, const std::string& password)> callback);
    void OnStateChanged(std::function<void(BlufiState state)> callback);

private:
    BlufiProvisioning();
    ~BlufiProvisioning();
    BlufiProvisioning(const BlufiProvisioning&) = delete;
    BlufiProvisioning& operator=(const BlufiProvisioning&) = delete;

    esp_err_t InitBtController();
    esp_err_t InitBlufiHost();
    void Deinit();

    static void EventCallback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);
    static void WifiEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

    bool configured_;
    BlufiState state_;
    EventGroupHandle_t event_group_;
    std::string configured_ssid_;
    std::string configured_password_;

    std::function<void(const std::string&, const std::string&)> on_configured_;
    std::function<void(BlufiState)> on_state_changed_;

    static constexpr int CONFIGURED_BIT = BIT0;
    static constexpr int FAILED_BIT = BIT1;
};
```

**文件**: `main/boards/ALichuangTest/bluetooth_provisioning/blufi_provisioning.cc`

```cpp
#include "blufi_provisioning.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_blufi_api.h>
#include <esp_event.h>
#include <nvs_flash.h>

static const char *TAG = "BlufiProvisioning";

// 前向声明安全函数（在blufi_security.cc中实现）
extern void blufi_dh_negotiate_data_handler(uint8_t *data, int len, uint8_t **output_data,
                                           int *output_len, bool *need_free);
extern int blufi_aes_encrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
extern int blufi_aes_decrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
extern uint16_t blufi_crc_checksum(uint8_t iv8, uint8_t *data, int len);
extern int blufi_security_init(void);
extern void blufi_security_deinit(void);

// BluFi回调函数配置
static esp_blufi_callbacks_t blufi_callbacks = {
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

BlufiProvisioning::BlufiProvisioning()
    : configured_(false), state_(BlufiState::IDLE) {
    event_group_ = xEventGroupCreate();
}

BlufiProvisioning::~BlufiProvisioning() {
    if (event_group_) {
        vEventGroupDelete(event_group_);
    }
}

void BlufiProvisioning::Start() {
    ESP_LOGI(TAG, "Starting BluFi provisioning");

    // 初始化蓝牙控制器
    if (InitBtController() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BT controller");
        state_ = BlufiState::FAILED;
        if (on_state_changed_) on_state_changed_(state_);
        return;
    }

    // 初始化BluFi主机
    if (InitBlufiHost() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BluFi host");
        state_ = BlufiState::FAILED;
        if (on_state_changed_) on_state_changed_(state_);
        return;
    }

    state_ = BlufiState::ADVERTISING;
    if (on_state_changed_) on_state_changed_(state_);

    ESP_LOGI(TAG, "BluFi started, waiting for connections");
}

void BlufiProvisioning::Stop() {
    ESP_LOGI(TAG, "Stopping BluFi provisioning");
    Deinit();
    state_ = BlufiState::IDLE;
    if (on_state_changed_) on_state_changed_(state_);
}

bool BlufiProvisioning::WaitForConfigured(int timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(
        event_group_,
        CONFIGURED_BIT | FAILED_BIT,
        pdTRUE,  // 清除位
        pdFALSE, // 等待任意一个位
        pdMS_TO_TICKS(timeout_ms)
    );

    return (bits & CONFIGURED_BIT) != 0;
}

void BlufiProvisioning::OnConfigured(
    std::function<void(const std::string&, const std::string&)> callback) {
    on_configured_ = callback;
}

void BlufiProvisioning::OnStateChanged(std::function<void(BlufiState)> callback) {
    on_state_changed_ = callback;
}

esp_err_t BlufiProvisioning::InitBtController() {
#if CONFIG_IDF_TARGET_ESP32
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
#endif

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize BT controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable BT controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BT controller initialized");
    return ESP_OK;
}

esp_err_t BlufiProvisioning::InitBlufiHost() {
    // 这里需要根据配置选择Bluedroid或NimBLE
    // 示例使用Bluedroid

#ifdef CONFIG_BT_BLUEDROID_ENABLED
    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bluedroid_init_with_cfg(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册BluFi回调
    ret = esp_blufi_register_callbacks(&blufi_callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BluFi register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册GAP回调
    ret = esp_ble_gap_register_callback(esp_blufi_gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化BluFi profile
    ret = esp_blufi_profile_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BluFi profile init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BluFi host initialized");
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Bluedroid not enabled");
    return ESP_FAIL;
#endif
}

void BlufiProvisioning::Deinit() {
    // 反初始化BluFi
    esp_blufi_profile_deinit();

#ifdef CONFIG_BT_BLUEDROID_ENABLED
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
#endif

    esp_bt_controller_disable();
    esp_bt_controller_deinit();
}

// BluFi事件回调处理（在blufi_callbacks.cc中实现详细逻辑）
void BlufiProvisioning::EventCallback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param) {
    auto& instance = GetInstance();

    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        ESP_LOGI(TAG, "BluFi init finish");
        esp_ble_gap_set_device_name("Xiaozhi-ESP32");
        esp_blufi_adv_start();
        break;

    case ESP_BLUFI_EVENT_BLE_CONNECT:
        ESP_LOGI(TAG, "BluFi BLE connected");
        instance.state_ = BlufiState::CONNECTED;
        if (instance.on_state_changed_) instance.on_state_changed_(instance.state_);
        esp_blufi_adv_stop();
        blufi_security_init();
        break;

    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        ESP_LOGI(TAG, "BluFi BLE disconnected");
        instance.state_ = BlufiState::ADVERTISING;
        if (instance.on_state_changed_) instance.on_state_changed_(instance.state_);
        blufi_security_deinit();
        esp_blufi_adv_start();
        break;

    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        // 接收SSID
        if (param->sta_ssid.ssid_len < 32) {
            instance.configured_ssid_.assign(
                (char*)param->sta_ssid.ssid,
                param->sta_ssid.ssid_len
            );
            ESP_LOGI(TAG, "Received SSID: %s", instance.configured_ssid_.c_str());
        }
        break;

    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        // 接收密码
        if (param->sta_passwd.passwd_len < 64) {
            instance.configured_password_.assign(
                (char*)param->sta_passwd.passwd,
                param->sta_passwd.passwd_len
            );
            ESP_LOGI(TAG, "Received password (length: %d)", instance.configured_password_.length());
        }
        break;

    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        ESP_LOGI(TAG, "Request to connect to AP");
        instance.state_ = BlufiState::CONFIGURING;
        if (instance.on_state_changed_) instance.on_state_changed_(instance.state_);

        // 触发配网完成回调
        if (instance.on_configured_ &&
            !instance.configured_ssid_.empty()) {
            instance.configured_ = true;
            xEventGroupSetBits(instance.event_group_, CONFIGURED_BIT);
            instance.on_configured_(instance.configured_ssid_, instance.configured_password_);
        }
        break;

    case ESP_BLUFI_EVENT_GET_WIFI_LIST:
        // WiFi扫描请求（详细实现见blufi_callbacks.cc）
        ESP_LOGI(TAG, "Request WiFi list");
        instance.state_ = BlufiState::SCANNING_WIFI;
        if (instance.on_state_changed_) instance.on_state_changed_(instance.state_);
        // TODO: 实现WiFi扫描和列表发送
        break;

    default:
        break;
    }
}
```

#### 步骤2：移植安全模块

**文件**: `main/boards/ALichuangTest/bluetooth_provisioning/blufi_security.cc`

这个文件直接从blufi示例的`blufi_security.c`移植，保持DH密钥协商、AES加密/解密和CRC校验功能不变。主要改动：
- 文件扩展名改为`.cc`（C++）
- 添加`extern "C"`包装（如果需要）
- 确保包含正确的头文件路径

（完整代码参考blufi示例的blufi_security.c，此处省略）

#### 步骤3：实现WiFi扫描回调

**文件**: `main/boards/ALichuangTest/bluetooth_provisioning/blufi_callbacks.cc`

```cpp
#include "blufi_provisioning.h"
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_blufi_api.h>

static const char *TAG = "BlufiCallbacks";

// WiFi扫描完成事件处理
static void HandleWifiScanDone() {
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGI(TAG, "No WiFi AP found");
        return;
    }

    // 分配内存存储扫描结果
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        ESP_LOGE(TAG, "malloc failed for ap_list");
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));

    // 过滤和去重（保留2.4GHz，去除重复SSID）
    esp_blufi_ap_record_t *blufi_ap_list = (esp_blufi_ap_record_t *)malloc(
        ap_count * sizeof(esp_blufi_ap_record_t));
    if (!blufi_ap_list) {
        free(ap_list);
        ESP_LOGE(TAG, "malloc failed for blufi_ap_list");
        return;
    }

    uint16_t filtered_count = 0;
    for (int i = 0; i < ap_count; i++) {
        // 只保留2.4GHz频段（信道1-14）
        if (ap_list[i].primary >= 1 && ap_list[i].primary <= 14) {
            // 检查是否重复
            bool duplicate = false;
            for (int j = 0; j < filtered_count; j++) {
                if (memcmp(blufi_ap_list[j].ssid, ap_list[i].ssid, 32) == 0) {
                    // 保留信号强的
                    if (ap_list[i].rssi > blufi_ap_list[j].rssi) {
                        blufi_ap_list[j].rssi = ap_list[i].rssi;
                    }
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                blufi_ap_list[filtered_count].rssi = ap_list[i].rssi;
                memcpy(blufi_ap_list[filtered_count].ssid, ap_list[i].ssid, 32);
                filtered_count++;
            }
        }
    }

    ESP_LOGI(TAG, "Sending %d WiFi networks to phone", filtered_count);
    esp_blufi_send_wifi_list(filtered_count, blufi_ap_list);

    free(blufi_ap_list);
    free(ap_list);
}

// 在BluFi事件回调中处理WiFi扫描请求
void ProcessWifiScanRequest() {
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);  // 阻塞式扫描
    if (ret == ESP_OK) {
        HandleWifiScanDone();
    } else {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
    }
}
```

#### 步骤4：在WifiBoard中集成

**文件**: `main/boards/common/wifi_board.cc` (修改)

```cpp
#include "wifi_board.h"

#if CONFIG_ENABLE_BLUETOOTH_PROVISIONING
#include "boards/ALichuangTest/bluetooth_provisioning/blufi_provisioning.h"
#endif

// 在StartNetwork()方法中添加蓝牙配网选项
void WifiBoard::StartNetwork() {
#if CONFIG_ENABLE_BLUETOOTH_PROVISIONING
    Settings settings("wifi", true);
    bool use_bluetooth = settings.GetInt("use_bluetooth_provisioning") == 1;

    if (use_bluetooth && wifi_config_mode_) {
        EnterBluetoothProvisioningMode();
        return;
    }
#endif

    // 原有逻辑保持不变
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
        return;
    }

    // ... 其余代码不变
}

#if CONFIG_ENABLE_BLUETOOTH_PROVISIONING
void WifiBoard::EnterBluetoothProvisioningMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& blufi = BlufiProvisioning::GetInstance();

    // 设置状态改变回调
    blufi.OnStateChanged([this](BlufiState state) {
        auto display = Board::GetInstance().GetDisplay();
        switch (state) {
        case BlufiState::ADVERTISING:
            display->ShowNotification("蓝牙配网: 等待连接...", 30000);
            break;
        case BlufiState::CONNECTED:
            display->ShowNotification("蓝牙已连接", 3000);
            break;
        case BlufiState::SCANNING_WIFI:
            display->ShowNotification("正在扫描WiFi...", 10000);
            break;
        case BlufiState::CONFIGURING:
            display->ShowNotification("正在配置WiFi...", 10000);
            break;
        default:
            break;
        }
    });

    // 设置配网完成回调
    blufi.OnConfigured([this](const std::string& ssid, const std::string& password) {
        ESP_LOGI(TAG, "BluFi configured: SSID=%s", ssid.c_str());

        // 保存到SsidManager
        auto& ssid_manager = SsidManager::GetInstance();
        ssid_manager.AddSsid(ssid, password);

        // 提示配网成功
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification("配网成功，重启中...", 3000);

        vTaskDelay(pdMS_TO_TICKS(2000));

        // 重启设备
        esp_restart();
    });

    // 启动BluFi配网
    blufi.Start();

    // 显示配网提示
    std::string hint = "请打开小程序\n通过蓝牙连接设备配置WiFi";
    application.Alert("蓝牙配网模式", hint.c_str(), "bluetooth", nullptr);

    // 等待配网完成（5分钟超时）
    if (!blufi.WaitForConfigured(300000)) {
        ESP_LOGW(TAG, "BluFi provisioning timeout");
        blufi.Stop();

        // 超时后回退到Web配网
        #if CONFIG_BLUFI_FALLBACK_TO_WEB
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        #endif
    }
}
#endif
```

#### 步骤5：添加Kconfig配置

**文件**: `main/boards/ALichuangTest/Kconfig.projbuild` (新建或修改)

```kconfig
menu "Bluetooth Provisioning Configuration"

config ENABLE_BLUETOOTH_PROVISIONING
    bool "Enable Bluetooth Provisioning"
    default n
    help
        Enable BluFi Bluetooth provisioning for WiFi configuration.
        When enabled, users can configure WiFi via Bluetooth using a mobile app.

config BLUFI_DEVICE_NAME
    string "BluFi Device Name"
    depends on ENABLE_BLUETOOTH_PROVISIONING
    default "Xiaozhi-ESP32"
    help
        The Bluetooth device name that will be advertised during provisioning.

config BLUFI_FALLBACK_TO_WEB
    bool "Fallback to Web Config on Timeout"
    depends on ENABLE_BLUETOOTH_PROVISIONING
    default y
    help
        If Bluetooth provisioning times out, automatically fall back to Web configuration mode.

config BLUFI_PROVISIONING_TIMEOUT
    int "Provisioning Timeout (seconds)"
    depends on ENABLE_BLUETOOTH_PROVISIONING
    default 300
    range 60 600
    help
        Maximum time to wait for Bluetooth provisioning to complete.

choice BT_STACK_SELECTION
    prompt "Bluetooth Stack"
    depends on ENABLE_BLUETOOTH_PROVISIONING
    default USE_BLUEDROID_STACK
    help
        Select which Bluetooth stack to use for BluFi.

config USE_BLUEDROID_STACK
    bool "Bluedroid"
    help
        Use the Bluedroid Bluetooth stack (recommended for ESP32/ESP32-S3).

config USE_NIMBLE_STACK
    bool "NimBLE"
    help
        Use the NimBLE Bluetooth stack (lighter weight, for ESP32-C3/C6).

endchoice

endmenu
```

#### 步骤6：更新CMakeLists.txt

**文件**: `main/boards/ALichuangTest/bluetooth_provisioning/CMakeLists.txt` (新建)

```cmake
idf_component_register(
    SRCS "blufi_provisioning.cc"
         "blufi_callbacks.cc"
         "blufi_security.cc"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event nvs_flash bt esp_blufi mbedtls
)

# 只在启用蓝牙配网时编译
if(NOT CONFIG_ENABLE_BLUETOOTH_PROVISIONING)
    target_compile_options(${COMPONENT_LIB} PRIVATE -DCONFIG_ENABLE_BLUETOOTH_PROVISIONING=0)
endif()
```

**文件**: `main/CMakeLists.txt` (修改)

```cmake
# 添加蓝牙配网组件
if(CONFIG_ENABLE_BLUETOOTH_PROVISIONING)
    set(EXTRA_COMPONENT_DIRS "${EXTRA_COMPONENT_DIRS}"
        "${CMAKE_CURRENT_SOURCE_DIR}/boards/ALichuangTest/bluetooth_provisioning")
endif()

# ... 其余内容
```

### 5. 配置和使用方法

#### 5.1 使能蓝牙配网

```bash
# 进入项目目录
cd xiaozhi-esp32

# 打开menuconfig
idf.py menuconfig

# 导航到: Bluetooth Provisioning Configuration
# -> [*] Enable Bluetooth Provisioning
# -> Device Name: Xiaozhi-ESP32
# -> [*] Fallback to Web Config on Timeout
# -> Provisioning Timeout: 300 seconds
# -> Bluetooth Stack: Bluedroid

# 保存并退出
```

#### 5.2 编译和烧录

```bash
# 清理并重新编译
idf.py clean
idf.py build

# 烧录到设备
idf.py -p COM3 flash monitor
```

#### 5.3 使用流程

1. **首次启动或重置WiFi配置**：
   - 设备检测到没有保存的WiFi配置
   - 自动进入蓝牙配网模式
   - 屏幕显示"蓝牙配网模式"和设备名称

2. **手机小程序连接**：
   - 打开小程序，扫描蓝牙设备
   - 找到"Xiaozhi-ESP32"设备并连接
   - 小程序请求WiFi列表

3. **配置WiFi**：
   - 设备扫描周围WiFi网络
   - 过滤5GHz和重复SSID
   - 发送WiFi列表到小程序
   - 用户选择SSID并输入密码
   - 小程序通过蓝牙发送配置

4. **连接和保存**：
   - 设备接收SSID和密码
   - 保存到SsidManager（NVS存储）
   - 设备重启并尝试连接WiFi

5. **超时处理**：
   - 如果5分钟内未完成配网
   - 自动回退到Web配网模式（如果启用）

### 6. 小程序开发建议

#### 6.1 蓝牙通信协议

小程序需要实现BluFi协议的客户端部分，主要步骤：

1. **蓝牙初始化和扫描**
```javascript
// 初始化蓝牙适配器
wx.openBluetoothAdapter({
  success: (res) => {
    console.log('蓝牙适配器初始化成功');
    // 开始扫描
    startBlufiScan();
  }
});

// 扫描BluFi设备
function startBlufiScan() {
  wx.startBluetoothDevicesDiscovery({
    services: ['0000FFFF-0000-1000-8000-00805F9B34FB'], // BluFi Service UUID
    success: (res) => {
      console.log('开始扫描BluFi设备');
    }
  });

  // 监听发现设备
  wx.onBluetoothDeviceFound((res) => {
    res.devices.forEach(device => {
      if (device.name && device.name.startsWith('Xiaozhi')) {
        console.log('发现设备:', device);
        // 显示在设备列表
      }
    });
  });
}
```

2. **连接设备**
```javascript
function connectBlufiDevice(deviceId) {
  wx.createBLEConnection({
    deviceId: deviceId,
    success: (res) => {
      console.log('连接成功');
      // 获取服务和特征值
      getBlufiServices(deviceId);
    }
  });
}

function getBlufiServices(deviceId) {
  wx.getBLEDeviceServices({
    deviceId: deviceId,
    success: (res) => {
      // 查找BluFi服务
      const blufiService = res.services.find(s =>
        s.uuid.toUpperCase().includes('FFFF'));

      // 获取特征值
      wx.getBLEDeviceCharacteristics({
        deviceId: deviceId,
        serviceId: blufiService.uuid,
        success: (res) => {
          // 保存特征值用于后续通信
        }
      });
    }
  });
}
```

3. **WiFi列表请求**
```javascript
function requestWifiList(deviceId, serviceId, charId) {
  // 构造BluFi GET_WIFI_LIST请求包
  const packet = buildBlufiPacket({
    type: 0x03, // GET_WIFI_LIST
    frameCtrl: 0x00
  });

  wx.writeBLECharacteristicValue({
    deviceId: deviceId,
    serviceId: serviceId,
    characteristicId: charId,
    value: packet,
    success: (res) => {
      console.log('请求WiFi列表成功');
    }
  });

  // 监听WiFi列表响应
  wx.onBLECharacteristicValueChange((res) => {
    const wifiList = parseBlufiWifiList(res.value);
    // 显示WiFi列表供用户选择
  });
}
```

4. **发送SSID和密码**
```javascript
function sendWifiCredentials(deviceId, serviceId, charId, ssid, password) {
  // 发送SSID
  const ssidPacket = buildBlufiPacket({
    type: 0x05, // SET_STA_SSID
    data: ssid
  });

  wx.writeBLECharacteristicValue({
    deviceId: deviceId,
    serviceId: serviceId,
    characteristicId: charId,
    value: ssidPacket
  });

  // 发送密码
  const passwordPacket = buildBlufiPacket({
    type: 0x06, // SET_STA_PASSWORD
    data: password
  });

  wx.writeBLECharacteristicValue({
    deviceId: deviceId,
    serviceId: serviceId,
    characteristicId: charId,
    value: passwordPacket
  });

  // 请求连接
  const connectPacket = buildBlufiPacket({
    type: 0x0A // CONNECT_TO_AP
  });

  wx.writeBLECharacteristicValue({
    deviceId: deviceId,
    serviceId: serviceId,
    characteristicId: charId,
    value: connectPacket,
    success: (res) => {
      console.log('配网请求已发送');
      // 等待连接结果
    }
  });
}
```

#### 6.2 BluFi协议包格式

BluFi协议使用特定的数据包格式：

```
+--------+--------+--------+--------+--------+----------+
| Type   | Frame  | Seq    | Data   |  Data  | Checksum |
|        | Ctrl   | Number | Length |        | (opt)    |
+--------+--------+--------+--------+--------+----------+
| 1 byte | 1 byte | 1 byte | 1 byte | N bytes| 2 bytes  |
```

主要Type值：
- `0x00`: Negotiate Data (DH密钥协商)
- `0x03`: Get WiFi List Request
- `0x04`: Get WiFi List Response
- `0x05`: Set STA SSID
- `0x06`: Set STA Password
- `0x0A`: Connect to AP Request
- `0x0B`: WiFi Connection Report

小程序需要实现完整的BluFi协议栈，或使用现有的BluFi小程序SDK。

### 7. 测试验证

#### 7.1 功能测试清单

- [ ] 蓝牙广播正常，设备名称正确
- [ ] 小程序可以扫描并连接到设备
- [ ] WiFi扫描返回正确的网络列表
- [ ] 5GHz网络被正确过滤
- [ ] 重复SSID被去重，保留信号最强的
- [ ] SSID和密码可以正确接收和保存
- [ ] WiFi连接成功后设备自动重启
- [ ] 配网超时后正确回退到Web配网
- [ ] 多次配网流程稳定性测试
- [ ] 内存泄漏测试

#### 7.2 日志监控

关键日志点：
```
I (xxx) BlufiProvisioning: Starting BluFi provisioning
I (xxx) BlufiProvisioning: BT controller initialized
I (xxx) BlufiProvisioning: BluFi host initialized
I (xxx) BlufiProvisioning: BluFi init finish
I (xxx) BlufiProvisioning: BluFi BLE connected
I (xxx) BlufiCallbacks: Sending 5 WiFi networks to phone
I (xxx) BlufiProvisioning: Received SSID: MyWiFi
I (xxx) BlufiProvisioning: Received password (length: 12)
I (xxx) BlufiProvisioning: Request to connect to AP
I (xxx) WifiBoard: BluFi configured: SSID=MyWiFi
```

#### 7.3 异常处理测试

- [ ] 蓝牙初始化失败
- [ ] WiFi扫描失败
- [ ] 接收到无效的SSID/密码
- [ ] WiFi连接超时
- [ ] WiFi密码错误
- [ ] 蓝牙连接中断
- [ ] 内存分配失败

### 8. 性能优化

#### 8.1 内存优化

- 使用动态内存分配，及时释放
- WiFi扫描结果处理后立即释放内存
- 限制WiFi列表最大数量（如50个）

#### 8.2 功耗优化

- 配网完成后立即关闭蓝牙
- WiFi扫描使用主动扫描模式（更快）
- 连接成功后立即停止蓝牙广播

#### 8.3 用户体验优化

- 实时显示配网进度和状态
- 提供清晰的错误提示
- 支持配网过程取消和重试
- 添加配网历史记录（可选）

### 9. 安全性考虑

#### 9.1 数据加密

- 启用DH密钥协商
- 使用AES-CFB128加密SSID和密码
- 使用CRC16校验数据完整性

#### 9.2 认证机制（可选）

- 添加设备识别码验证
- 支持用户自定义PIN码
- 限制配网尝试次数

#### 9.3 存储安全

- 使用NVS加密存储WiFi密码
- 定期清理未使用的配置
- 支持恢复出厂设置

### 10. 故障排查

#### 10.1 常见问题

**问题1：小程序扫描不到设备**
- 检查蓝牙是否初始化成功
- 检查广播是否启动（日志中确认）
- 检查设备名称是否正确
- 尝试重启蓝牙适配器

**问题2：WiFi列表为空**
- 检查WiFi是否已初始化
- 检查扫描是否成功（返回值）
- 检查是否所有网络都被过滤了
- 增加日志输出扫描结果

**问题3：配网后无法连接WiFi**
- 检查SSID和密码是否正确保存
- 检查SsidManager是否正常工作
- 检查WiFi认证模式是否支持
- 手动测试WiFi连接

**问题4：内存不足**
- 减少WiFi扫描结果数量
- 检查内存泄漏
- 优化缓冲区大小
- 使用堆栈监控工具

#### 10.2 调试工具

- ESP-IDF Monitor: 实时查看日志
- 蓝牙抓包工具: 分析蓝牙通信
- nRF Connect: 测试BLE连接
- ESP32调试器: GDB调试

### 11. 后续扩展

#### 11.1 功能扩展

- 支持企业级WiFi（EAP认证）
- 支持WiFi Mesh网络配网
- 支持多设备批量配网
- 添加配网记录云同步

#### 11.2 多平台支持

- 开发iOS小程序版本
- 开发Android原生应用
- 开发H5网页配网
- 支持微信小程序

#### 11.3 协议优化

- 实现BluFi协议v2
- 支持更快的配网速度
- 减少数据传输量
- 优化错误重传机制

## 12. 参考资料

### 官方文档
- [ESP-IDF BluFi Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/blufi.html)
- [ESP32 Bluetooth API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/index.html)
- [BluFi Protocol Specification](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/blufi/main/blufi_protocol.md)

### 示例代码
- [ESP-IDF BluFi Example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/blufi)
- [EspBlufi Android App](https://github.com/EspressifApp/EspBlufi)
- [EspBlufi iOS App](https://github.com/EspressifApp/EspBlufiForiOS)

### 相关组件
- xiaozhi-esp32项目文档: `D:\AIDev\xiaozhi-esp32\README.md`
- WifiBoard实现: `main/boards/common/wifi_board.cc`
- SsidManager实现: `managed_components/78__esp-wifi-connect/`

## 13. 总结

本文档详细说明了在xiaozhi-esp32项目的AlichuangTest板型中实现BluFi蓝牙配网功能的完整方案，包括：

1. **架构设计**：模块化设计，最小化对现有代码的影响
2. **实现步骤**：分步骤详细说明每个文件的实现
3. **配置方法**：通过menuconfig灵活配置
4. **使用流程**：完整的用户配网流程
5. **小程序开发**：提供小程序端开发指导
6. **测试验证**：全面的测试清单和方法
7. **优化建议**：性能和用户体验优化
8. **故障排查**：常见问题和解决方案

按照本文档实施，可以为xiaozhi-esp32项目添加完整的蓝牙配网功能，提供更便捷的WiFi配置体验。
