# 事件上传功能开发TODO

## 概述
本文档描述事件上传功能的分步实现计划，每个阶段都可独立开发和测试，确保问题可以即时发现和修复。

## 开发原则
- ✅ 每个阶段可独立编译和测试
- ✅ 优先实现核心功能，后续迭代优化
- ✅ 每步都有具体的验证方法
- ✅ 使用日志输出验证功能正确性
- ✅ 问题即时修复，不累积到后续阶段

---

## Phase 1: 基础框架搭建 [⏱️ 30分钟]

### 1.1 创建基础文件结构
- [ ] 创建目录 `main/boards/ALichuangTest/interaction/`
- [ ] 创建配置文件 `event_notification_config.h`
- [ ] 创建头文件 `event_uploader.h`（只包含类声明框架）
- [ ] 创建实现文件 `event_uploader.cc`（只包含空实现）

### 1.2 定义配置常量
```cpp
// event_notification_config.h
struct EventNotificationConfig {
    static constexpr bool ENABLED = true;
    static constexpr int MAX_CACHE_SIZE = 20;
    static constexpr int CACHE_TIMEOUT_MS = 300000;  // 5分钟
    static constexpr int BATCH_SIZE = 10;
};
```

### 1.3 实现基础类框架
- [ ] EventUploader构造函数和析构函数
- [ ] 基础成员变量（enabled_, device_id_）
- [ ] 获取设备ID的逻辑（从MAC地址生成）

### 测试点 1.1
```cpp
// 添加测试代码到构造函数
EventUploader() {
    ESP_LOGI(TAG, "EventUploader created");
    // 获取并打印device_id
    ESP_LOGI(TAG, "Device ID: %s", device_id_.c_str());
}
```
**验证**：编译通过，运行时能看到日志输出

---

## Phase 2: 事件转换逻辑 [⏱️ 45分钟]

### 2.1 实现事件类型映射
- [ ] 实现 `GetEventTypeString()` - 将EventType转换为字符串
- [ ] 处理触摸事件的位置信息（Left/Right/Both）
- [ ] 处理运动事件的类型映射

### 2.2 实现事件文本生成
- [ ] 实现 `GenerateEventText()` - 生成中文描述
- [ ] 为每种事件类型配置对应的中文文本

### 2.3 实现事件转换
- [ ] 实现 `ConvertEvent()` - Event → CachedEvent
- [ ] 实现 `CalculateDuration()` - 计算事件持续时间
- [ ] 暂时忽略时间同步，使用单调时钟

### 测试点 2.1
```cpp
// 创建单元测试函数
void TestEventConversion() {
    EventUploader uploader;
    
    // 测试触摸事件
    Event touch_event{EventType::TOUCH_TAP};
    touch_event.data.touch_data.x = 0; // LEFT
    
    auto cached = uploader.ConvertEvent(touch_event);
    ESP_LOGI(TAG, "Event type: %s", cached.event_type.c_str());
    ESP_LOGI(TAG, "Event text: %s", cached.event_text.c_str());
    
    // 期望输出：
    // Event type: Touch_Left_Tap
    // Event text: 主人轻轻拍了我的左侧
}
```

### 测试点 2.2
```cpp
// 测试所有事件类型的映射
void TestAllEventTypes() {
    // 测试每种EventType的转换
    // 验证event_type和event_text格式正确
}
```

---

## Phase 3: JSON序列化 [⏱️ 30分钟]

### 3.1 实现payload构建
- [ ] 实现 `BuildEventPayload()` 模板函数
- [ ] 生成符合规范的JSON格式
- [ ] 处理event_payload字段（通常为空）

### 3.2 内存安全处理
- [ ] 使用智能指针管理cJSON对象
- [ ] 实现CJsonDeleter和cjson_uptr
- [ ] 确保无内存泄漏

