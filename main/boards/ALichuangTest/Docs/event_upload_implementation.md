# 事件上传功能实现指南

## 概述
本文档描述如何将交互事件（触摸事件、运动事件等）上传到服务器。采用独立的事件上传机制，减少对主程序的侵入性。

**关键设计原则**：
- 外层消息使用`type: "lx/v1/event"`，独立于MCP协议
- 使用`start_time`和`end_time`时间字段，清晰表示事件时间范围
- 事件数据存储在`event_payload`中，包含原`metadata`内容
- 独立的上传方法，避免与MCP工具调用混淆
- 最小化对现有Application架构的影响

**关键改进点**：
- ✅ **架构独立**：独立的事件上传，不依赖MCP协议栈
- ✅ **时间字段简化**：使用`start_time`和`end_time`，语义清晰
- ✅ **数据结构统一**：所有事件属性统一在`event_payload`中
- ✅ **内存安全**：使用智能指针管理JSON对象，防止内存泄漏
- ✅ **连接状态管理**：直接检查连接状态
- ✅ **批量发送支持**：支持事件批量上传，提高网络效率
- ✅ **最小侵入性**：独立的发送方法，减少对Application主逻辑的影响

## 架构设计

### 事件上传优势
- **协议独立**：不依赖MCP协议，避免与工具调用混淆
- **直接通信**：通过现有连接直接发送，无需额外协议层
- **无响应开销**：单向事件推送，不需要服务器响应
- **简单高效**：JSON格式直观，处理逻辑简单
- **扩展性强**：便于未来添加新的事件类型和字段

### 事件流程
```
用户交互 → TouchEngine/MotionEngine → EventProcessor(防抖/节流/冷却) 
    → EventEngine → Event Uploader → 服务器 → LLM
```

## 事件上传消息格式

### 基础消息结构
通过独立的发送方法发送的完整消息格式：
```json
{
  "session_id": "9aa008fa-c874-4829-b70b-fca7fa30e3da",
  "type": "lx/v1/event",
  "payload": {
    "events": [
      {
        "event_type": "tickled",
        "event_payload": {
          "touch_count": 5,
          "position": "both",
          "intensity": 3
        },
        "event_text": "主人在挠我痒痒，好痒啊",
        "start_time": 1755222858360,
        "end_time": 1755222858360
      },
      {
        "event_type": "long_press",
        "event_payload": {
          "position": "left",
          "pressure": 0.8
        },
        "event_text": "主人长时间按住了我的左侧",
        "start_time": 1755222860000,
        "end_time": 1755222862500
      }
    ]
  }
}
```

### 事件上传机制

采用独立的连接直接上传事件：

- **协议类型**: `lx/v1/event`
- **传输方式**: 直接发送
- **响应模式**: 单向推送，无需服务器响应
- **优势**: 
  - 协议简单，易于实现和调试
  - 独立于MCP系统，避免耦合
  - 直接的事件到服务器通道
  - 便于服务端分类处理（入队/写库/推LLM）

## 事件数据结构

### 事件参数格式
```typescript
interface EventMessage {
  session_id: string;
  type: "lx/v1/event";
  payload: EventPayload;
}

interface EventPayload {
  events: Event[];
}

interface Event {
  event_type: string;        // 事件类型标识
  event_payload: {           // 事件具体数据（原metadata内容）
    event_id?: string;       // 事件唯一ID，用于去重
    [key: string]: any;      // 其他事件相关数据
  };
  event_text: string;        // 事件描述文本（供LLM理解）
  start_time: number;        // 事件开始时间戳（ms since epoch）
  end_time: number;          // 事件结束时间戳（ms since epoch）
}
```

### 设备端事件类型映射

