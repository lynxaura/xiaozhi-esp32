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
        "event_type": "Touch_Both_Tickled",
        "event_text": "主人在挠我痒痒，好痒啊",
        "start_time": 1755222858360,
        "end_time": 1755222858360
      },
      {
        "event_type": "Touch_Left_LongPress",
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
  event_type: string;        // 事件类型标识（包含位置信息）
  event_text: string;        // 事件描述文本（供LLM理解）
  start_time: number;        // 事件开始时间戳（ms since epoch）
  end_time: number;          // 事件结束时间戳（ms since epoch）
  event_payload?: {          // 可选的额外数据（通常为空）
    [key: string]: any;
  };
}
```

### 设备端事件类型映射

#### 触摸事件映射
```cpp
// TouchEventType + Position → event_type字符串 → event_text

// ✅ 需要上传的触摸事件（Touch_[Position]_[Action]格式）
// 单侧触摸事件
TouchEventType::SINGLE_TAP + LEFT   → "Touch_Left_Tap"        → "主人轻轻拍了我的左侧"
TouchEventType::SINGLE_TAP + RIGHT  → "Touch_Right_Tap"       → "主人轻轻拍了我的右侧"
TouchEventType::LONG_PRESS + LEFT         → "Touch_Left_LongPress"  → "主人长时间按住了我的左侧"
TouchEventType::LONG_PRESS + RIGHT        → "Touch_Right_LongPress" → "主人长时间按住了我的右侧"

// 双侧触摸事件（特殊模式）
TouchEventType::SINGLE_TAP + BOTH   → "Touch_Both_Tap"        → "主人同时拍了我的两侧"
TouchEventType::CRADLED             → "Touch_Both_Cradled"    → "主人温柔地抱着我"
TouchEventType::TICKLED             → "Touch_Both_Tickled"    → "主人在挠我痒痒"

// ❌ 不上传的事件
TouchEventType::RELEASE      // 释放事件（无需上传）

// 注意事项：
// 1. event_payload通常为空，位置信息已包含在event_type中
// 2. CRADLED是特殊的双侧长按模式，需要满足：
//    - 双侧同时触摸超过2秒
//    - IMU保持稳定（设备静止）
//    - 这与普通的双侧长按不同，CRADLED更像是"温柔地抱着"的语义
// 3. TICKLED需要在2秒内检测到4次以上的无规律触摸
```

#### 运动事件映射
```cpp
// MotionEventType → event_type字符串 → event_text（统一使用Motion_前缀）
EventType::MOTION_SHAKE           → "Motion_Shake"           → "主人轻轻摇了摇我"
EventType::MOTION_SHAKE_VIOLENTLY → "Motion_ShakeViolently"  → "主人用力摇晃我" 
EventType::MOTION_FLIP            → "Motion_Flip"            → "主人把我翻了个身"
EventType::MOTION_FREE_FALL       → "Motion_FreeFall"        → "糟糕，我掉下去了"
EventType::MOTION_PICKUP          → "Motion_PickUp"          → "主人把我拿起来了"
EventType::MOTION_UPSIDE_DOWN     → "Motion_UpsideDown"      → "主人把我倒立起来了"

// 运动事件的event_payload为空或包含少量必要信息
// 时间处理：
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

注意：在实际集成时，需确保时间同步机制正常工作。未同步前先缓存事件但不发送；一旦 `IsTimesynced()==true` 或收到服务端时间校正，再计算正确的 `start_time` 和 `end_time` 发送缓存事件。

### 1. 创建事件上传器

**文件位置**: `main/boards/ALichuangTest/interaction/event_uploader.h`

