# BluFi 蓝牙配网技术文档

## 概述

本文档描述 ALichuangTest 板型的 BluFi 蓝牙配网功能的技术架构、实现原理和集成方式。BluFi 是 Espressif 提供的基于蓝牙低功耗 (BLE) 的 WiFi 配网协议，允许用户通过手机应用为设备配置 WiFi 凭证。

**适用范围：** 开发人员、系统架构师、技术评审

**技术栈：** ESP-IDF 5.5+, NimBLE 蓝牙协议栈, C++17

---

## 1. 功能特性

### 1.1 核心功能

- **蓝牙 WiFi 配网**：通过 BLE 连接传输 WiFi SSID 和密码
- **WiFi 网络扫描**：扫描周围可用 WiFi 网络并发送给手机端
- **持久化存储**：WiFi 凭证保存到 NVS (非易失性存储)
- **安全通信**：支持 DH 密钥交换和 AES 加密
- **自动重启连接**：配网成功后自动重启并连接 WiFi

### 1.2 配置选项 (menuconfig)

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `CONFIG_ENABLE_BLUETOOTH_PROVISIONING` | 启用蓝牙配网功能 | `n` |
| `CONFIG_BLUETOOTH_PROVISIONING_TIMEOUT` | 配网超时时间（秒） | `300` |
| `CONFIG_BLUETOOTH_PROVISIONING_ALWAYS_ON_BOOT` | 每次启动都进入配网模式（测试用） | `n` |
| `CONFIG_BLUFI_FALLBACK_TO_WEB_CONFIG` | 超时后回退到 Web 配网 | `y` |

---

## 2. 系统架构

### 2.1 模块组成

```
bluetooth_provisioning/
├── blufi_provisioning.h/cc    # 主配网逻辑和事件处理 (~270 行)
├── blufi_init.c                # NimBLE/Bluedroid 协议栈初始化 (~200 行)
└── blufi_security.c            # 安全模块：DH/AES/CRC (~230 行)
```

**总代码量：** ~770 行

### 2.2 架构层次

```
┌─────────────────────────────────────────────┐
│  Application Layer (ALichuangTest.cc)       │
│  - StartNetwork() 入口                       │
│  - 配网完成回调处理                           │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│  BluFi Provisioning Layer                   │
│  (blufi_provisioning.cc)                    │
│  - 配网状态管理                               │
│  - WiFi 扫描和连接                            │
│  - 事件回调分发                               │
└──────────────┬──────────────┬───────────────┘
               │              │
     ┌─────────▼─────┐   ┌────▼──────────┐
     │  NimBLE Stack │   │ WiFi Driver   │
     │  (blufi_init) │   │ (ESP-IDF)     │
     └───────────────┘   └───────────────┘
```

### 2.3 核心类设计

#### BlufiProvisioning (单例模式)

**职责：**
- 管理蓝牙配网生命周期
- 处理 BluFi 协议事件
- 协调 WiFi 扫描和连接
- 状态通知和回调管理

**关键接口：**
```cpp
class BlufiProvisioning {
public:
    static BlufiProvisioning& GetInstance();

    void Start();                           // 启动配网
    void Stop();                            // 停止配网
    bool WaitForConfigured(int timeout_ms); // 等待配网完成

    // 回调设置
    void OnConfigured(std::function<...>);  // 配网成功回调
    void OnStateChanged(std::function<...>); // 状态变化回调

private:
    // BluFi 事件处理（静态回调）
    static void EventCallback(esp_blufi_cb_event_t, esp_blufi_cb_param_t*);

    // WiFi 事件处理
    static void WifiEventHandler(...);
};
```

**状态机：**
```
IDLE → ADVERTISING → CONNECTED → SCANNING_WIFI
                  ↓                      ↓
              FAILED                CONFIGURING
                                        ↓
                                   CONNECTING
                                        ↓
                                  CONNECTED_WIFI
```

---

## 3. 工作流程

### 3.1 初始化流程

