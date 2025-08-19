# 基于MCP Notification的事件上传功能实现指南

## 概述
本文档描述如何使用MCP (Model Context Protocol) Notification机制，将交互事件（触摸事件、运动事件等）上传到服务器。采用标准的JSON-RPC 2.0 Notification格式，通过现有的MCP通信通道实现设备主动事件推送。

**重要兼容性说明**：
- 事件字段名保持与旧版`lx/v1/event`协议一致：`start_time`、`end_time`、`event_text`
- 外层消息统一使用`type: "mcp"`，不再使用`type: "lx/v1/event"`
- 所有事件统一使用单一方法`events/publish`，不区分touch/motion等子类型
- 服务端仅需修改外层协议解析，事件体结构零改动

**关键改进点**：
- ✅ **内存安全**：使用`unique_ptr<cJSON>`防止double free，确保内存管理安全
- ✅ **时间戳类型**：使用int64整型毫秒时间戳，确保跨语言解析稳定性
- ✅ **锁粒度优化**：JSON序列化移到锁外，避免长时间持锁阻塞新事件
- ✅ **配置统一化**：所有常量集中在EventNotificationConfig，避免重复定义
- ✅ **连接状态判断**：使用`IsMcpChannelOpened()`，建议Protocol层补充更通用接口
- ✅ **批量发送实现**：支持配置的BATCH_SIZE分批发送，避免单个消息过大
- ✅ **事件去重机制**：自动生成event_id，支持服务端按(device_id, event_id)去重
- ✅ **API确认**：明确`SendMcpMessage()`只接受内层JSON-RPC，外层封装自动处理

## 架构设计

### MCP Notification优势
- **标准化协议**：遵循JSON-RPC 2.0规范，与MCP工具调用共享同一协议层
- **统一通道**：复用现有WebSocket/MQTT连接，无需额外通信通道
- **无响应开销**：Notification不需要服务器响应，减少网络往返
- **类型化消息**：通过method字段清晰分类不同事件类型
- **扩展性强**：便于未来添加新的notification类型

### 事件流程
```
用户交互 → TouchEngine/MotionEngine → EventProcessor(防抖/节流/冷却) 
    → EventEngine → MCP Notification → 服务器 → LLM
```

## MCP Notification消息格式

### 基础消息结构
通过`Application::SendMcpMessage()`发送的完整消息格式（保持与旧版字段兼容）：
```json
{
  "session_id": "9aa008fa-c874-4829-b70b-fca7fa30e3da",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "events/publish",
    "params": {
      "events": [
        {
          "event_type": "tickled",
          "start_time": 1755222858360,
          "end_time": 1755222859560,
          "event_text": "User tickled the device with rapid touches",
          "metadata": {
            "touch_count": 5,
            "intensity": "medium"
          }
        }
      ]
    }
  }
}
```

### 事件发布方法

采用单一统一的方法处理所有事件：

- **Method**: `events/publish`
- **类型**: JSON-RPC 2.0 Notification (无id字段，不需要响应)
- **用途**: 设备主动推送所有类型的交互事件
- **优势**: 
  - 简化服务端路由逻辑
  - event_type字段已足够区分事件类型
  - 符合JSON-RPC语义（Notification不回包）
  - 便于服务端统一处理（入队/写库/推LLM）

## 事件数据结构

### 事件参数格式（与旧版lx/v1/event保持一致）
```typescript
interface EventParams {
  events: Event[];
}

interface Event {
  event_type: string;        // 事件类型标识
  start_time: number;        // 事件开始时间戳（int64 ms since epoch）
  end_time: number;          // 事件结束时间戳（int64 ms since epoch）
  event_text: string;        // 事件描述文本（供LLM理解）
  metadata?: {               // 额外元数据（可选）
    event_id?: string;       // 事件唯一ID，用于去重（格式：{device_id}-{timestamp}-{seq}）
    [key: string]: any;
  };
}
```

### 设备端事件类型映射

#### 触摸事件映射
```cpp
// TouchEventType → event_type字符串
TouchEventType::TICKLED      → "tickled"      // 挠痒模式
TouchEventType::CRADLED      → "cradled"      // 摇篮模式  
TouchEventType::SINGLE_TAP   → "tap"          // 单击
TouchEventType::DOUBLE_TAP   → "double_tap"   // 双击
TouchEventType::LONG_PRESS   → "long_press"   // 长按
TouchEventType::HOLD         → "hold"         // 持续按住
TouchEventType::RELEASE      → "release"      // 释放
```