```cpp
#ifndef EVENT_UPLOADER_H
#define EVENT_UPLOADER_H

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

class EventUploader {
public:
    EventUploader();
    ~EventUploader();
    
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
        std::string event_type;      // 事件类型（如 "Touch_Left_Tap"）
        std::string event_text;      // 事件描述文本（中文）
        int64_t start_time;          // 事件开始时间（Unix时间戳，毫秒，时间同步后才有效）
        int64_t end_time;            // 事件结束时间（Unix时间戳，毫秒，时间同步后才有效）
        int64_t mono_ms;             // 单调时钟时间（毫秒，用于时间同步前的记录）
        uint32_t duration_ms;        // 事件持续时间（毫秒）
        cjson_uptr event_payload;    // 额外数据（通常为空），智能指针管理
        
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
    std::string GetEventTypeString(const Event& event);  // 需要完整event对象来获取位置信息
    std::string GenerateEventText(const Event& event);   // 生成event_text字段
    cjson_uptr GenerateEventPayload(const Event& event); // 返回智能指针（通常为空）
    uint32_t CalculateDuration(const Event& event);      // 计算事件持续时间
    
    // 泛型发送事件（模板定义在头文件，避免链接问题）
    template<class It>
    void SendEvents(It first, It last) {
        if (first == last) return;
        
        std::string payload = BuildEventPayload(first, last);
        Application::GetInstance().SendEventMessage(payload);
    }
    
    // 泛型构建事件payload（模板定义在头文件）
    template<class It>
    std::string BuildEventPayload(It first, It last) {
        // 只构建payload部分，session_id和type由Application添加
        cJSON* payload = cJSON_CreateObject();
        cJSON* events_array = cJSON_CreateArray();
        
        for (auto it = first; it != last; ++it) {
            const auto& event = *it;
            cJSON* event_obj = cJSON_CreateObject();
            
            // 添加事件字段（只输出start_time/end_time，不输出内部的mono_ms）
            cJSON_AddStringToObject(event_obj, "event_type", event.event_type.c_str());
            cJSON_AddStringToObject(event_obj, "event_text", event.event_text.c_str());
            cJSON_AddNumberToObject(event_obj, "start_time", event.start_time);
            cJSON_AddNumberToObject(event_obj, "end_time", event.end_time);
            
            // 只有在event_payload存在时才添加
            if (event.event_payload) {
                cJSON_AddItemToObject(event_obj, "event_payload", 
                                    cJSON_Duplicate(event.event_payload.get(), true));
            }
            
            cJSON_AddItemToArray(events_array, event_obj);
        }
        
        cJSON_AddItemToObject(payload, "events", events_array);
        
        char* json_str = cJSON_PrintUnformatted(payload);
        std::string result(json_str);
        
        cJSON_free(json_str);
        cJSON_Delete(payload);
        
        return result;
    }
    
    // 检查连接状态
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
    
#ifdef UNIT_TEST
    // 测试友元声明，允许单元测试访问私有成员
    friend class TestEventUploader;
    friend std::string __test_build_payload(EventUploader& uploader,
        std::vector<CachedEvent>::iterator first,
        std::vector<CachedEvent>::iterator last) {
        return uploader.BuildEventPayload(first, last);
    }
#endif
};

#endif // EVENT_UPLOADER_H
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

3. **职责分离**：
   - **EventUploader**：只负责构建事件payload（`{"events": [...]}`）
   - **Application**：负责添加session_id和type，构建完整消息
   - **优势**：EventUploader不需要了解session管理，Application统一控制消息格式

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
        // 连接已建立且时间已同步，立即发送
        CachedEvent events[] = {std::move(cached)};
        SendEvents(events, events + 1);  // 发送payload，Application会添加session_id和type
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
    
    // 清理过期事件（TTL检查）
    if (!events_to_send.empty() && EventNotificationConfig::CACHE_TIMEOUT_MS > 0) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        int64_t epoch_now_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
        int64_t mono_now_ms = esp_timer_get_time() / 1000;
        
        auto old_size = events_to_send.size();
        events_to_send.erase(
            std::remove_if(events_to_send.begin(), events_to_send.end(),
                [&](const CachedEvent& e) {
                    // 计算事件的实际时间
                    int64_t event_time = e.start_time > 0 ? e.start_time 
                                       : (epoch_now_ms - (mono_now_ms - e.mono_ms));
                    return (epoch_now_ms - event_time) > EventNotificationConfig::CACHE_TIMEOUT_MS;
                }),
            events_to_send.end());
        
        if (old_size != events_to_send.size()) {
            ESP_LOGW(TAG, "Dropped %zu expired events (TTL=%dms)", 
                     old_size - events_to_send.size(), 
                     EventNotificationConfig::CACHE_TIMEOUT_MS);
        }
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
    
    // 回填正确的Unix时间戳
    if (!events_to_send.empty()) {
        // 获取当前的Unix时间和单调时钟
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        int64_t epoch_now_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
        int64_t mono_now_ms = esp_timer_get_time() / 1000;
        
        for (auto& event : events_to_send) {
            // 使用单调时钟差值计算事件的真实Unix时间
            // 事件发生时的Unix时间 = 当前Unix时间 - (当前单调时间 - 事件单调时间)
            int64_t time_diff_ms = mono_now_ms - event.mono_ms;
            event.start_time = epoch_now_ms - time_diff_ms;
            event.end_time = event.start_time + event.duration_ms;
        }
        
        // 清理过期事件（TTL检查）
        if (EventNotificationConfig::CACHE_TIMEOUT_MS > 0) {
            auto old_size = events_to_send.size();
            events_to_send.erase(
                std::remove_if(events_to_send.begin(), events_to_send.end(),
                    [&](const CachedEvent& e) {
                        return (epoch_now_ms - e.start_time) > EventNotificationConfig::CACHE_TIMEOUT_MS;
                    }),
                events_to_send.end());
            
            if (old_size != events_to_send.size()) {
                ESP_LOGW(TAG, "Dropped %zu expired events after time sync", 
                         old_size - events_to_send.size());
            }
        }
        
        // 分批发送
        const int BATCH_SIZE = EventNotificationConfig::BATCH_SIZE;
        for (size_t i = 0; i < events_to_send.size(); i += BATCH_SIZE) {
            size_t end = std::min(i + BATCH_SIZE, events_to_send.size());
            SendEvents(events_to_send.begin() + i, events_to_send.begin() + end);
        }
    }
}

// SendEvents和BuildEventPayload模板函数已移至头文件，避免链接问题

CachedEvent EventUploader::ConvertEvent(const Event& event) {
    CachedEvent cached;
    cached.event_type = GetEventTypeString(event);  // 传入完整的event对象
    cached.event_text = GenerateEventText(event);
    
    // 记录单调时钟时间（始终可用）
    cached.mono_ms = esp_timer_get_time() / 1000;  // 微秒转毫秒
    
    // 计算持续时间
    cached.duration_ms = CalculateDuration(event);
    
    // 如果时间已同步，计算Unix时间戳
    if (IsTimesynced()) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        cached.start_time = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
        cached.end_time = cached.start_time + cached.duration_ms;
    } else {
        // 时间未同步，先设为0，等同步后再回填
        cached.start_time = 0;
        cached.end_time = 0;
    }
    
    cached.event_payload = GenerateEventPayload(event);
    return cached; // 移动语义自动生效，转移unique_ptr所有权
}

// 计算事件持续时间
uint32_t EventUploader::CalculateDuration(const Event& event) {
    // 触摸事件从touch_data.y中获取持续时间
    if (event.type == EventType::TOUCH_LONG_PRESS || 
        event.type == EventType::TOUCH_TAP ||
        event.type == EventType::TOUCH_CRADLED) {
        // touch_data.y存储了TouchEvent的duration_ms
        uint32_t duration_ms = static_cast<uint32_t>(event.data.touch_data.y);
        if (duration_ms > 0) {
            return duration_ms;
        }
    }
    
    // TICKLED事件有2秒的语义窗口
    if (event.type == EventType::TOUCH_TICKLED) {
        return 2000;  // 2秒内多次触摸的检测窗口
    }
    
    // 其他瞬时事件，持续时间为0
    return 0;
}

std::string EventUploader::GetEventTypeString(const Event& event) {
    // 对于单侧触摸事件，需要结合位置信息生成完整的事件类型
    if (event.type == EventType::TOUCH_TAP ||
        event.type == EventType::TOUCH_LONG_PRESS) {
        
        // 从event.data.touch_data.x获取位置信息
        // x = 0: left, x = 1: right, x = 2: both
        std::string position = "Both";
        if (event.data.touch_data.x == 0) {
            position = "Left";
        } else if (event.data.touch_data.x == 1) {
            position = "Right";
        }
        
        // 构建完整的事件类型名称
        switch (event.type) {
            case EventType::TOUCH_TAP:
                return "Touch_" + position + "_Tap";
            case EventType::TOUCH_LONG_PRESS:
                return "Touch_" + position + "_LongPress";
            default:
                break;
        }
    }
    
    // 特殊双侧触摸事件（这些事件本身就包含了双侧的含义）
    switch (event.type) {
        case EventType::TOUCH_TICKLED: return "Touch_Both_Tickled";
        case EventType::TOUCH_CRADLED: return "Touch_Both_Cradled";
        
        // 运动事件（统一使用Motion_前缀）
        case EventType::MOTION_SHAKE: return "Motion_Shake";
        case EventType::MOTION_SHAKE_VIOLENTLY: return "Motion_ShakeViolently";
        case EventType::MOTION_FLIP: return "Motion_Flip";
        case EventType::MOTION_FREE_FALL: return "Motion_FreeFall";
        case EventType::MOTION_PICKUP: return "Motion_PickUp";
        case EventType::MOTION_UPSIDE_DOWN: return "Motion_UpsideDown";
        case EventType::MOTION_TILT: return "Motion_Tilt";
        
        default: return "Unknown";
    }
}


std::string EventUploader::GenerateEventText(const Event& event) {
    // 生成供LLM理解的中文event_text
    
    // 对于需要区分位置的触摸事件
    if (event.type == EventType::TOUCH_TAP) {
        // 从event.data.touch_data.x获取位置信息
        if (event.data.touch_data.x == 0) {
            return "主人轻轻拍了我的左侧";
        } else if (event.data.touch_data.x == 1) {
            return "主人轻轻拍了我的右侧";
        } else {
            return "主人同时拍了我的两侧";
        }
    }
    
    if (event.type == EventType::TOUCH_LONG_PRESS) {
        if (event.data.touch_data.x == 0) {
            return "主人长时间按住了我的左侧";
        } else if (event.data.touch_data.x == 1) {
            return "主人长时间按住了我的右侧";
        } else {
            return "主人同时长按了我的两侧";
        }
    }
    
    // 特殊双侧触摸事件
    switch (event.type) {
        case EventType::TOUCH_TICKLED:
            return "主人在挠我痒痒";
        case EventType::TOUCH_CRADLED:
            return "主人温柔地抱着我";
            
        // 运动事件
        case EventType::MOTION_SHAKE:
            return "主人轻轻摇了摇我";
        case EventType::MOTION_SHAKE_VIOLENTLY:
            return "主人用力摇晃我";
        case EventType::MOTION_FLIP:
            return "主人把我翻了个身";
        case EventType::MOTION_FREE_FALL:
            return "糟糕，我掉下去了";
        case EventType::MOTION_PICKUP:
            return "主人把我拿起来了";
        case EventType::MOTION_UPSIDE_DOWN:
            return "主人把我倒立起来了";
            
        default:
            return "主人和我互动了";
    }
}

cjson_uptr EventUploader::GenerateEventPayload(const Event& event) {
    // 由于位置信息已经包含在event_type中，大多数事件不需要event_payload
    // 只在需要额外信息时才创建payload
    
    // 目前所有事件都不需要额外的payload
    // 未来如果需要添加额外信息（如传感器数据等），可以在这里扩展
    
    return cjson_uptr{nullptr};  // 返回空payload
}

bool EventUploader::IsConnected() const {
    auto& app = Application::GetInstance();
    // 检查WebSocket/MQTT控制通道连接状态
    // IsConnected()语义：文本/控制通道是否已建立（不是音频通道）
    // 事件上传使用控制通道，与音频流无关
    return app.GetProtocol() && app.GetProtocol()->IsConnected();
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
void SendEventMessage(const std::string& payload_str);

// 在application.cc中实现（构建完整消息）
void Application::SendEventMessage(const std::string& payload_str) {
    Schedule([this, payload_str]() {
        if (!protocol_) return;
        
        // 构建完整的消息
        cJSON* message = cJSON_CreateObject();
        
        // 添加session_id
        if (!session_id_.empty()) {
            cJSON_AddStringToObject(message, "session_id", session_id_.c_str());
        }
        
        // 添加消息类型
        cJSON_AddStringToObject(message, "type", "lx/v1/event");
        
        // 解析并添加payload
        cJSON* payload = cJSON_Parse(payload_str.c_str());
        if (payload) {
            cJSON_AddItemToObject(message, "payload", payload);
        }
        
        // 发送完整消息
        char* json_str = cJSON_PrintUnformatted(message);
        std::string full_message(json_str);
        
        protocol_->SendText(full_message);
        
        cJSON_free(json_str);
        cJSON_Delete(message);
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

// 事件上传配置
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
// 测试事件payload格式
void TestEventPayloadFormat() {
    EventUploader uploader;
    
    // 1. 创建完整的CachedEvent测试数据
    std::vector<EventUploader::CachedEvent> events;
    
    // 测试触摸事件
    {
        EventUploader::CachedEvent event;
        event.event_type = "Touch_Left_Tap";
        event.event_text = "主人轻轻拍了我的左侧";
        event.start_time = 1755222858360;
        event.end_time = 1755222858360;
        event.mono_ms = esp_timer_get_time() / 1000;
        event.duration_ms = 0;  // 瞬时事件
        event.event_payload = nullptr;  // 触摸事件通常不需要额外payload
        events.push_back(std::move(event));
    }
    
    // 测试长按事件（带持续时间）
    {
        EventUploader::CachedEvent event;
        event.event_type = "Touch_Right_LongPress";
        event.event_text = "主人长时间按住了我的右侧";
        event.start_time = 1755222860000;
        event.end_time = 1755222862500;
        event.mono_ms = esp_timer_get_time() / 1000;
        event.duration_ms = 2500;  // 2.5秒
        event.event_payload = nullptr;
        events.push_back(std::move(event));
    }
    
    // 2. 调用BuildEventPayload（通过友元函数访问）
#ifdef UNIT_TEST
    std::string payload_str = __test_build_payload(uploader, events.begin(), events.end());
#else
    // 生产环境下，BuildEventPayload是私有的，需要通过公开接口测试
    // 这里可以通过模拟HandleEvent和SendEvents的完整流程来测试
    std::string payload_str = "{}";  // 占位符
#endif
    cJSON* payload = cJSON_Parse(payload_str.c_str());
    
    // 3. 验证payload结构
    assert(payload != nullptr);
    cJSON* events_array = cJSON_GetObjectItem(payload, "events");
    assert(events_array != nullptr);
    assert(cJSON_IsArray(events_array));
    assert(cJSON_GetArraySize(events_array) == 2);
    
    // 4. 验证第一个事件
    cJSON* event1 = cJSON_GetArrayItem(events_array, 0);
    assert(cJSON_GetObjectItem(event1, "event_type"));
    assert(cJSON_GetObjectItem(event1, "event_text"));
    assert(cJSON_GetObjectItem(event1, "start_time"));
    assert(cJSON_GetObjectItem(event1, "end_time"));
    assert(strcmp(cJSON_GetObjectItem(event1, "event_type")->valuestring, "Touch_Left_Tap") == 0);
    assert(strcmp(cJSON_GetObjectItem(event1, "event_text")->valuestring, "主人轻轻拍了我的左侧") == 0);
    assert(cJSON_GetObjectItem(event1, "start_time")->valuedouble == 1755222858360);
    assert(cJSON_GetObjectItem(event1, "end_time")->valuedouble == 1755222858360);
    
    // 5. 验证第二个事件（带持续时间）
    cJSON* event2 = cJSON_GetArrayItem(events_array, 1);
    assert(strcmp(cJSON_GetObjectItem(event2, "event_type")->valuestring, "Touch_Right_LongPress") == 0);
    assert(cJSON_GetObjectItem(event2, "start_time")->valuedouble == 1755222860000);
    assert(cJSON_GetObjectItem(event2, "end_time")->valuedouble == 1755222862500);
    
    cJSON_Delete(payload);
}

// 测试时间同步逻辑
void TestTimeSynchronization() {
    EventUploader uploader;
    
    // 模拟未同步时创建的事件
    Event test_event{EventType::TOUCH_TAP};
    test_event.data.touch_data.x = 0;  // LEFT
    test_event.data.touch_data.y = 0;  // duration = 0
    
    // 转换事件（此时时间未同步）
    auto cached = uploader.ConvertEvent(test_event);
    
    // 验证未同步时的字段
    assert(cached.mono_ms > 0);  // 单调时钟应该有值
    assert(cached.start_time == 0);  // Unix时间戳应该为0
    assert(cached.end_time == 0);
    assert(cached.duration_ms == 0);
    
    // 模拟时间同步后的处理
    std::vector<EventUploader::CachedEvent> events;
    events.push_back(std::move(cached));
    
    // 模拟OnTimeSynced的逻辑
    int64_t epoch_now_ms = 1755222858360;
    int64_t mono_now_ms = esp_timer_get_time() / 1000;
    
    for (auto& event : events) {
        int64_t time_diff_ms = mono_now_ms - event.mono_ms;
        event.start_time = epoch_now_ms - time_diff_ms;
        event.end_time = event.start_time + event.duration_ms;
    }
    
    // 验证时间同步后的时间戳
    assert(events[0].start_time > 0);
    assert(events[0].end_time >= events[0].start_time);
}

// 测试内存安全（防止double free）
void TestMemorySafety() {
    EventUploader uploader;
    std::vector<Event> events;
    
    // 添加大量事件，触发vector扩容
    for (int i = 0; i < 100; ++i) {
        Event event{EventType::TOUCH_TICKLED, /* ... */};
        events.push_back(event);
        uploader.HandleEvent(event); // 这会导致CachedEvent的移动和扩容
    }
    
    // 如果没有崩溃，说明unique_ptr正确管理了内存
    // 析构时会自动清理，不会double free
}
```

