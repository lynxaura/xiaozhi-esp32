# BluFi 配网集成说明（ALichuangTest）

本说明文档补充了实际落地实现的细节，便于测试与后续维护。

## 代码结构

- 目录：`main/boards/ALichuangTest/bluetooth_provisioning/`
  - `blufi_provisioning.h/.cc`：BluFi 配网封装（事件回调、Wi‑Fi 扫描/连接、回调通知）
  - `blufi_init.c`：BluFi 控制器/主机初始化（从示例精简适配，内置 `BLUFI_DEVICE_NAME`）
  - `blufi_security.c`：安全模块（DH/AES/CRC，从示例移植）
- `main/boards/ALichuangTest/ALichuangTest.cc`：在启用测试开关时覆盖 `StartNetwork()` 以优先进入 BluFi 配网

## Kconfig 选项

位于 `Xiaozhi Assistant` 菜单下：

- `ENABLE_BLUETOOTH_PROVISIONING`：启用 BluFi 配网（同时选择 `BT_ENABLED` 和 `BT_NIMBLE_ENABLED`）
- `BLUETOOTH_PROVISIONING_ALWAYS_ON_BOOT`：测试用，每次开机都启动 BluFi 配网
- `BLUETOOTH_PROVISIONING_TIMEOUT`：配网超时（秒），默认 300
- `BLUFI_FALLBACK_TO_WEB_CONFIG`：超时回退到 Web AP 配网，默认开启

## 构建集成

当开启 `ENABLE_BLUETOOTH_PROVISIONING` 时，`main/CMakeLists.txt` 会自动编译 `bluetooth_provisioning` 下的 `.c/.cc` 源文件。

## 运行流程

1. 开机 → 若启用“每次开机都启动配网”，进入 BluFi（广告名 `Xiaozhi-BluFi`）
2. 手机小程序连入 → 请求 Wi‑Fi 列表 → 设备扫描并通过 BluFi 回传
3. 小程序发送 SSID/密码 → 设备写入 `SsidManager` 并尝试连接
4. 获得 IP → 通过 BluFi 上报成功 → 触发回调并重启
5. 重启后由 `WifiBoard`/`WifiStation` 使用已保存凭据联网

## 注意事项

- 配网阶段独立初始化最小 Wi‑Fi 环境用于扫描与连接，成功后重启，避免与运行态 `WifiStation` 状态冲突
- 默认使用 NimBLE（通过 `BT_NIMBLE_ENABLED`）；如需 Bluedroid，可在 IDF 配置中切换
- 若测试阶段希望始终进入配网，请开启 `BLUETOOTH_PROVISIONING_ALWAYS_ON_BOOT`