#### 运动事件映射
```cpp
// MotionEventType → event_type字符串
EventType::MOTION_SHAKE           → "shake"           // 摇晃
EventType::MOTION_SHAKE_VIOLENTLY → "shake_violently" // 剧烈摇晃
EventType::MOTION_FLIP            → "flip"            // 翻转
EventType::MOTION_FREE_FALL       → "free_fall"       // 自由落体
EventType::MOTION_PICKUP          → "pickup"          // 拿起
EventType::MOTION_UPSIDE_DOWN     → "upside_down"     // 倒置
EventType::MOTION_TILT            → "tilt"            // 倾斜
```

## 发送策略

### 实时发送策略
由于本地EventProcessor已经实施了防抖、节流、冷却等策略，到达上传阶段的事件都是需要及时处理的：

1. **连接已建立时**：立即发送事件notification，无需等待
2. **连接未建立时**：缓存事件，连接成功后批量发送
3. **优先级处理**：根据事件重要性分级处理，紧急事件优先发送

#### 事件优先级定义
```cpp
enum class EventPriority {
    LOW = 0,      // 普通事件（如普通tap）
    MEDIUM = 1,   // 重要交互（如long_press、shake）
    HIGH = 2,     // 紧急事件（如TICKLED、CRADLED、shake_violently）
    CRITICAL = 3  // 系统关键事件（如free_fall）
};

// 事件优先级映射
inline EventPriority GetEventPriority(EventType type) {
    switch (type) {
        case EventType::MOTION_FREE_FALL:
            return EventPriority::CRITICAL;
            
        case EventType::TOUCH_TICKLED:
        case EventType::TOUCH_CRADLED:
        case EventType::MOTION_SHAKE_VIOLENTLY:
            return EventPriority::HIGH;
            
        case EventType::TOUCH_LONG_PRESS:
        case EventType::MOTION_SHAKE:
        case EventType::MOTION_PICKUP:
            return EventPriority::MEDIUM;
            
        default:
            return EventPriority::LOW;
    }
}
```

### 连接管理
- **连接复用**：使用现有MCP连接通道，无需单独建立
- **连接检测**：通过`IsMcpChannelOpened()`检查WebSocket/MCP连接状态
- **智能重连**：连接断开后自动缓存事件，重连后发送

### 缓存策略
- **最大缓存数**：20个事件（本地已过滤，不会太多）
- **缓存时长**：最多保留5分钟
- **溢出策略**：FIFO，删除最旧的事件
- **批量发送**：连接恢复后使用同一个`events/publish`方法批量发送
  - 不换method，仍为`events/publish`
  - `params.events`数组包含多条事件
  - 按配置的BATCH_SIZE分批发送，避免单个消息过大

## 实现方案

### 初始化

注意：在实际集成时，需确保时间同步机制正常工作。未同步前先缓存事件但不发送；一旦 `IsTimesynced()==true` 或收到服务端时间校正，再附上正确的 `start_time`/`end_time` 发送缓存事件。

### 1. 创建MCP事件通知器

**文件位置**: `main/boards/ALichuangTest/interaction/mcp_event_notifier.h`

