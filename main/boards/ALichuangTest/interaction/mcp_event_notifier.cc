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

McpEventNotifier::CachedEvent McpEventNotifier::ConvertEvent(const Event& event) {
    CachedEvent cached;
    cached.event_type = GetEventTypeString(event.type);
    
    // 获取当前时间戳（Unix epoch毫秒，整型）
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    cached.timestamp_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    
    // 获取事件持续时间
    cached.duration_ms = GetEventDuration(event);
    
    cached.event_text = GenerateEventText(event);
    cached.metadata = GenerateEventMetadata(event);
    return cached; // 移动语义自动生效，转移unique_ptr所有权
}

// 获取事件持续时间（毫秒）
uint32_t McpEventNotifier::GetEventDuration(const Event& event) {
    // 触摸事件从touch_data.y中获取持续时间
    if (event.type == EventType::TOUCH_LONG_PRESS || 
        event.type == EventType::TOUCH_TAP) {
        // touch_data.y存储了TouchEvent的duration_ms
        return static_cast<uint32_t>(event.data.touch_data.y);
    }
    
    // 其他事件默认为瞬时事件（0ms）
    return 0;
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