```
1. esp_blufi_controller_init()        // 初始化蓝牙硬件控制器
   └─> esp_bt_controller_init()
   └─> esp_bt_controller_enable(BLE)

2. esp_blufi_host_and_cb_init()       // 初始化 NimBLE 协议栈
   └─> esp_nimble_init()
   └─> 配置 ble_hs_cfg 回调
       ├─> reset_cb: blufi_on_reset
       ├─> sync_cb: blufi_on_sync
       ├─> gatts_register_cb: esp_blufi_gatt_svr_register_cb
       └─> store_status_cb: ble_store_util_status_rr
   └─> esp_blufi_gatt_svr_init()      // 注册 GATT 服务
   └─> esp_blufi_btc_init()
   └─> esp_nimble_enable()             // 启动 BLE 主机任务

3. blufi_on_sync() 回调触发           // BLE 栈同步完成
   └─> esp_blufi_profile_init()       // 初始化 BluFi Profile
       └─> 触发 ESP_BLUFI_EVENT_INIT_FINISH

4. ESP_BLUFI_EVENT_INIT_FINISH        // 初始化完成
   └─> esp_blufi_adv_start()          // 开始 BLE 广播
```

**关键点：**
- GATT 服务必须在 `esp_nimble_enable()` **之前**注册
- `esp_blufi_profile_init()` 在 `blufi_on_sync()` 回调中调用
- 这样才能确保服务发现正常工作

### 3.2 配网流程

```
用户侧                     设备侧                        WiFi
  │                          │                            │
  │  1. 扫描 BLE 设备         │                            │
  ├─────────────────────────>│                            │
  │                          │ ADVERTISING                │
  │                          │                            │
  │  2. 连接 "Xiaozhi-BluFi" │                            │
  ├─────────────────────────>│                            │
  │                          │ CONNECTED                  │
  │                          │                            │
  │  3. DH 密钥协商           │                            │
  │<────────────────────────>│                            │
  │  (安全通道建立)           │                            │
  │                          │                            │
  │  4. 请求 WiFi 列表        │                            │
  ├─────────────────────────>│                            │
  │                          │ SCANNING_WIFI              │
  │                          ├───────────────────────────>│
  │                          │  esp_wifi_scan_start()     │
  │                          │<───────────────────────────┤
  │                          │  WIFI_EVENT_SCAN_DONE      │
  │  5. 返回 WiFi 列表        │                            │
  │<─────────────────────────┤                            │
  │  (SSID, RSSI)            │                            │
  │                          │                            │
  │  6. 发送 SSID + 密码      │                            │
  ├─────────────────────────>│                            │
  │                          │ CONFIGURING                │
  │                          │ 保存到 NVS                  │
  │                          │                            │
  │  7. 请求连接 AP           │                            │
  ├─────────────────────────>│                            │
  │                          │ CONNECTING                 │
  │                          ├───────────────────────────>│
  │                          │  esp_wifi_connect()        │
  │                          │<───────────────────────────┤
  │                          │  IP_EVENT_STA_GOT_IP       │
  │                          │ CONNECTED_WIFI             │
  │  8. 上报连接成功          │                            │
  │<─────────────────────────┤                            │
  │                          │                            │
  │                          │ 1.5 秒后                    │
  │                          │ esp_restart()              │
  │                          X                            │
  │                                                        │
  │                     (设备重启)                          │
  │                          │                            │
  │                          │ 自动从 NVS 读取 WiFi        │
  │                          ├───────────────────────────>│
  │                          │ 自动连接                    │
  │                          │                            │
```

### 3.3 关键事件处理