```cpp
#ifndef MCP_EVENT_NOTIFIER_H
#define MCP_EVENT_NOTIFIER_H

#include "event_engine.h"
#include "application.h"
#include "interaction/event_notification_config.h"
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>      // for std::min
#include <cJSON.h>

// cJSON智能指针，防止double free
struct CJsonDeleter { 
    void operator()(cJSON* p) const { 
        if (p) cJSON_Delete(p); 
    } 
};
using cjson_uptr = std::unique_ptr<cJSON, CJsonDeleter>;

class McpEventNotifier {
public:
    McpEventNotifier();
    ~McpEventNotifier();
    
    // 处理事件（决定立即发送或缓存）
    void HandleEvent(const Event& event);
    
    // 启用/禁用通知
    void Enable(bool enable) { enabled_ = enable; }
    
    // 连接状态回调
    void OnConnectionOpened();
    void OnConnectionClosed();
    
    // 时间同步状态回调
    void OnTimeSynced();
    
    // 设置事件过滤器
    void SetEventFilter(std::function<bool(const Event&)> filter) {
        event_filter_ = filter;
    }
    
private:
    struct CachedEvent {
        std::string event_type;
        int64_t start_time;     // 毫秒时间戳，整型
        int64_t end_time;       // 毫秒时间戳，整型
        std::string event_text; // 事件描述文本（与旧版字段保持一致）
        cjson_uptr metadata;    // 智能指针管理metadata，防止double free
        
        // 默认构造函数，unique_ptr自动初始化为nullptr
        CachedEvent() = default;
        
        // 支持移动，禁止拷贝（防止double free）
        CachedEvent(CachedEvent&&) = default;
        CachedEvent& operator=(CachedEvent&&) = default;
        CachedEvent(const CachedEvent&) = delete;
        CachedEvent& operator=(const CachedEvent&) = delete;
        
        // 无需自定义析构函数，unique_ptr自动管理内存
    };
    
    // 转换事件格式
    CachedEvent ConvertEvent(const Event& event);
    std::string GetEventTypeString(EventType type);
    std::string GenerateEventText(const Event& event);  // 生成event_text字段
    cjson_uptr GenerateEventMetadata(const Event& event); // 返回智能指针
    
    // 泛型发送事件（模板定义在头文件，避免链接问题）
    template<class It>
    void SendEvents(It first, It last) {
        if (first == last) return;
        
        std::string payload = BuildNotificationPayload(first, last);
        Application::GetInstance().SendMcpMessage(payload);
    }
    
    // 泛型构建MCP Notification payload（模板定义在头文件）
    template<class It>
    std::string BuildNotificationPayload(It first, It last) {
        cJSON* notification = cJSON_CreateObject();
        cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
        cJSON_AddStringToObject(notification, "method", "events/publish");
        
        cJSON* params = cJSON_CreateObject();
        cJSON* events_array = cJSON_CreateArray();
        
        for (auto it = first; it != last; ++it) {
            const auto& event = *it;
            cJSON* event_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(event_obj, "event_type", event.event_type.c_str());
            cJSON_AddNumberToObject(event_obj, "start_time", event.start_time);  // 整型毫秒
            cJSON_AddNumberToObject(event_obj, "end_time", event.end_time);
            cJSON_AddStringToObject(event_obj, "event_text", event.event_text.c_str());
            
            if (event.metadata) {
                cJSON_AddItemToObject(event_obj, "metadata", 
                                    cJSON_Duplicate(event.metadata.get(), true));
            }
            
            cJSON_AddItemToArray(events_array, event_obj);
        }
        
        cJSON_AddItemToObject(params, "events", events_array);
        cJSON_AddItemToObject(notification, "params", params);
        
        char* json_str = cJSON_PrintUnformatted(notification);
        std::string result(json_str);
        
        cJSON_free(json_str);
        cJSON_Delete(notification);
        
        return result;
    }
    
    // 检查MCP通道状态（避免"音频通道"误导）
    bool IsMcpChannelOpened() const;
    
    // 检查时间是否已同步（避免1970时间戳）
    bool IsTimesynced() const;
    
    bool enabled_;
    std::vector<CachedEvent> event_cache_;
    std::mutex cache_mutex_;
    std::function<bool(const Event&)> event_filter_;
    
    // 事件序列号，用于生成唯一event_id
    std::atomic<uint32_t> event_sequence_{0};
    std::string device_id_;  // 设备唯一标识
    bool time_synced_;       // 时间同步状态
    
    // 注意：缓存大小等配置统一从EventNotificationConfig获取，避免重复定义
};

#endif // MCP_EVENT_NOTIFIER_H
```

**重要说明**：
1. **内存安全**：使用`std::unique_ptr<cJSON, CJsonDeleter>`防止double free
   - **问题**：原始`cJSON*`在vector扩容时会被复制，导致多个对象持有同一指针
   - **解决**：`unique_ptr`不可复制只可移动，确保所有权唯一性
   - **效果**：vector扩容时自动走移动语义，转移所有权，不会double free

2. **避免额外容器拷贝**：使用模板迭代器接口避免临时对象
   - **优势**：`template<class It> SendEvents(It first, It last)`支持任意迭代器范围
   - **性能**：无需构造临时vector，直接操作原始容器的迭代器
   - **灵活性**：支持单个事件、批量事件、范围事件等多种场景
   - **链接安全**：模板定义在头文件中，避免跨TU调用时的ODR/链接错误

**文件位置**: `main/boards/ALichuangTest/interaction/mcp_event_notifier.cc`