### 2. 集成测试
```cpp
// 测试连接状态变化时的事件处理
void TestConnectionStateHandling() {
    EventUploader uploader;
    
    // 模拟断线时的事件缓存
    uploader.OnConnectionClosed();
    
    Event event1{EventType::TOUCH_TAP};
    event1.data.touch_data.x = 0;  // LEFT
    uploader.HandleEvent(event1);
    
    Event event2{EventType::MOTION_SHAKE};
    uploader.HandleEvent(event2);
    
    // 验证事件被缓存（需要访问内部状态）
    // assert(uploader.event_cache_.size() == 2);
    
    // 模拟连接恢复
    uploader.OnConnectionOpened();
    // 验证批量发送逻辑
}

// 测试批量发送
void TestBatchSending() {
    EventUploader uploader;
    std::vector<EventUploader::CachedEvent> large_batch;
    
    // 创建超过BATCH_SIZE的事件
    for (int i = 0; i < EventNotificationConfig::BATCH_SIZE * 2 + 1; ++i) {
        EventUploader::CachedEvent event;
        event.event_type = "Motion_Shake";
        event.event_text = "主人轻轻摇了摇我";
        event.start_time = 1755222858360 + i * 1000;
        event.end_time = event.start_time;
        event.mono_ms = esp_timer_get_time() / 1000 + i * 1000;
        event.duration_ms = 0;
        large_batch.push_back(std::move(event));
    }
    
    // 验证会分成3批发送
    // 第1批: BATCH_SIZE个
    // 第2批: BATCH_SIZE个
    // 第3批: 1个
}
```