### 测试点 3.1 - 真实事件JSON生成测试
```cpp
// 在EventUploader::HandleEvent()开头添加调试输出
void EventUploader::HandleEvent(const Event& event) {
    if (!enabled_) return;
    
    ESP_LOGI(TAG, "=== Event Processing Debug ===");
    ESP_LOGI(TAG, "Raw event type: %d", (int)event.type);
    
    // 转换事件
    CachedEvent cached = ConvertEvent(event);
    
    ESP_LOGI(TAG, "Event converted: %s -> %s", 
             cached.event_type.c_str(), cached.event_text.c_str());
    
    // 测试JSON生成（单事件）
    std::vector<CachedEvent> test_vec;
    test_vec.push_back(std::move(cached));
    
    std::string payload = BuildEventPayload(test_vec.begin(), test_vec.end());
    ESP_LOGI(TAG, "Generated JSON: %s", payload.c_str());
    
    // 验证JSON有效性
    cJSON* json = cJSON_Parse(payload.c_str());
    if (json) {
        ESP_LOGI(TAG, "✓ JSON valid");
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "✗ JSON invalid!");
    }
    
    // 继续正常处理流程...
}
```

**测试方法**: 
1. 烧录固件: `idf.py build flash monitor`
2. 物理操作: 轻拍左侧触摸区域  
3. 观察日志: 验证事件转换和JSON格式正确

**期望日志输出**:
```bash
I EventUploader: === Event Processing Debug ===
I EventUploader: Raw event type: 21
I EventUploader: Event converted: Touch_Left_Tap -> 主人轻轻拍了我的左侧  
I EventUploader: Generated JSON: {"events":[{"event_type":"Touch_Left_Tap",...}]}
I EventUploader: ✓ JSON valid
```

---

## Phase 4: Application集成（最小化） [⏱️ 45分钟]

### 4.1 添加SendEventMessage方法
- [ ] 在 `application.h` 中声明 `SendEventMessage()`
- [ ] 在 `application.cc` 中实现（添加session_id和type）
- [ ] 使用Schedule确保线程安全

### 4.2 实现完整消息构建
- [ ] 构建包含session_id的完整消息
- [ ] 设置type为"lx/v1/event"
- [ ] 将payload嵌入到完整消息中

### 测试点 4.1
```cpp
// 在Application中添加测试方法
void TestSendEventMessage() {
    std::string test_payload = R"({"events":[{"event_type":"Touch_Left_Tap","event_text":"测试事件","start_time":1234567890,"end_time":1234567890}]})";
    
    SendEventMessage(test_payload);
    // 查看日志确认消息格式正确
}
```

### 测试点 4.2 - 真实协议发送测试
```cpp
// 在Application::SendEventMessage()中添加详细日志
void Application::SendEventMessage(const std::string& payload_str) {
    Schedule([this, payload_str]() {
        ESP_LOGI(TAG, "=== Sending Event Message ===");
        ESP_LOGI(TAG, "Payload: %s", payload_str.c_str());
        
        if (!protocol_) {
            ESP_LOGW(TAG, "Protocol not available, message not sent");
            return;
        }
        
        if (!protocol_->IsConnected()) {
            ESP_LOGW(TAG, "Protocol not connected, message not sent");
            return;
        }
        
        // 构建完整消息
        cJSON* message = cJSON_CreateObject();
        if (!session_id_.empty()) {
            cJSON_AddStringToObject(message, "session_id", session_id_.c_str());
        }
        cJSON_AddStringToObject(message, "type", "lx/v1/event");
        
        cJSON* payload = cJSON_Parse(payload_str.c_str());
        if (payload) {
            cJSON_AddItemToObject(message, "payload", payload);
        }
        
        char* json_str = cJSON_PrintUnformatted(message);
        std::string full_message(json_str);
        
        ESP_LOGI(TAG, "Full message: %s", full_message.c_str());
        ESP_LOGI(TAG, "Sending to server...");
        
        protocol_->SendText(full_message);
        
        ESP_LOGI(TAG, "✓ Event message sent successfully");
        
        cJSON_free(json_str);
        cJSON_Delete(message);
    });
}
```

**测试方法**:
1. 确保设备已连接到服务器
2. 触发事件（如轻拍触摸区域）
3. 观察完整的发送流程日志
4. 在服务端验证收到正确格式的消息

**期望日志输出**:
```bash
I Application: === Sending Event Message ===
I Application: Payload: {"events":[{"event_type":"Touch_Left_Tap",...}]}
I Application: Full message: {"session_id":"xxx","type":"lx/v1/event","payload":{"events":[...]}}
I Application: Sending to server...
I Application: ✓ Event message sent successfully
```

---

## Phase 5: 连接和缓存管理 [⏱️ 1小时]