```cpp
#include "mcp_event_notifier.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>      // for esp_efuse_mac_get_default
#include <sys/time.h>        // for gettimeofday

#define TAG "McpEventNotifier"

McpEventNotifier::McpEventNotifier() 
    : enabled_(false), time_synced_(false) {
    // 获取设备ID（可以从MAC地址、芯片ID等生成）
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char device_id_buf[18];
    snprintf(device_id_buf, sizeof(device_id_buf), 
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    device_id_ = device_id_buf;
}

McpEventNotifier::~McpEventNotifier() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    event_cache_.clear();
}

void McpEventNotifier::HandleEvent(const Event& event) {
    if (!enabled_) {
        return;
    }
    
    // 应用事件过滤器
    if (event_filter_ && !event_filter_(event)) {
        return;
    }
    
    // 在锁外进行事件转换，避免长时间持锁
    CachedEvent cached = ConvertEvent(event);
    
    if (IsMcpChannelOpened() && IsTimesynced()) {
        // MCP通道已建立且时间已同步，立即发送
        CachedEvent events[] = {std::move(cached)};
        SendEvents(events, events + 1);
        ESP_LOGI(TAG, "Event sent immediately: %s", events[0].event_type.c_str());
    } else {
        // 缓存事件（最小锁粒度）
        std::string event_type;
        size_t cache_size;
        
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            if (event_cache_.size() >= EventNotificationConfig::MAX_CACHE_SIZE) {
                // 删除最旧的事件
                event_cache_.erase(event_cache_.begin());
                ESP_LOGW(TAG, "Event cache full, dropping oldest event");
            }
            event_type = cached.event_type; // 拷贝用于日志
            event_cache_.emplace_back(std::move(cached));
            cache_size = event_cache_.size();
        }
        
        // 锁外进行日志输出
        ESP_LOGI(TAG, "Event cached: %s (cache size: %zu)", 
                 event_type.c_str(), cache_size);
    }
}

void McpEventNotifier::OnConnectionOpened() {
    std::vector<CachedEvent> events_to_send;
    
    // 在锁内快速移动缓存事件，释放锁后再序列化
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (!event_cache_.empty()) {
            ESP_LOGI(TAG, "Connection opened, sending %zu cached events", 
                     event_cache_.size());
            events_to_send = std::move(event_cache_);
            event_cache_.clear();
        }
    }
    
    // 检查时间同步状态，未同步则继续缓存
    if (!IsTimesynced()) {
        ESP_LOGW(TAG, "Time not synced, keeping events cached until time sync");
        std::lock_guard<std::mutex> lock(cache_mutex_);
        event_cache_ = std::move(events_to_send);
        return;
    }
    
    // 在锁外进行JSON序列化和网络发送
    if (!events_to_send.empty()) {
        // 分批发送以避免单个消息过大
        const int BATCH_SIZE = EventNotificationConfig::BATCH_SIZE;
        for (size_t i = 0; i < events_to_send.size(); i += BATCH_SIZE) {
            size_t end = std::min(i + BATCH_SIZE, events_to_send.size());
            
            // 直接使用迭代器范围，避免额外容器拷贝
            SendEvents(events_to_send.begin() + i, events_to_send.begin() + end);
        }
    }
}

void McpEventNotifier::OnConnectionClosed() {
    ESP_LOGI(TAG, "Connection closed, events will be cached");
}

void McpEventNotifier::OnTimeSynced() {
    std::vector<CachedEvent> events_to_send;
    
    // 时间同步后，发送所有缓存的事件
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        time_synced_ = true;
        
        if (!event_cache_.empty() && IsMcpChannelOpened()) {
            ESP_LOGI(TAG, "Time synced, sending %zu cached events", 
                     event_cache_.size());
            events_to_send = std::move(event_cache_);
            event_cache_.clear();
        }
    }
    
    // 发送缓存的事件（附上正确的时间戳）
    if (!events_to_send.empty()) {
        // 更新时间戳为当前同步后的时间
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        int64_t current_time_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
        
        for (auto& event : events_to_send) {
            // 保持事件的相对时长，但更新为同步后的绝对时间
            int64_t duration = event.end_time - event.start_time;
            event.start_time = current_time_ms;
            event.end_time = current_time_ms + duration;
        }
        
        // 分批发送
        const int BATCH_SIZE = EventNotificationConfig::BATCH_SIZE;
        for (size_t i = 0; i < events_to_send.size(); i += BATCH_SIZE) {
            size_t end = std::min(i + BATCH_SIZE, events_to_send.size());
            SendEvents(events_to_send.begin() + i, events_to_send.begin() + end);
        }
    }
}

// 模板函数已移至头文件，避免链接问题

CachedEvent McpEventNotifier::ConvertEvent(const Event& event) {
    CachedEvent cached;
    cached.event_type = GetEventTypeString(event.type);
    
    // 获取当前时间戳（Unix epoch毫秒，整型）
    // 注意：esp_timer_get_time()返回的是系统启动后的微秒数，不是Unix时间
    // 实际应用中需要使用gettimeofday()或其他方式获取真实Unix时间
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t current_time_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    
    cached.start_time = current_time_ms;
    
    // 如果有持续时间，计算end_time；否则end_time等于start_time+1ms
    if (event.duration_ms > 0) {
        cached.end_time = current_time_ms + event.duration_ms;
    } else {
        cached.end_time = current_time_ms + 1; // 默认1毫秒持续时间
    }
    
    cached.event_text = GenerateEventText(event);
    cached.metadata = GenerateEventMetadata(event);
    return cached; // 移动语义自动生效，转移unique_ptr所有权
}

std::string McpEventNotifier::GetEventTypeString(EventType type) {
    switch (type) {
        // 触摸事件
        case EventType::TOUCH_TICKLED: return "tickled";
        case EventType::TOUCH_CRADLED: return "cradled";
        case EventType::TOUCH_SINGLE_TAP: return "tap";
        case EventType::TOUCH_DOUBLE_TAP: return "double_tap";
        case EventType::TOUCH_LONG_PRESS: return "long_press";
        case EventType::TOUCH_HOLD: return "hold";
        case EventType::TOUCH_RELEASE: return "release";
        
        // 运动事件
        case EventType::MOTION_SHAKE: return "shake";
        case EventType::MOTION_SHAKE_VIOLENTLY: return "shake_violently";
        case EventType::MOTION_FLIP: return "flip";
        case EventType::MOTION_FREE_FALL: return "free_fall";
        case EventType::MOTION_PICKUP: return "pickup";
        case EventType::MOTION_UPSIDE_DOWN: return "upside_down";
        case EventType::MOTION_TILT: return "tilt";
        
        default: return "unknown";
    }
}

// 不再需要GetNotificationMethod函数，统一使用events/publish

std::string McpEventNotifier::GenerateEventText(const Event& event) {
    // 生成供LLM理解的event_text（与旧版字段保持一致）
    switch (event.type) {
        case EventType::TOUCH_TICKLED:
            return "User tickled the device with multiple rapid touches";
        case EventType::TOUCH_CRADLED:
            return "Device is being held gently on both sides";
        case EventType::MOTION_SHAKE:
            return "Device was shaken by user";
        case EventType::MOTION_SHAKE_VIOLENTLY:
            return "Device was shaken violently";
        case EventType::MOTION_FLIP:
            return "Device was flipped over";
        case EventType::MOTION_FREE_FALL:
            return "Device is in free fall - possible drop";
        case EventType::MOTION_PICKUP:
            return "Device was picked up by user";
        case EventType::TOUCH_SINGLE_TAP:
            return "User tapped the device once";
        case EventType::TOUCH_LONG_PRESS:
            return "User performed a long press";
        default:
            return "User interacted with the device";
    }
}

cjson_uptr McpEventNotifier::GenerateEventMetadata(const Event& event) {
    cJSON* metadata = cJSON_CreateObject();
    
    // 生成唯一的event_id用于去重
    uint32_t seq = event_sequence_.fetch_add(1);
    char event_id[64];
    snprintf(event_id, sizeof(event_id), "%s-%lld-%u", 
             device_id_.c_str(), 
             (long long)esp_timer_get_time(), 
             seq);
    cJSON_AddStringToObject(metadata, "event_id", event_id);
    
    // 根据事件类型添加相关元数据
    switch (event.type) {
        case EventType::TOUCH_TICKLED:
            if (event.touch_count > 0) {
                cJSON_AddNumberToObject(metadata, "touch_count", event.touch_count);
            }
            break;
            
        case EventType::MOTION_SHAKE:
        case EventType::MOTION_SHAKE_VIOLENTLY:
            if (event.intensity > 0) {
                cJSON_AddNumberToObject(metadata, "intensity", event.intensity);
            }
            break;
            
        case EventType::MOTION_TILT:
            if (event.angle > 0) {
                cJSON_AddNumberToObject(metadata, "angle", event.angle);
            }
            break;
            
        default:
            break;
    }
    
    // 如果只有event_id，仍然返回（event_id总是需要的）
    // 如果真的没有任何内容，删除并返回nullptr
    if (cJSON_GetArraySize(metadata) == 0) {
        cJSON_Delete(metadata);
        return cjson_uptr{nullptr};
    }
    
    return cjson_uptr{metadata}; // 转移所有权到unique_ptr
}

bool McpEventNotifier::IsMcpChannelOpened() const {
    auto& app = Application::GetInstance();
    // 检查MCP/WebSocket连接状态
    // 注：当前复用现有音频连接判断，建议Protocol层补充更通用的接口：
    // - Protocol::IsControlChannelOpened() 或 
    // - Protocol::IsJsonChannelOpened()
    // 避免与音频概念混淆，握手/会话可独立于音频存在
    return app.GetProtocol() && app.GetProtocol()->IsAudioChannelOpened();
}

bool McpEventNotifier::IsTimesynced() const {
    if (!time_synced_) {
        // 检查系统时间是否合理（不是1970年）
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        // 如果时间戳小于2020年1月1日，认为时间未同步
        const int64_t MIN_VALID_TIMESTAMP = 1577836800; // 2020-01-01 00:00:00 UTC
        return tv.tv_sec > MIN_VALID_TIMESTAMP;
    }
    return true;
}
```