#### 触摸事件映射
```cpp
// TouchEventType → EventType → event_type字符串 → event_text(区分左右)

// ✅ 需要上传的触摸事件
TouchEventType::SINGLE_TAP   → EventType::TOUCH_TAP        → "tap"        → "主人轻轻拍了我的左侧/右侧"
TouchEventType::HOLD         → EventType::TOUCH_LONG_PRESS → "long_press" → "主人长时间按住了我的左侧/右侧"
TouchEventType::CRADLED      → EventType::TOUCH_CRADLED    → "cradled"    → "主人温柔地抱着我"
TouchEventType::TICKLED      → EventType::TOUCH_TICKLED    → "tickled"    → "主人在挠我痒痒"

// ❌ 不上传的事件
TouchEventType::RELEASE      → EventType::MOTION_NONE     // 释放事件（无需上传）

// 触摸位置和时间信息存储在 event_payload 中：
// - position: "left"/"right"/"both"
// - start_time: 事件开始时间戳
// - end_time: 根据duration计算（start_time + duration，无duration时等于start_time）
```

#### 运动事件映射
```cpp
// MotionEventType → event_type字符串 → event_text
EventType::MOTION_SHAKE           → "shake"           → "主人轻轻摇了摇我"
EventType::MOTION_SHAKE_VIOLENTLY → "shake_violently" → "主人用力摇晃我" 
EventType::MOTION_FLIP            → "flip"            → "主人把我翻了个身"
EventType::MOTION_FREE_FALL       → "free_fall"       → "糟糕，我掉下去了"
EventType::MOTION_PICKUP          → "pickup"          → "主人把我拿起来了"
EventType::MOTION_UPSIDE_DOWN     → "upside_down"     → "主人把我倒立起来了"

// 运动事件的时间处理：
// - 瞬时事件：start_time = end_time = 当前时间戳
// - 持续事件：end_time = start_time + duration（如适用）
```

## 发送策略

### 实时发送策略
由于本地EventProcessor已经实施了防抖、节流、冷却等策略，到达上传阶段的事件都是需要及时处理的：

1. **连接已建立时**：立即发送事件，无需等待
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
- **连接复用**：使用现有连接通道，无需单独建立
- **连接检测**：直接检查连接状态
- **智能重连**：连接断开后自动缓存事件，重连后发送

### 缓存策略
- **最大缓存数**：20个事件（本地已过滤，不会太多）
- **缓存时长**：最多保留5分钟
- **溢出策略**：FIFO，删除最旧的事件
- **批量发送**：连接恢复后批量发送多个事件
  - 在同一个payload.events数组中包含多条事件
  - 按配置的BATCH_SIZE分批发送，避免单个消息过大

## 实现方案

### 初始化

注意：在实际集成时，需确保时间同步机制正常工作。未同步前先缓存事件但不发送；一旦 `IsTimesynced()==true` 或收到服务端时间校正，再附上正确的 `timestamp` 发送缓存事件。