### 5.1 实现连接状态检查
- [ ] 实现 `IsConnected()` 方法
- [ ] 检查Protocol的连接状态

### 5.2 实现事件缓存
- [ ] 实现 `HandleEvent()` - 决定立即发送或缓存
- [ ] 实现缓存容量管理（FIFO策略）
- [ ] 添加线程安全的缓存操作

### 5.3 实现连接恢复时的批量发送
- [ ] 实现 `OnConnectionOpened()`
- [ ] 实现批量发送逻辑
- [ ] 处理BATCH_SIZE分批

### 测试点 5.1
```cpp
// 测试缓存机制
void TestEventCaching() {
    EventUploader uploader;
    uploader.Enable(true);
    
    // 模拟断线状态
    uploader.OnConnectionClosed();
    
    // 发送多个事件
    for (int i = 0; i < 5; i++) {
        Event event{EventType::TOUCH_TAP};
        uploader.HandleEvent(event);
    }
    
    // 检查缓存大小（需要友元或测试接口）
    ESP_LOGI(TAG, "Events cached successfully");
    
    // 模拟连接恢复
    uploader.OnConnectionOpened();
    // 验证批量发送
}
```

### 测试点 5.2
```cpp
// 测试缓存溢出处理
void TestCacheOverflow() {
    EventUploader uploader;
    
    // 发送超过MAX_CACHE_SIZE的事件
    for (int i = 0; i < 30; i++) {
        Event event{EventType::MOTION_SHAKE};
        uploader.HandleEvent(event);
    }
    
    // 验证最旧的事件被删除
    // 验证缓存大小不超过MAX_CACHE_SIZE
}
```

---

## Phase 6: 时间同步处理 [⏱️ 45分钟]

### 6.1 实现时间同步检测
- [ ] 实现 `IsTimesynced()` 方法
- [ ] 检查系统时间合理性（不是1970年）

### 6.2 实现时间回填逻辑
- [ ] 实现 `OnTimeSynced()` 回调
- [ ] 使用单调时钟差值计算真实Unix时间戳
- [ ] 处理缓存事件的时间戳回填

### 6.3 实现TTL过期清理
- [ ] 检查事件是否超过CACHE_TIMEOUT_MS
- [ ] 清理过期事件

### 测试点 6.1
```cpp
// 测试时间同步逻辑
void TestTimeSync() {
    EventUploader uploader;
    
    // 创建事件（时间未同步）
    Event event{EventType::TOUCH_TAP};
    auto cached = uploader.ConvertEvent(event);
    
    ESP_LOGI(TAG, "Before sync - start_time: %lld, mono_ms: %lld", 
             cached.start_time, cached.mono_ms);
    
    // 模拟时间同步
    uploader.OnTimeSynced();
    
    // 验证时间戳被正确回填
}
```

---

## Phase 7: 板级集成 [⏱️ 45分钟]

### 7.1 集成到ALichuangTest
- [ ] 在 `ALichuangTest.h` 中添加 `event_uploader_` 成员
- [ ] 在构造函数中创建EventUploader实例
- [ ] 实现 `GetEventUploader()` 访问器

### 7.2 注册事件回调
- [ ] 将EventUploader连接到EventEngine
- [ ] 确保事件能够传递到EventUploader

### 7.3 连接Application回调
- [ ] 在 `OnAudioChannelOpened()` 中调用 `uploader->OnConnectionOpened()`
- [ ] 在 `OnAudioChannelClosed()` 中调用 `uploader->OnConnectionClosed()`
- [ ] 在时间同步时调用 `uploader->OnTimeSynced()`

### 测试点 7.1
```cpp
// 在ALichuangTest构造函数中
ALichuangTest() {
    // ... 现有代码 ...
    
    event_uploader_ = std::make_unique<EventUploader>();
    ESP_LOGI(TAG, "EventUploader integrated into board");
    
    // 测试事件流
    if (event_engine_ && event_uploader_) {
        event_engine_->RegisterCallback([this](const Event& event) {
            ESP_LOGI(TAG, "Event received in board: type=%d", (int)event.type);
            event_uploader_->HandleEvent(event);
        });
    }
}
```

---

## Phase 8: 端到端测试 [⏱️ 1小时]