### 2. 集成到ALichuangTest板级代码

修改 `main/boards/ALichuangTest/ALichuangTest.h`:

```cpp
#include "interaction/mcp_event_notifier.h"

class ALichuangTest : public WifiBoard {
private:
    // 现有成员...
    std::unique_ptr<McpEventNotifier> mcp_event_notifier_;
    
public:
    ALichuangTest() {
        // 现有初始化...
        
        // 创建MCP事件通知器
        mcp_event_notifier_ = std::make_unique<McpEventNotifier>();
        
        // 配置事件过滤器（可选）
        mcp_event_notifier_->SetEventFilter([](const Event& event) {
            // 过滤掉不需要上报的事件
            return true; // 默认上报所有事件
        });
        
        // 注册事件回调
        if (event_engine_) {
            event_engine_->RegisterCallback([this](const Event& event) {
                if (mcp_event_notifier_) {
                    mcp_event_notifier_->HandleEvent(event);
                }
            });
        }
    }
    
    // 供Application使用的访问器
    McpEventNotifier* GetEventNotifier() { 
        return mcp_event_notifier_.get(); 
    }
};
```

### 3. Application集成（最小化修改）

首先确保`Application::SendMcpMessage()`方法存在，如果不存在需要添加：

```cpp
// 在application.h中声明（需要添加#include <cJSON.h>）
void SendMcpMessage(const std::string& payload);

// 在application.cc中实现（使用cJSON避免转义风险）
void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload]() {
        if (!protocol_) return;
        
        // 使用cJSON构建外层消息，避免字符串拼接的转义风险
        cJSON* outer = cJSON_CreateObject();
        cJSON_AddStringToObject(outer, "session_id", session_id_.c_str());
        cJSON_AddStringToObject(outer, "type", "mcp");
        
        // 解析内层JSON-RPC payload
        cJSON* inner = cJSON_Parse(payload.c_str());
        if (!inner) { 
            ESP_LOGE("Application", "Invalid JSON payload: %s", payload.c_str());
            cJSON_Delete(outer); 
            return; 
        }
        
        cJSON_AddItemToObject(outer, "payload", inner);
        
        char* json_str = cJSON_PrintUnformatted(outer);
        if (json_str) {
            protocol_->SendText(json_str);
            cJSON_free(json_str);
        }
        cJSON_Delete(outer);
    });
}
```