### 3. 端到端测试
```cpp
// 完整的事件流测试
void TestEndToEndEventFlow() {
    // 1. 初始化整个系统
    ALichuangTest board;
    board.Initialize();
    
    // 2. 模拟触摸事件
    TouchEvent touch_event;
    touch_event.type = TouchEventType::SINGLE_TAP;
    touch_event.position = TouchPosition::LEFT;
    touch_event.timestamp_us = esp_timer_get_time();
    touch_event.duration_ms = 100;
    
    // 3. 触发事件处理链
    // TouchEngine → EventEngine → EventUploader → Application → Protocol
    
    // 4. 验证服务器收到的消息格式
    // 使用mock服务器或者抓包工具验证：
    // - 消息type是否为"lx/v1/event"
    // - payload.events数组是否包含正确的事件
    // - event_type是否为"Touch_Left_Tap"
    // - 时间戳是否合理
}

// 性能测试
void TestHighFrequencyEvents() {
    EventUploader uploader;
    auto start_time = esp_timer_get_time();
    
    // 快速生成100个事件
    for (int i = 0; i < 100; ++i) {
        Event event{EventType::TOUCH_TAP};
        event.data.touch_data.x = i % 2;  // 交替左右
        uploader.HandleEvent(event);
    }
    
    auto end_time = esp_timer_get_time();
    auto duration_us = end_time - start_time;
    
    // 验证处理时间在可接受范围内
    assert(duration_us < 100000);  // 100ms内处理完100个事件
    
    ESP_LOGI("TEST", "Processed 100 events in %lld us", duration_us);
}
```

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
    
    # 推送到LLM上下文（使用正确的事件类型名称）
    important_events = [
        'Motion_Shake', 'Motion_ShakeViolently', 'Motion_FreeFall',
        'Touch_Left_LongPress', 'Touch_Right_LongPress',
        'Touch_Both_Cradled', 'Touch_Both_Tickled'
    ]
    if event_type in important_events:
        llm_context.add_interaction(event)
    
    # 触发相应的业务逻辑
    if event_type == 'Motion_FreeFall':
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