| 事件 | 处理逻辑 |
|------|----------|
| `ESP_BLUFI_EVENT_INIT_FINISH` | 启动 BLE 广播，设置状态为 ADVERTISING |
| `ESP_BLUFI_EVENT_BLE_CONNECT` | 标记已连接，设置状态为 CONNECTED |
| `ESP_BLUFI_EVENT_BLE_DISCONNECT` | 重新启动广播，返回 ADVERTISING 状态 |
| `ESP_BLUFI_EVENT_GET_WIFI_LIST` | 懒加载初始化 WiFi，启动扫描，状态 SCANNING_WIFI |
| `ESP_BLUFI_EVENT_RECV_STA_SSID` | 缓存接收到的 SSID |
| `ESP_BLUFI_EVENT_RECV_STA_PASSWD` | 缓存接收到的密码 |
| `ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP` | 保存凭证到 SsidManager，尝试连接 WiFi，状态 CONNECTING |
| `WIFI_EVENT_SCAN_DONE` | 构建并发送 WiFi 列表到手机 |
| `IP_EVENT_STA_GOT_IP` | 上报连接成功，触发 `on_configured_` 回调 |

---

## 4. 与 xiaozhi-esp32 的集成

### 4.1 启动入口

在 `ALichuangTest::StartNetwork()` 中集成：

```cpp
#ifdef CONFIG_BLUETOOTH_PROVISIONING_ALWAYS_ON_BOOT
    // 测试模式：每次启动都进入配网
    BlufiProvisioning::GetInstance().Start();
    // ...
#else
    // 正常模式：使用 WifiBoard 标准流程
    WifiBoard::StartNetwork();
#endif
```

**正常启动逻辑：**
1. `WifiBoard::StartNetwork()` 检查 `SsidManager` 是否有保存的凭证
2. 如果有 → 直接连接 WiFi
3. 如果没有 → 进入配网模式 (Web 或蓝牙)

### 4.2 配网完成处理

```cpp
blufi.OnConfigured([&](const std::string& ssid, const std::string& password) {
    ESP_LOGI(TAG, "BluFi configured: SSID=%s", ssid.c_str());
    vTaskDelay(pdMS_TO_TICKS(1500));  // 给手机端足够时间收到响应
    esp_restart();                     // 重启应用新配置
});
```

**重启后流程：**
1. `SsidManager` 从 NVS 加载已保存的 SSID
2. `WifiBoard::StartNetwork()` 检测到有凭证
3. 直接连接 WiFi，**不再进入配网模式**

### 4.3 内存优化

配网前临时停止音频服务以释放 RAM：
```cpp
app.GetAudioService().Stop();  // 蓝牙启动前
// ... BluFi 配网 ...
app.GetAudioService().Start(); // 超时或失败后恢复
```

**原因：** NimBLE 蓝牙栈需要约 50KB RAM，与音频处理模块共享有限的内存空间。

---

## 5. 蓝牙与 WiFi 共存

### 5.1 技术背景

ESP32-S3 的 WiFi 和蓝牙共享同一个 2.4GHz 射频硬件，使用**时分复用 (TDM)** 技术：

```
时间片：[BT] [WiFi] [BT] [WiFi] [BT] [WiFi] ...
           ↑     ↑     ↑     ↑
         蓝牙   WiFi  蓝牙  WiFi
```

### 5.2 关键实现点

**WiFi 扫描配置：**
```cpp
wifi_scan_config_t scan_cfg = {
    .ssid = NULL,
    .bssid = NULL,
    .channel = 0,
    .show_hidden = false,
    // 关键：不设置 scan_time！
    // 让系统使用默认扫描时间以确保 BT/WiFi 时间片协调
};
```

**为什么不能自定义扫描时间？**
- 自定义时间可能打破时间片平衡
- 导致蓝牙连接不稳定或 WiFi 扫描失败
- ESP-IDF 默认值已针对共存优化

**ESP-IDF 警告：**
```
W (xxx) wifi:Error! Should use default active scan time parameter
         for WiFi scan when Bluetooth is enabled!!!!!!
```

如果看到此警告，说明代码中设置了 `scan_cfg.scan_time`，应该移除。

---

## 6. 安全机制

### 6.1 加密流程

1. **DH 密钥交换** (`blufi_security.c`)
   - 使用 Diffie-Hellman 算法协商共享密钥
   - 防止中间人攻击