然后在连接管理回调中添加少量代码：

```cpp
void Application::OnAudioChannelOpened() {
    ESP_LOGI(TAG, "Audio channel opened");
    
    // 通知MCP事件通知器连接已建立
    auto& board = Board::GetInstance();
    if (auto* alichuang = dynamic_cast<ALichuangTest*>(&board)) {
        if (auto* notifier = alichuang->GetEventNotifier()) {
            notifier->Enable(true);
            notifier->OnConnectionOpened();
        }
    }
    
    // 其他现有逻辑...
}

void Application::OnAudioChannelClosed() {
    // 通知MCP事件通知器连接已关闭
    auto& board = Board::GetInstance();
    if (auto* alichuang = dynamic_cast<ALichuangTest*>(&board)) {
        if (auto* notifier = alichuang->GetEventNotifier()) {
            notifier->OnConnectionClosed();
        }
    }
    
    // 其他现有逻辑...
}

// 建议添加时间同步回调处理
void Application::OnTimeSynchronized() {
    // 当SNTP同步完成或收到服务端时间校正时调用
    auto& board = Board::GetInstance();
    if (auto* alichuang = dynamic_cast<ALichuangTest*>(&board)) {
        if (auto* notifier = alichuang->GetEventNotifier()) {
            notifier->OnTimeSynced();
        }
    }
}
```