### 8.1 模拟真实事件流
- [ ] 触发实际的触摸事件
- [ ] 触发实际的运动事件
- [ ] 验证事件从传感器到服务器的完整流程

### 8.2 网络测试
- [ ] 测试WiFi连接和断开时的行为
- [ ] 测试事件缓存和批量发送
- [ ] 使用Wireshark或日志验证消息格式

### 8.3 压力测试
- [ ] 快速连续触发多个事件
- [ ] 测试高频事件的处理性能
- [ ] 验证内存使用情况

### 测试点 8.1
```bash
# 使用串口监视器观察日志
# 触摸左侧 -> 应该看到 "Touch_Left_Tap"
# 长按右侧 -> 应该看到 "Touch_Right_LongPress"
# 摇晃设备 -> 应该看到 "Motion_Shake"
```

### 测试点 8.2 - 真实服务端验证
**测试方法**:
1. **设备端**: 确保设备正常连接到你的后端服务器
2. **后端日志**: 检查后端服务器日志，确认收到了事件消息
3. **消息格式验证**: 后端应该能够正确解析 `lx/v1/event` 类型的消息
4. **数据完整性**: 验证事件的所有字段都正确传输

**后端应该收到的消息格式**:
```json
{
  "session_id": "9aa008fa-c874-4829-b70b-fca7fa30e3da",
  "type": "lx/v1/event", 
  "payload": {
    "events": [
      {
        "event_type": "Touch_Left_Tap",
        "event_text": "主人轻轻拍了我的左侧",
        "start_time": 1755222858360,
        "end_time": 1755222858360
      }
    ]
  }
}
```

**验证步骤**:
1. 触摸设备左侧
2. 检查设备日志确认消息已发送
3. 检查后端日志确认消息已接收
4. 验证后端解析出的事件类型和描述正确

---

## Phase 9: 优化和完善 [⏱️ 30分钟]

### 9.1 性能优化
- [ ] 优化JSON序列化性能
- [ ] 减少不必要的内存分配
- [ ] 优化锁的粒度

### 9.2 错误处理
- [ ] 添加异常处理
- [ ] 处理JSON解析失败
- [ ] 处理网络发送失败

### 9.3 日志优化
- [ ] 添加详细的调试日志
- [ ] 使用不同的日志级别
- [ ] 添加性能统计日志

---

## 验证清单

### 基础功能验证
- [ ] 事件类型转换正确
- [ ] 中文描述生成正确
- [ ] JSON格式符合规范
- [ ] 消息包含正确的session_id和type

### 连接管理验证
- [ ] 连接时立即发送事件
- [ ] 断线时缓存事件
- [ ] 重连时批量发送缓存

### 时间同步验证
- [ ] 时间未同步时使用单调时钟
- [ ] 时间同步后回填Unix时间戳
- [ ] TTL过期清理正常

### 稳定性验证
- [ ] 无内存泄漏
- [ ] 无崩溃
- [ ] 高负载下稳定运行
- [ ] 长时间运行稳定

---

## 问题排查指南

### 常见问题

1. **编译错误**
   - 检查头文件包含路径
   - 检查CMakeLists.txt是否包含新文件
   - 检查依赖的类和函数是否存在

2. **事件未发送**
   - 检查EventUploader是否已启用（Enable(true)）
   - 检查连接状态（IsConnected()）
   - 查看日志确认事件是否被缓存

3. **JSON格式错误**
   - 使用在线JSON验证工具检查格式
   - 确认cJSON使用正确
   - 检查字符串转义

4. **内存问题**
   - 使用ESP32的heap监控功能
   - 检查智能指针使用
   - 验证vector扩容时的移动语义

5. **时间戳异常**
   - 检查SNTP配置
   - 验证系统时间
   - 查看时间同步日志

---

## 完成标准

✅ 所有测试点通过  
✅ 无编译警告  
✅ 无运行时错误  
✅ 服务端能正确接收和解析事件  
✅ 代码review通过  
✅ 文档更新完成  

---

## 时间估算

- Phase 1-3: 1.5小时（基础实现）
- Phase 4-6: 2小时（核心功能）
- Phase 7-8: 1.5小时（集成测试）
- Phase 9: 0.5小时（优化）
- **总计**: 约5.5小时

建议分2-3个开发周期完成，每个周期2-3小时，确保充分测试。