通过统一的事件上传机制，使用event_type区分，保持命名规范一致：

```json
{
  "event_type": "Battery_Low",         // 电量事件
  "event_type": "Network_Changed",     // 网络状态
  "event_type": "Pattern_Detected",    // 行为模式
  "event_type": "Gesture_SwipeUp",     // 手势识别
  "event_type": "Proximity_Near",      // 接近感应
  "event_type": "Light_Changed"        // 环境光线
}
```

所有新增事件类型都通过同一个事件上传通道发送，服务端根据event_type字段路由处理。命名规范：`Category_Action` 格式。

### 自定义事件支持
```cpp
class CustomEventUploader : public EventUploader {
    // 继承并扩展，支持应用特定的事件类型
};
```

## 迁移路径

### 从旧协议迁移到新的事件上传协议

1. **阶段1**：实现新的事件上传器
2. **阶段2**：服务器同时支持两种格式
3. **阶段3**：新设备使用lx/v1/event，旧设备继续使用原协议
4. **阶段4**：逐步升级旧设备固件
5. **阶段5**：完全迁移到新协议

## 总结

使用独立的`lx/v1/event`协议实现事件上传具有以下优势：

✅ **协议独立**：不依赖MCP协议，使用独立的`type: "lx/v1/event"`  
✅ **职责清晰**：EventUploader构建payload，Application添加外层字段  
✅ **极简设计**：直接的事件推送，无需响应机制  
✅ **时间语义清晰**：`start_time`和`end_time`明确表示事件时间范围  
✅ **事件类型自描述**：位置信息包含在event_type中（如`Touch_Left_Tap`）  
✅ **易于扩展**：新增事件类型只需定义新的event_type值  