## 配置管理

### 事件通知配置

创建 `main/boards/ALichuangTest/interaction/event_notification_config.h`:

```cpp
#ifndef EVENT_NOTIFICATION_CONFIG_H
#define EVENT_NOTIFICATION_CONFIG_H

// MCP事件通知配置
struct EventNotificationConfig {
    // 基本开关
    static constexpr bool ENABLED = true;
    
    // 通知策略
    static constexpr bool IMMEDIATE_SEND = true;         // 立即发送
    static constexpr int MAX_CACHE_SIZE = 20;            // 最大缓存数（统一配置）
    static constexpr int CACHE_TIMEOUT_MS = 300000;      // 5分钟超时
    static constexpr int BATCH_SIZE = 10;                // 批量发送大小
    
    // 事件类型过滤
    static constexpr bool NOTIFY_TOUCH_EVENTS = true;
    static constexpr bool NOTIFY_MOTION_EVENTS = true;
    static constexpr bool NOTIFY_DEVICE_STATE = true;
    
    // 调试选项
    static constexpr bool LOG_NOTIFICATIONS = true;
    static constexpr bool LOG_VERBOSE = false;
};

// 事件优先级定义已移至"实时发送策略"章节

#endif // EVENT_NOTIFICATION_CONFIG_H
```

## 测试验证

### 1. 单元测试
```cpp
// 测试MCP Notification payload格式
void TestNotificationFormat() {
    McpEventNotifier notifier;
    Event test_event{EventType::TOUCH_TICKLED, /* ... */};
    
    // 验证生成的JSON格式
    std::string payload = notifier.BuildTestPayload(test_event);
    cJSON* json = cJSON_Parse(payload.c_str());
    
    assert(cJSON_GetObjectItem(json, "jsonrpc"));
    assert(cJSON_GetObjectItem(json, "method"));
    assert(!cJSON_GetObjectItem(json, "id")); // Notification不应有id
    
    cJSON_Delete(json);
}

// 测试内存安全（防止double free）
void TestMemorySafety() {
    McpEventNotifier notifier;
    std::vector<Event> events;
    
    // 添加大量事件，触发vector扩容
    for (int i = 0; i < 100; ++i) {
        Event event{EventType::TOUCH_TICKLED, /* ... */};
        events.push_back(event);
        notifier.HandleEvent(event); // 这会导致CachedEvent的移动和扩容
    }
    
    // 如果没有崩溃，说明unique_ptr正确管理了内存
    // 析构时会自动清理，不会double free
}
```

### 2. 集成测试
- 测试连接建立后事件立即发送
- 测试断线重连后缓存事件批量发送
- 测试不同类型事件的method路由

### 3. 端到端测试
- 触发各种交互事件，验证服务器接收
- 验证LLM对事件描述的理解和响应
- 测试高频事件的性能影响

## 服务器端处理

服务器在JSON-RPC分发器中注册事件处理器：