2. **AES-CFB128 加密**
   - 加密 SSID 和密码传输
   - 确保凭证不被窃听

3. **CRC16 校验**
   - 验证数据完整性
   - 检测传输错误

### 6.2 安全配置

```cpp
ble_hs_cfg.sm_io_cap = 4;  // 无 IO 能力（Just Works 配对）
```

**当前实现：** Just Works 配对模式（无 PIN 码）

**未来扩展：** 可添加 PIN 码验证或设备识别码

---

## 7. 故障排查

### 7.1 常见问题

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| 手机扫描不到设备 | 广播未启动 | 检查日志确认 `BLUFI init finished, start advertising` |
| 连接后 Service Discovery 失败 | GATT 服务注册时序错误 | 确保 `esp_blufi_gatt_svr_init()` 在 `esp_nimble_enable()` 之前调用 |
| WiFi 扫描列表为空 | WiFi 未初始化或扫描失败 | 检查 `InitWifiIfNeeded()` 是否正确调用 |
| 配网后无法连接 WiFi | 密码错误或 WiFi 不兼容 | 检查 WiFi 认证模式（仅支持 WPA2-PSK） |
| BT/WiFi 共存警告 | 自定义扫描时间参数 | 移除 `scan_cfg.scan_time` 配置 |

### 7.2 调试日志关键点

**正常启动流程：**
```
I (xxx) BlufiProvisioning: Starting BluFi provisioning
I (xxx) BLE_INIT: BT controller compile version [...]
I (xxx) BLUFI_EXAMPLE: registered service 0xffff with handle=14
I (xxx) BlufiProvisioning: BLUFI init finished, start advertising
I (xxx) NimBLE: GAP procedure initiated: advertise
```

**配网流程：**
```
I (xxx) BLUFI_EXAMPLE: connection established
I (xxx) BlufiProvisioning: BLE connected
I (xxx) BlufiProvisioning: Get WiFi list request
I (xxx) wifi: scan start
I (xxx) wifi: scan done
I (xxx) BlufiProvisioning: Received SSID: YourWiFi
I (xxx) BlufiProvisioning: Received password length: 8
I (xxx) BlufiProvisioning: Request connect to AP
I (xxx) wifi: connected with YourWiFi, channel 6
I (xxx) BlufiProvisioning: Got IP, provisioning success
```

---

## 8. 配置指南

### 8.1 启用蓝牙配网

```bash
idf.py menuconfig
```

导航到：
```
Xiaozhi Assistant
  → [*] Enable Bluetooth Provisioning
  → (300) BluFi provisioning timeout (seconds)
  → [ ] Always start BluFi provisioning on boot (testing)
  → [*] Fallback to Web Config on Timeout
```

### 8.2 测试模式

启用 `Always start BluFi provisioning on boot` 用于开发测试：
- 每次启动都进入配网模式
- 无需手动擦除 NVS
- 方便反复测试配网流程

**生产环境请关闭此选项！**

### 8.3 构建和烧录

```bash
idf.py build
idf.py -p COM3 flash monitor
```

---

## 9. 手机端开发

### 9.1 协议支持

