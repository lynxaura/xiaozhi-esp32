# ALichuangTest 生产检查清单

## 概述
本文档记录了开发过程中为了便于调试和测试而做的临时修改，这些修改在生产环境中需要恢复或重新配置。

## 必须修复的开发配置

### 1. OTA 版本检查功能
**当前状态**: 已禁用
**位置**: 
- `main/boards/ALichuangTest/config.json` - 第13行
- `main/ota.cc` - 第83-86行

**开发期间的修改**:
```json
"CONFIG_OTA_URL=\"\""
```
```cpp
if (url.empty()) {
    ESP_LOGI(TAG, "OTA URL not configured, skipping version check");
    return true;  // 跳过OTA检查，返回成功
}
```

**生产环境修复**:
1. 设置正确的生产OTA服务器URL:
   ```json
   "CONFIG_OTA_URL=\"https://your-production-server.com/xiaozhi/ota/\""
   ```
2. 移除或注释掉跳过OTA检查的代码
3. 确保OTA服务器可达且证书配置正确

---

### 2. 事件上传测试配置
**当前状态**: 使用固定设备ID
**位置**: `main/boards/ALichuangTest/interaction/event_uploader.cc` - 第27行

**开发期间的修改**:
```cpp
return "alichuang_test_device";  // 固定的测试设备ID
```

**生产环境修复**:
实现真实的设备ID生成逻辑，例如:
```cpp
std::string EventUploader::GenerateDeviceId() {
    // 使用MAC地址生成唯一设备ID
    std::string mac = SystemInfo::GetMacAddress();
    return "alichuang_" + mac;
    
    // 或者使用UUID
    auto& board = Board::GetInstance();
    return board.GetUuid();
}
```

---

### 3. 调试日志级别
**当前状态**: 详细调试日志
**位置**: 
- `main/boards/ALichuangTest/interaction/event_uploader.cc`
- 其他相关文件

**开发期间的配置**:
- 大量ESP_LOGI调试信息
- 事件处理详细日志输出
- JSON数据完整打印

**生产环境修复**:
1. 将调试日志改为ESP_LOGD或移除
2. 减少敏感信息的日志输出
3. 在sdkconfig中设置适当的日志级别:
   ```
   CONFIG_LOG_DEFAULT_LEVEL_INFO=y
   或
   CONFIG_LOG_DEFAULT_LEVEL_WARN=y
   ```

---

### 4. 网络配置检查
**当前状态**: 需要确认
**位置**: 板级网络配置

**需要检查的项目**:
- WiFi连接超时设置
- 网络重连策略
- DNS服务器配置
- 证书验证设置

**生产环境要求**:
- 启用SSL证书验证
- 设置合理的网络超时
- 配置生产环境的DNS服务器

---

### 5. 硬件调试配置
**当前状态**: 开发模式
**位置**: 各硬件驱动文件

**可能的开发配置**:
- 振动马达功率设置
- 触摸传感器灵敏度
- LED亮度和动画速度
- 音频音量默认值

**生产环境修复**:
- 调整为生产环境适合的硬件参数
- 确保硬件功耗符合预期
- 验证所有硬件功能正常

---

### 6. 内存和性能优化
**当前状态**: 开发配置
**可能需要调整的项目**:
- 事件缓存大小 (当前20个事件)
- 任务栈大小设置
- 内存分配策略
- 看门狗超时设置

**生产环境优化**:
- 根据实际使用情况调整缓存大小
- 优化内存使用
- 设置合适的看门狗超时

---

## 测试验证清单

### 功能测试
- [ ] OTA功能完整测试
- [ ] 事件上传端到端测试
- [ ] 网络连接稳定性测试
- [ ] 硬件交互测试
- [ ] 长时间运行测试

### 安全测试
- [ ] SSL证书验证
- [ ] 设备认证机制
- [ ] 数据传输加密
- [ ] 敏感信息保护

### 性能测试
- [ ] 内存使用监控
- [ ] CPU性能测试
- [ ] 功耗测试
- [ ] 网络延迟测试

---

## 配置文件对比

### 开发环境 vs 生产环境

| 配置项 | 开发环境 | 生产环境 |
|--------|----------|----------|
| OTA_URL | "" (空) | "https://prod-server.com/ota/" |
| 日志级别 | DEBUG/INFO | WARN/ERROR |
| 设备ID | 固定字符串 | 动态生成 |
| SSL验证 | 可能关闭 | 必须启用 |

---

## 部署前检查命令

```bash
# 1. 检查OTA配置
grep -r "CONFIG_OTA_URL" main/boards/ALichuangTest/

# 2. 检查调试日志
grep -r "ESP_LOGI" main/boards/ALichuangTest/ | wc -l

# 3. 检查固定配置
grep -r "alichuang_test_device" main/boards/ALichuangTest/

# 4. 检查证书配置
grep -r "CONFIG_.*SSL" main/boards/ALichuangTest/
```

---

## 联系信息
如有疑问，请联系开发团队确认具体的生产环境配置要求。

**更新日期**: 2025-01-21
**版本**: v1.0