关键设计原则：
- **字段简化**：使用`start_time`+`end_time`，语义清晰
- **协议独立**：独立的`lx/v1/event`消息类型
- **职责分离**：EventUploader负责payload，Application负责完整消息
- **单向推送**：无需服务器响应，符合设备事件推送场景

数据流程：
```
Event对象 → EventUploader::ConvertEvent() → CachedEvent
         → EventUploader::BuildEventPayload() → payload JSON
         → Application::SendEventMessage() → 添加session_id和type
         → Protocol::SendText() → 服务器
```

通过本方案，在保持架构清晰的前提下，实现了简单、高效的设备事件推送系统。

---

## 后端开发沟通指南

### 🂯 核心信息

新增事件上传协议：`lx/v1/event`，用于接收设备交互事件。

### 📝 消息格式
```json
{
  "session_id": "9aa008fa-c874-4829-b70b-fca7fa30e3da",
  "type": "lx/v1/event",
  "payload": {
    "events": [
      {
        "event_type": "Touch_Both_Tickled",
        "start_time": 1755222858360,
        "end_time": 1755222858360,
        "event_text": "主人在挠我痒痒，好痒啊"
      },
      {
        "event_type": "Touch_Left_LongPress",
        "start_time": 1755222860000,
        "end_time": 1755222862500,
        "event_text": "主人长时间按住了我的左侧"
      }
    ]
  }
}
```