**推荐方案：**
- 使用 Espressif 官方 BluFi SDK
  - Android: [EspBlufi](https://github.com/EspressifApp/EspBlufi)
  - iOS: [EspBlufiForiOS](https://github.com/EspressifApp/EspBlufiForiOS)

**微信小程序：**
- 使用 `wx.openBluetoothAdapter()` 等 BLE API
- 实现 BluFi 协议客户端
- 需要处理数据分包和校验

### 9.2 BluFi 协议数据包格式

```
+--------+--------+--------+--------+--------+----------+
| Type   | Frame  | Seq    | Data   |  Data  | Checksum |
|        | Ctrl   | Number | Length |        | (opt)    |
+--------+--------+--------+--------+--------+----------+
| 1 byte | 1 byte | 1 byte | 1 byte | N bytes| 2 bytes  |
```

**主要消息类型：**
- `0x00`: Negotiate Data (DH 密钥协商)
- `0x03`: Get WiFi List Request
- `0x04`: Get WiFi List Response
- `0x05`: Set STA SSID
- `0x06`: Set STA Password
- `0x0A`: Connect to AP Request
- `0x0B`: WiFi Connection Report

**参考文档：**
- [ESP-IDF BluFi Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/blufi.html)
- [BluFi Protocol Spec](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/blufi/main/blufi_protocol.md)

---

## 10. 技术限制与注意事项

### 10.1 硬件限制

- **仅支持 2.4GHz WiFi**：ESP32-S3 不支持 5GHz
- **内存约束**：蓝牙和音频不能同时运行（需释放音频模块）
- **射频共享**：BT/WiFi 共存时性能受时间片分配影响

### 10.2 安全限制

- **WiFi 认证模式**：默认仅支持 WPA2-PSK
- **企业级 WiFi**：不支持 EAP 认证
- **配对安全**：当前使用 Just Works 模式（无 PIN 码）

### 10.3 已知问题

1. **配网过程中不能接听电话**：蓝牙连接会中断
2. **某些路由器兼容性问题**：建议使用主流品牌路由器测试
3. **扫描结果可能不完整**：信号弱的 AP 可能被过滤

---

## 11. 未来扩展方向

### 11.1 功能增强

- [ ] 支持企业级 WiFi (EAP 认证)
- [ ] 添加 PIN 码配对
- [ ] WiFi Mesh 网络配网
- [ ] 批量设备配网

### 11.2 性能优化

- [ ] 优化内存占用，支持蓝牙和音频同时运行
- [ ] 加快 WiFi 扫描速度
- [ ] 改进 BT/WiFi 共存性能

### 11.3 多平台支持

- [ ] 微信小程序客户端
- [ ] 鸿蒙 HarmonyOS 应用
- [ ] Web Bluetooth API 支持

---

## 12. 参考资料

### 12.1 官方文档

- [ESP-IDF BluFi Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/blufi.html)
- [ESP32 Bluetooth API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/index.html)
- [NimBLE User Guide](https://mynewt.apache.org/latest/network/docs/index.html)

### 12.2 示例代码

- [ESP-IDF BluFi Example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/blufi)
- [EspBlufi Android App](https://github.com/EspressifApp/EspBlufi)

### 12.3 项目文档

- 本项目 README: `D:\AIDev\xiaozhi-esp32\README.md`
- WiFi 板型基类: `main/boards/common/wifi_board.cc`
- SSID 管理器: `managed_components/78__esp-wifi-connect/`

---

## 附录 A：技术术语表

| 术语 | 全称 | 说明 |
|------|------|------|
| BluFi | Bluetooth WiFi | Espressif 的蓝牙配网协议 |
| BLE | Bluetooth Low Energy | 蓝牙低功耗 |
| NimBLE | - | Apache Mynewt 的轻量级蓝牙协议栈 |
| GATT | Generic Attribute Profile | BLE 通用属性协议 |
| GAP | Generic Access Profile | BLE 通用访问协议 |
| DH | Diffie-Hellman | 密钥交换算法 |
| AES | Advanced Encryption Standard | 高级加密标准 |
| NVS | Non-Volatile Storage | 非易失性存储 |
| TDM | Time Division Multiplexing | 时分复用 |

---

## 附录 B：版本历史

| 版本 | 日期 | 变更说明 |
|------|------|---------|
| 1.0 | 2025-01 | 初始实现，基于 ESP-IDF BluFi 示例 |
| 1.1 | 2025-01 | 修复 NimBLE 初始化时序问题 |
| 1.2 | 2025-01 | 修复 BT/WiFi 共存警告 |

---

**文档维护者：** ALichuangTest 开发团队
**最后更新：** 2025-01-23
**适用版本：** ESP-IDF 5.5.0+