### 1. 创建事件上传器

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
        int64_t start_time;     // 事件开始时间（Unix时间戳，毫秒）
        int64_t end_time;       // 事件结束时间（Unix时间戳，毫秒）
        std::string event_text; // 事件描述文本
        cjson_uptr event_payload;    // 事件具体数据，智能指针管理防止double free
        
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
    cjson_uptr GenerateEventPayload(const Event& event); // 返回智能指针
    int64_t CalculateEndTime(const Event& event, int64_t start_time);  // 计算结束时间
    
    // 泛型发送事件（模板定义在头文件，避免链接问题）
    template<class It>
    void SendEvents(It first, It last) {
        if (first == last) return;
        
        std::string message = BuildEventMessage(first, last);
        Application::GetInstance().SendEventMessage(message);
    }
    
    // 泛型构建事件消息 payload（模板定义在头文件）
    template<class It>
    std::string BuildEventMessage(It first, It last) {
        cJSON* notification = cJSON_CreateObject();
        cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
        cJSON_AddStringToObject(notification, "method", "events/publish");
        
        cJSON* params = cJSON_CreateObject();
        cJSON* events_array = cJSON_CreateArray();
        
        for (auto it = first; it != last; ++it) {
            const auto& event = *it;
            cJSON* event_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(event_obj, "event_type", event.event_type.c_str());
            cJSON_AddNumberToObject(event_obj, "timestamp", event.timestamp_ms);  // 整型毫秒
            
            // 只有持续时间大于0时才添加duration_ms字段
            if (event.duration_ms > 0) {
                cJSON_AddNumberToObject(event_obj, "duration_ms", event.duration_ms);
            }
            
            cJSON_AddStringToObject(event_obj, "event_text", event.event_text.c_str());
            
            if (event.event_payload) {
                cJSON_AddItemToObject(event_obj, "event_payload", 
                                    cJSON_Duplicate(event.event_payload.get(), true));
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
    bool IsConnected() const;
    
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

#endif // WEBSOCKET_EVENT_UPLOADER_H
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

**文件位置**: `main/boards/ALichuangTest/interaction/event_uploader.cc`

```cpp
#include "event_uploader.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>      // for esp_efuse_mac_get_default
#include <sys/time.h>        // for gettimeofday

#define TAG "EventUploader"

EventUploader::EventUploader() 
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

EventUploader::~EventUploader() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    event_cache_.clear();
}

void EventUploader::HandleEvent(const Event& event) {
    if (!enabled_) {
        return;
    }
    
    // 应用事件过滤器
    if (event_filter_ && !event_filter_(event)) {
        return;
    }
    
    // 在锁外进行事件转换，避免长时间持锁
    CachedEvent cached = ConvertEvent(event);
    
    if (IsConnected() && IsTimesynced()) {
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

void EventUploader::OnConnectionOpened() {
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

void EventUploader::OnConnectionClosed() {
    ESP_LOGI(TAG, "Connection closed, events will be cached");
}

void EventUploader::OnTimeSynced() {
    std::vector<CachedEvent> events_to_send;
    
    // 时间同步后，发送所有缓存的事件
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        time_synced_ = true;
        
        if (!event_cache_.empty() && IsConnected()) {
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
            // 更新时间戳为同步后的时间，保持duration不变
            event.timestamp_ms = current_time_ms;
            // duration_ms保持原值，不需要更新
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

CachedEvent EventUploader::ConvertEvent(const Event& event) {
    CachedEvent cached;
    cached.event_type = GetEventTypeString(event.type);
    
    // 获取当前时间戳（Unix epoch毫秒，整型）
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    cached.start_time = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    
    // 计算事件结束时间
    cached.end_time = CalculateEndTime(event, cached.start_time);
    
    cached.event_text = GenerateEventText(event);
    cached.event_payload = GenerateEventPayload(event);
    return cached; // 移动语义自动生效，转移unique_ptr所有权
}

// 计算事件结束时间
int64_t EventUploader::CalculateEndTime(const Event& event, int64_t start_time) {
    // 触摸事件从touch_data.y中获取持续时间
    if (event.type == EventType::TOUCH_LONG_PRESS || 
        event.type == EventType::TOUCH_TAP) {
        // touch_data.y存储了TouchEvent的duration_ms
        uint32_t duration_ms = static_cast<uint32_t>(event.data.touch_data.y);
        if (duration_ms > 0) {
            return start_time + duration_ms;
        }
    }
    
    // 其他事件或无持续时间的事件，end_time等于start_time
    return start_time;
}

std::string EventUploader::GetEventTypeString(EventType type) {
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


std::string EventUploader::GenerateEventText(const Event& event) {
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

cjson_uptr EventUploader::GenerateEventPayload(const Event& event) {
    cJSON* event_payload = cJSON_CreateObject();
    
    // 生成唯一的event_id用于去重
    uint32_t seq = event_sequence_.fetch_add(1);
    char event_id[64];
    snprintf(event_id, sizeof(event_id), "%s-%lld-%u", 
             device_id_.c_str(), 
             (long long)esp_timer_get_time(), 
             seq);
    cJSON_AddStringToObject(event_payload, "event_id", event_id);
    
    // 根据事件类型添加相关元数据
    switch (event.type) {
        case EventType::TOUCH_TICKLED:
            if (event.touch_count > 0) {
                cJSON_AddNumberToObject(event_payload, "touch_count", event.touch_count);
            }
            break;
            
        case EventType::MOTION_SHAKE:
        case EventType::MOTION_SHAKE_VIOLENTLY:
            if (event.intensity > 0) {
                cJSON_AddNumberToObject(event_payload, "intensity", event.intensity);
            }
            break;
            
        case EventType::MOTION_TILT:
            if (event.angle > 0) {
                cJSON_AddNumberToObject(event_payload, "angle", event.angle);
            }
            break;
            
        default:
            break;
    }
    
    // 如果只有event_id，仍然返回（event_id总是需要的）
    // 如果真的没有任何内容，删除并返回nullptr
    if (cJSON_GetArraySize(event_payload) == 0) {
        cJSON_Delete(event_payload);
        return cjson_uptr{nullptr};
    }
    
    return cjson_uptr{event_payload}; // 转移所有权到unique_ptr
}

bool EventUploader::IsConnected() const {
    auto& app = Application::GetInstance();
    // 检查连接状态
    // 注：当前复用现有音频连接判断，建议Protocol层补充更通用的接口：
    // - Protocol::IsControlChannelOpened() 或 
    // - Protocol::IsJsonChannelOpened()
    // 避免与音频概念混淆，握手/会话可独立于音频存在
    return app.GetProtocol() && app.GetProtocol()->IsAudioChannelOpened();
}

bool EventUploader::IsTimesynced() const {
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
#include "interaction/event_uploader.h"

class ALichuangTest : public WifiBoard {
private:
    // 现有成员...
    std::unique_ptr<EventUploader> event_uploader_;
    
public:
    ALichuangTest() {
        // 现有初始化...
        
        // 创建事件上传器
        event_uploader_ = std::make_unique<EventUploader>();
        
        // 配置事件过滤器（可选）
        event_uploader_->SetEventFilter([](const Event& event) {
            // 过滤掉不需要上报的事件
            return true; // 默认上报所有事件
        });
        
        // 注册事件回调
        if (event_engine_) {
            event_engine_->RegisterCallback([this](const Event& event) {
                if (event_uploader_) {
                    event_uploader_->HandleEvent(event);
                }
            });
        }
    }
    
    // 供Application使用的访问器
    EventUploader* GetEventUploader() { 
        return event_uploader_.get(); 
    }
};
```

### 3. Application集成（最小化修改）

首先确保`Application::SendEventMessage()`方法存在，如果不存在需要添加：

```cpp
// 在application.h中声明
 void SendEventMessage(const std::string& message);

// 在application.cc中实现（直接发送完整消息）
void Application::SendEventMessage(const std::string& message) {
    Schedule([this, message]() {
        if (!protocol_) return;
        
        // 直接发送完整的JSON消息，无需额外封装
        protocol_->SendText(message);
    });
}
```

然后在连接管理回调中添加少量代码：

```cpp
void Application::OnAudioChannelOpened() {
    ESP_LOGI(TAG, "Audio channel opened");
    
    // 通知事件上传器连接已建立
    auto& board = Board::GetInstance();
    if (auto* alichuang = dynamic_cast<ALichuangTest*>(&board)) {
        if (auto* uploader = alichuang->GetEventUploader()) {
            uploader->Enable(true);
            uploader->OnConnectionOpened();
        }
    }
    
    // 其他现有逻辑...
}

void Application::OnAudioChannelClosed() {
    // 通知事件上传器连接已关闭
    auto& board = Board::GetInstance();
    if (auto* alichuang = dynamic_cast<ALichuangTest*>(&board)) {
        if (auto* uploader = alichuang->GetEventUploader()) {
            uploader->OnConnectionClosed();
        }
    }
    
    // 其他现有逻辑...
}

// 建议添加时间同步回调处理
void Application::OnTimeSynchronized() {
    // 当SNTP同步完成或收到服务端时间校正时调用
    auto& board = Board::GetInstance();
    if (auto* alichuang = dynamic_cast<ALichuangTest*>(&board)) {
        if (auto* uploader = alichuang->GetEventUploader()) {
            uploader->OnTimeSynced();
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

服务器直接处理事件消息：

```python
def handle_event_message(message):
    # 检查消息类型
    if message.get('type') == 'lx/v1/event':
        payload = message.get('payload', {})
        events = payload.get('events', [])
        
        # 处理所有事件
        for event in events:
            process_event(event)
        
        return True  # 表示已处理
    
    return False  # 表示未处理，由其他处理器处理

def process_event(event):
    """处理单个事件"""
    event_type = event.get('event_type')
    
    # 字段处理
    start_time = event.get('start_time')
    end_time = event.get('end_time')
    event_text = event.get('event_text')
    event_payload = event.get('event_payload', {})
    
    # 计算持续时间（如需要）
    duration_ms = end_time - start_time if end_time > start_time else 0
    
    # 写入数据库
    db.write_event({
        'event_type': event_type,
        'start_time': start_time,
        'end_time': end_time,
        'duration_ms': duration_ms,
        'event_text': event_text,
        'event_payload': event_payload
    })
    
    # 推送到LLM上下文
    if event_type in ['shake', 'shake_violently', 'free_fall', 'long_press']:
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

### Event Payload安全限制
- **服务端白名单**：只接受预定义的event_payload字段（如event_id、touch_count、intensity等）
- **大小上限**：每个事件的event_payload总大小不超过1KB
- **键数限制**：event_payload对象最多包含16个键值对
- **类型检查**：验证event_payload值的数据类型（字符串、数字、布尔值）
- **内容过滤**：拒绝包含特殊字符或过长字符串的event_payload
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
✅ **简化设计**：`timestamp`/`duration_ms`/`event_text`更加清晰  
✅ **统一协议**：外层统一`type: "mcp"`，内层JSON-RPC格式  
✅ **易于扩展**：新增事件类型只需定义新的event_type值  

关键设计原则：
- **字段简化**：使用`timestamp`+`duration_ms`，避免冗余计算
- **协议统一**：所有消息走MCP通道，`type: "mcp"`
- **方法唯一**：`events/publish`处理所有事件类型
- **语义一致**：Notification不回包，符合设备主动推送场景

通过本方案，在保持最大兼容性的前提下，实现了标准、高效的设备事件推送系统。

---

## 后端开发沟通指南

### 🂯 核心信息

新增事件上传协议：`lx/v1/event`，用于接收设备交互事件。

### 📝 消息格式
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
          "timestamp": 1755222858360,
          "event_text": "主人在挠我痒痒，好痒啊",
          "metadata": {
            "touch_count": 5,
            "position": "both"
          }
        },
        {
          "event_type": "long_press",
          "timestamp": 1755222860000,
          "duration_ms": 2500,
          "event_text": "主人长时间按住了我的左侧",
          "metadata": {
            "position": "left"
          }
        }
      ]
    }
  }
}
```

### 🔧 实现方式

**步骤1**: 在 MCP 路由器中添加新方法
```python
@mcp_handler.method("events/publish")
def handle_events_publish(params):
    events = params.get('events', [])
    for event in events:
        event_text = event.get('event_text', '')
        print(f"Received device event: {event_text}")
    return None  # Notification无需响应