### 🔧 实现方式

**步骤1**: 处理事件消息
```python
def handle_message(message):
    if message.get('type') == 'lx/v1/event':
        payload = message.get('payload', {})
        events = payload.get('events', [])
        for event in events:
            process_event(event)
        return True
    return False
```

**步骤2**: 提取事件字段

```python
# 单个事件处理示例
def process_event(event):
    event_type = event.get('event_type')        # 事件类型："Touch_Left_Tap", "Motion_Shake" 等
    event_text = event.get('event_text')        # 事件描述："主人轻轻拍了我的左侧"
    start_time = event.get('start_time')        # 开始时间戳
    end_time = event.get('end_time')            # 结束时间戳
    event_payload = event.get('event_payload')  # 额外数据（通常为None）
    
    # 从event_type中解析位置信息
    if event_type.startswith('Touch_'):
        parts = event_type.split('_')
        if len(parts) >= 3:
            position = parts[1].lower()  # "left", "right", "both"
            action = '_'.join(parts[2:])  # "Tap", "LongPress", etc.
    
    # 你的业务逻辑...
    print(f"{event_type}: {event_text}")
```

### 📝 事件类型列表

**✅ 触摸事件**（Touch_[Position]_[Action]格式）：

单侧触摸：
- `Touch_Left_Tap` - 主人轻轻拍了我的左侧（<500ms）
- `Touch_Right_Tap` - 主人轻轻拍了我的右侧（<500ms）
- `Touch_Left_LongPress` - 主人长时间按住了我的左侧（>500ms）
- `Touch_Right_LongPress` - 主人长时间按住了我的右侧（>500ms）