```python
def handle_mcp_message(message):
    payload = message.get('payload', {})
    
    # 检查是否为notification（无id字段）
    if 'id' not in payload and 'method' in payload:
        method = payload['method']
        params = payload.get('params', {})
        
        if method == 'events/publish':
            # 统一的事件处理入口
            for event in params.get('events', []):
                event_type = event.get('event_type')
                # 根据event_type进行分发处理
                # 入队列/写数据库/推送LLM
                process_event(event)
        
        # Notification不需要响应
        return None

def process_event(event):
    """处理单个事件"""
    event_type = event.get('event_type')
    
    # 字段兼容性处理（支持新旧字段混合）
    # 统一转换为标准字段名
    if 'timestamp' in event and 'start_time' not in event:
        event['start_time'] = event['timestamp']
    if 'duration_ms' in event and 'end_time' not in event:
        event['end_time'] = event['start_time'] + event['duration_ms']
    if 'description' in event and 'event_text' not in event:
        event['event_text'] = event['description']
    
    # 写入数据库
    db.write_event(event)
    
    # 推送到LLM上下文
    if event_type in ['tickled', 'cradled', 'shake']:
        llm_context.add_interaction(event)
    
    # 触发相应的业务逻辑
    if event_type == 'free_fall':
        handle_emergency_event(event)
```

## 性能优化

### 内存管理
- **智能指针**：使用`unique_ptr`自动管理cJSON内存，防止double free
- **移动语义**：CachedEvent支持移动不支持拷贝，确保所有权清晰
- **避免额外容器拷贝**：模板迭代器接口避免临时vector构造，直接操作原始迭代器
- **锁粒度优化**：将JSON序列化移到锁外，减少持锁时间
- **缓存结构**：当前使用`vector`，容量20性能OK。若扩大可考虑`std::deque`适合频繁头删
- **RAII原则**：所有资源自动管理，析构时自动清理

### 网络优化
- 批量发送减少网络开销
- 使用压缩（如果协议支持）
- 避免发送冗余信息

### CPU优化
- 异步处理避免阻塞主循环
- 使用高效的JSON库
- 缓存常用字符串

## 安全考虑

### 隐私保护
- 不在事件中包含用户敏感信息
- 描述文本避免暴露隐私
- 遵循数据最小化原则

### 防止滥用
- 限制事件发送频率
- 验证事件数据合法性
- 防止恶意事件注入

### Metadata安全限制
- **服务端白名单**：只接受预定义的metadata字段（如event_id、touch_count、intensity等）
- **大小上限**：每个事件的metadata总大小不超过1KB
- **键数限制**：metadata对象最多包含16个键值对
- **类型检查**：验证metadata值的数据类型（字符串、数字、布尔值）
- **内容过滤**：拒绝包含特殊字符或过长字符串的metadata
- **路由性能**：避免过大JSON影响路由线程，务必在接收端校验

## 扩展能力

### 未来可扩展的事件类型

通过`events/publish`统一发送，使用event_type区分：

```json
{
  "event_type": "battery_low",      // 电量事件
  "event_type": "network_changed",  // 网络状态
  "event_type": "pattern_detected", // 行为模式
  "event_type": "gesture_swipe",    // 手势识别
  "event_type": "proximity_near",   // 接近感应
  "event_type": "light_changed"     // 环境光线
}
```

所有新增事件类型都通过同一个`events/publish`方法发送，服务端根据event_type字段路由处理。

### 自定义事件支持
```cpp
class CustomEventNotifier : public McpEventNotifier {
    // 继承并扩展，支持应用特定的事件类型
};
```

## 迁移路径

### 从旧协议迁移到MCP Notification

1. **阶段1**：实现MCP Notification发送器
2. **阶段2**：服务器同时支持两种格式
3. **阶段3**：新设备使用MCP，旧设备继续使用原协议
4. **阶段4**：逐步升级旧设备固件
5. **阶段5**：完全迁移到MCP Notification

## 总结

使用统一的`events/publish`方法实现事件上传具有以下优势：

✅ **最小侵入**：事件字段与旧版保持一致，服务端改动极小  
✅ **极简设计**：单一method处理所有事件，无需多个路由  
✅ **标准化**：遵循JSON-RPC 2.0 Notification规范（无id，不回包）  
✅ **兼容性好**：`start_time`/`end_time`/`event_text`字段名不变  
✅ **统一协议**：外层统一`type: "mcp"`，内层JSON-RPC格式  
✅ **易于扩展**：新增事件类型只需定义新的event_type值  

关键设计原则：
- **字段兼容**：沿用旧版字段名，避免服务端大改
- **协议统一**：所有消息走MCP通道，`type: "mcp"`
- **方法唯一**：`events/publish`处理所有事件类型
- **语义一致**：Notification不回包，符合设备主动推送场景

通过本方案，在保持最大兼容性的前提下，实现了标准、高效的设备事件推送系统。