```

**步骤2**: 提取 event_text 字段

```python
# 单个事件处理示例
def process_event(event):
    event_type = event.get('event_type')        # 事件类型："tap", "shake" 等
    event_text = event.get('event_text')        # 事件描述："主人轻轻拍了我一下"
    timestamp = event.get('timestamp')          # 时间戳
    duration_ms = event.get('duration_ms', 0)   # 可选：持续时间
    
    # 你的业务逻辑...
    print(f"{event_type}: {event_text}")
```

### 📝 事件类型列表

**✅ 触摸事件**（区分左右位置）：
- `tap` - 轻拍（主人轻轻拍了我的左侧/右侧，<500ms）
- `long_press` - 长按（主人长时间按住了我的左侧/右侧，>500ms）
- `cradled` - 摇篮（主人温柔地抱着我，双侧持续触摸>2秒且IMU静止）
- `tickled` - 挠痒（主人在挠我痒痒，好痒啊，2秒内多次无规律触摸>4次）

**✅ 运动事件**：
- `shake` - 轻摇（主人轻轻摇了摇我）
- `shake_violently` - 用力摇（主人用力摇晃我）
- `flip` - 翻身（主人把我翻了个身）
- `free_fall` - 掉落（糟糕，我掉下去了）
- `pickup` - 被拿起（主人把我拿起来了）
- `upside_down` - 倒立（主人把我倒立起来了）

**触摸位置信息**：
- 左侧触摸：metadata.position = "left"
- 右侧触摸：metadata.position = "right"  
- 双侧触摸：metadata.position = "both"

### ⚠️ 注意

- **JSON-RPC 2.0 Notification**: 无需返回响应
- **批量事件**: `params.events` 是数组，可能包含多个事件