双侧触摸（特殊模式）：
- `Touch_Both_Tap` - 主人同时拍了我的两侧
- `Touch_Both_Cradled` - 主人温柔地抱着我（双侧持续触摸>2秒且IMU静止）
- `Touch_Both_Tickled` - 主人在挠我痒痒（2秒内多次无规律触摸>4次）

**✅ 运动事件**（Motion_前缀）：
- `Motion_Shake` - 主人轻轻摇了摇我
- `Motion_ShakeViolently` - 主人用力摇晃我
- `Motion_Flip` - 主人把我翻了个身
- `Motion_FreeFall` - 糟糕，我掉下去了
- `Motion_PickUp` - 主人把我拿起来了
- `Motion_UpsideDown` - 主人把我倒立起来了

**服务端处理提示**：
所有事件类型都采用 `Category_[Position_]Action` 的命名格式，便于解析和分类处理。
例如：可以通过 `event_type.startswith('Touch_')` 判断是否为触摸事件，
通过 `'Left' in event_type` 判断是否为左侧触摸。

**注意**：
- event_payload字段通常为空，所有必要信息都已包含在event_type中
- 位置信息直接体现在事件类型名称中，无需额外解析

### ⚠️ 注意

- **非MCP协议**: 使用独立的 `lx/v1/event` 消息类型，不是MCP/JSON-RPC格式
- **无需响应**: 这是单向事件推送，服务器不需要返回响应
- **批量事件**: `payload.events` 是数组，可能包含多个事件
- **位置信息**: 触摸位置已包含在 event_type 中（如 Touch_Left_Tap, Touch_Right_LongPress）