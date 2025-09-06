#include "event_uploader.h"
#include "application.h"     // for Application::GetInstance()
#include <esp_log.h>
#include <esp_timer.h>
// Removed sys/time.h - using esp_timer_get_time() for unified timeline
#include <inttypes.h>        // for PRIu32, PRId64
#include <algorithm>         // for std::min, std::remove_if

EventUploader::EventUploader() 
    : enabled_(false),
      current_has_emotion_state_(false),
      current_valence_(0.0f),
      current_arousal_(0.0f) {
    
    // 生成设备ID
    device_id_ = GenerateDeviceId();
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "EventUploader created");
    ESP_LOGI(TAG_EVENT_UPLOADER, "Device ID: %s", device_id_.c_str());
}

EventUploader::~EventUploader() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    event_cache_.clear();
    ESP_LOGI(TAG_EVENT_UPLOADER, "EventUploader destroyed");
}

std::string EventUploader::GenerateDeviceId() {
    // 简化版：使用固定的设备ID，或者可以从其他地方获取
    // 在实际项目中，这个ID可以从配置文件、NVRAM等获取
    return "alichuang_test_device";
}

void EventUploader::SetCurrentEmotionState(float valence, float arousal) {
    std::lock_guard<std::mutex> lock(emotion_mutex_);
    current_has_emotion_state_ = true;
    current_valence_ = valence;
    current_arousal_ = arousal;
    
    ESP_LOGD(TAG_EVENT_UPLOADER, "Updated emotion state: V=%.2f, A=%.2f", valence, arousal);
}

void EventUploader::HandleEvent(const Event& event) {
    if (!enabled_) {
        ESP_LOGD(TAG_EVENT_UPLOADER, "EventUploader disabled, ignoring event");
        return;
    }
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "=== Event Processing Debug ===");
    ESP_LOGI(TAG_EVENT_UPLOADER, "Raw event type: %d", (int)event.type);
    if (event.type == EventType::TOUCH_TAP || event.type == EventType::TOUCH_LONG_PRESS ||
        event.type == EventType::TOUCH_CRADLED || event.type == EventType::TOUCH_TICKLED) {
        const char* pos_str = "UNKNOWN";
        switch (event.data.touch_data.position) {
            case TouchPosition::LEFT: pos_str = "LEFT"; break;
            case TouchPosition::RIGHT: pos_str = "RIGHT"; break;
            case TouchPosition::BOTH: pos_str = "BOTH"; break;
            case TouchPosition::ANY: pos_str = "ANY"; break;
        }
        ESP_LOGI(TAG_EVENT_UPLOADER, "Touch data: position=%s, duration=%lums, tap_count=%lu", 
                 pos_str, (unsigned long)event.data.touch_data.duration_ms, 
                 (unsigned long)event.data.touch_data.tap_count);
    }
    
    // 转换事件
    auto cached = ConvertEvent(event);
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "Event converted: %s -> %s", 
             cached.event_type.c_str(), cached.event_text.c_str());
    ESP_LOGI(TAG_EVENT_UPLOADER, "Duration: %lums, Start: %lld, End: %lld", 
             (long unsigned)cached.duration_ms, (long long)cached.start_time, (long long)cached.end_time);
    
    // 尝试发送或缓存事件
    TrySendOrCache(std::move(cached));
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "✓ Event processing completed");
    
    // 定期清理过期事件（更频繁，因为缓存时间缩短到5秒）
    static uint32_t cleanup_counter = 0;
    if (++cleanup_counter % 10 == 0) {  // 每10个事件清理一次
        ClearExpiredEvents();
    }
}

void EventUploader::HandleBatchEvents(const std::vector<Event>& events) {
    if (!enabled_) {
        ESP_LOGD(TAG_EVENT_UPLOADER, "EventUploader disabled, ignoring batch events");
        return;
    }
    
    if (events.empty()) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Empty events batch, ignoring");
        return;
    }
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "=== Batch Event Processing Debug ===");
    ESP_LOGI(TAG_EVENT_UPLOADER, "Processing %zu events in batch", events.size());
    
    // 转换所有事件
    std::vector<CachedEvent> cached_events;
    cached_events.reserve(events.size());
    
    for (const auto& event : events) {
        auto cached = ConvertEvent(event);
        ESP_LOGD(TAG_EVENT_UPLOADER, "Event: %s -> %s", 
                 cached.event_type.c_str(), cached.event_text.c_str());
        cached_events.push_back(std::move(cached));
    }
    
    // 批量发送或缓存
    TrySendOrCacheBatch(std::move(cached_events));
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "✓ Batch event processing completed");
}

void EventUploader::TrySendOrCache(CachedEvent&& event) {
    // 这个方法决定是立即发送还是缓存事件
    // 策略：
    // 1. 总是先尝试立即发送（SendEventMessage会处理网络和WebSocket连接）
    // 2. 如果我们检测到没有网络，可以选择缓存（但实际上SendEventMessage会失败）
    
    // 暂时缓存事件，以防SendEventMessage失败时可以重试
    // 但目前的设计是：如果没有连接，SendEventMessage会尝试建立连接
    // 所以我们直接发送，让Application层处理连接逻辑
    
    SendSingleEvent(std::move(event));
    
    // 注意：如果需要实现失败后缓存的逻辑，可以在这里添加
    // 但根据需求，我们不希望缓存超过5秒的事件
}

void EventUploader::TrySendOrCacheBatch(std::vector<CachedEvent>&& events) {
    // 批量发送策略：直接发送，让Application层处理连接逻辑
    SendBatchEvents(std::move(events));
}

void EventUploader::OnConnectionOpened() {
    ESP_LOGI(TAG_EVENT_UPLOADER, "Connection opened - processing recent cached events (5s window)");
    // 先清理超过5秒的过期事件
    ClearExpiredEvents();
    // 然后发送剩余的缓存事件
    ProcessCachedEvents();
}

void EventUploader::OnConnectionClosed() {
    ESP_LOGW(TAG_EVENT_UPLOADER, "Connection closed - events will be cached");
}


void EventUploader::AddToCache(CachedEvent&& event) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // 检查缓存大小限制
    if (event_cache_.size() >= EventNotificationConfig::MAX_CACHE_SIZE) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Cache full (%d events), removing oldest", 
                EventNotificationConfig::MAX_CACHE_SIZE);
        event_cache_.erase(event_cache_.begin());
    }
    
    event_cache_.push_back(std::move(event));
    ESP_LOGI(TAG_EVENT_UPLOADER, "Event cached, total cached: %d", event_cache_.size());
}

void EventUploader::ProcessCachedEvents() {
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    if (event_cache_.empty()) {
        ESP_LOGD(TAG_EVENT_UPLOADER, "No cached events to process");
        return;
    }
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "Processing %d cached events", event_cache_.size());
    
    try {
        // 性能监控
        int64_t start_time = esp_timer_get_time();
        size_t successful_batches = 0;
        size_t total_events = event_cache_.size();
        
        // 批量发送缓存的事件
        size_t batch_start = 0;
        while (batch_start < event_cache_.size()) {
            size_t batch_end = std::min(batch_start + EventNotificationConfig::BATCH_SIZE, 
                                        event_cache_.size());
            
            // 构建批量事件的JSON
            std::string payload = BuildEventPayload(
                event_cache_.begin() + batch_start,
                event_cache_.begin() + batch_end
            );
            
            // 验证JSON
            cJSON* json = cJSON_Parse(payload.c_str());
            if (json) {
                ESP_LOGI(TAG_EVENT_UPLOADER, "Sending batch of %d events", batch_end - batch_start);
                cJSON_Delete(json);
                
                Application::GetInstance().SendEventMessage(payload);
                successful_batches++;
            } else {
                ESP_LOGE(TAG_EVENT_UPLOADER, "Invalid JSON in batch, skipping");
            }
            
            batch_start = batch_end;
        }
        
        // 清空缓存
        event_cache_.clear();
        
        // 性能报告
        int64_t elapsed_us = esp_timer_get_time() - start_time;
        ESP_LOGI(TAG_EVENT_UPLOADER, "Processed %d events in %d batches, took %lldus", 
                total_events, successful_batches, elapsed_us);
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG_EVENT_UPLOADER, "Exception in ProcessCachedEvents: %s", e.what());
        // 保持缓存，稍后重试
    } catch (...) {
        ESP_LOGE(TAG_EVENT_UPLOADER, "Unknown exception in ProcessCachedEvents");
        // 保持缓存，稍后重试
    }
}

void EventUploader::ClearExpiredEvents() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    if (event_cache_.empty()) {
        return;
    }
    
    int64_t current_time_us = esp_timer_get_time();  // 微秒
    int64_t expiry_time_us = current_time_us - (EventNotificationConfig::CACHE_TIMEOUT_MS * 1000);  // 转换为微秒
    
    // 移除超过5秒的过期事件
    auto it = std::remove_if(event_cache_.begin(), event_cache_.end(),
        [expiry_time_us](const CachedEvent& event) {
            return event.end_time < expiry_time_us;
        });
    
    if (it != event_cache_.end()) {
        size_t removed = std::distance(it, event_cache_.end());
        event_cache_.erase(it, event_cache_.end());
        ESP_LOGI(TAG_EVENT_UPLOADER, "Removed %d expired events (>5s old) from cache", removed);
    }
}


size_t EventUploader::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return event_cache_.size();
}

EventUploader::CachedEvent EventUploader::ConvertEvent(const Event& event) {
    CachedEvent cached;
    cached.event_type = GetEventTypeString(event);
    cached.event_text = GenerateEventText(event);
    
    // 计算持续时间
    cached.duration_ms = CalculateDuration(event);
    
    // 使用统一的esp_timer时间轴（微秒）
    int64_t current_time_us = esp_timer_get_time();
    
    // 使用事件的实际时间戳（如果有的话）
    if (event.timestamp_us > 0) {
        cached.end_time = event.timestamp_us;
        cached.start_time = cached.end_time - (cached.duration_ms * 1000);
    } else {
        // 如果事件没有时间戳，使用当前时间
        cached.start_time = current_time_us - (cached.duration_ms * 1000);
        cached.end_time = current_time_us;
    }
    ESP_LOGD(TAG_EVENT_UPLOADER, "Using esp_timer timeline: end=%lld us, start=%lld us", 
             cached.end_time, cached.start_time);
    
    cached.event_payload = nullptr; // 通常为空
    
    // 添加当前情感状态
    {
        std::lock_guard<std::mutex> lock(emotion_mutex_);
        cached.has_emotion_state = current_has_emotion_state_;
        if (current_has_emotion_state_) {
            cached.valence = current_valence_;
            cached.arousal = current_arousal_;
        }
    }
    
    return cached; // 移动语义自动生效
}

std::string EventUploader::GetEventTypeString(const Event& event) {
    // 根据事件日志中看到的事件类型来映射
    switch (event.type) {
        // 触摸事件 - 使用新的TouchPosition枚举
        case EventType::TOUCH_TAP: {
            switch (event.data.touch_data.position) {
                case TouchPosition::LEFT: return "Touch_Left_Tap";
                case TouchPosition::RIGHT: return "Touch_Right_Tap";
                case TouchPosition::BOTH: return "Touch_Both_Tap";
                default: return "Touch_Unknown_Tap";
            }
        }
        
        case EventType::TOUCH_LONG_PRESS: {
            switch (event.data.touch_data.position) {
                case TouchPosition::LEFT: return "Touch_Left_LongPress";
                case TouchPosition::RIGHT: return "Touch_Right_LongPress";
                case TouchPosition::BOTH: return "Touch_Both_LongPress";
                default: return "Touch_Unknown_LongPress";
            }
        }
        
        case EventType::TOUCH_CRADLED:
            return "Touch_Both_Cradled";
            
        case EventType::TOUCH_TICKLED:
            return "Touch_Both_Tickled";
        
        // 运动事件
        case EventType::MOTION_SHAKE:
            return "Motion_Shake";
        case EventType::MOTION_SHAKE_VIOLENTLY:
            return "Motion_ShakeViolently";
        case EventType::MOTION_FLIP:
            return "Motion_Flip";
        case EventType::MOTION_FREE_FALL:
            return "Motion_FreeFall";
        case EventType::MOTION_PICKUP:
            return "Motion_Pickup";
        case EventType::MOTION_UPSIDE_DOWN:
            return "Motion_UpsideDown";
            
        default:
            ESP_LOGW(TAG_EVENT_UPLOADER, "Unknown event type: %d", (int)event.type);
            return "Unknown";
    }
}

std::string EventUploader::GenerateEventText(const Event& event) {
    // 生成供LLM理解的中文描述
    switch (event.type) {
        case EventType::TOUCH_TAP: {
            // 支持合并事件的多次点击
            std::string tap_text;
            switch (event.data.touch_data.position) {
                case TouchPosition::LEFT: tap_text = "主人轻轻拍了我的左侧"; break;
                case TouchPosition::RIGHT: tap_text = "主人轻轻拍了我的右侧"; break;
                case TouchPosition::BOTH: tap_text = "主人同时拍了我的两侧"; break;
                default: tap_text = "主人轻轻拍了我"; break;
            }
            
            if (event.data.touch_data.tap_count > 1) {
                return tap_text + "（连续" + std::to_string(event.data.touch_data.tap_count) + "次）";
            }
            return tap_text;
        }
        
        case EventType::TOUCH_LONG_PRESS: {
            switch (event.data.touch_data.position) {
                case TouchPosition::LEFT: return "主人长时间按住了我的左侧";
                case TouchPosition::RIGHT: return "主人长时间按住了我的右侧";
                case TouchPosition::BOTH: return "主人同时长按了我的两侧";
                default: return "主人长时间按住了我";
            }
        }
        
        case EventType::TOUCH_CRADLED:
            return "主人温柔地抱着我";
        case EventType::TOUCH_TICKLED:
            return "主人在挠我痒痒";
            
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

uint32_t EventUploader::CalculateDuration(const Event& event) {
    // 从新的TouchEventData结构中获取持续时间
    if (event.type == EventType::TOUCH_LONG_PRESS || 
        event.type == EventType::TOUCH_TAP ||
        event.type == EventType::TOUCH_CRADLED) {
        // 使用专用的duration_ms字段
        return event.data.touch_data.duration_ms;
    }
    
    // TICKLED事件有2秒的语义窗口
    if (event.type == EventType::TOUCH_TICKLED) {
        return 2000;  // 2秒内多次触摸的检测窗口
    }
    
    // 其他瞬时事件，持续时间为0
    return 0;
}

void EventUploader::SendSingleEvent(CachedEvent&& event) {
    try {
        // 验证事件
        if (!ValidateEvent(event)) {
            ESP_LOGW(TAG_EVENT_UPLOADER, "Event validation failed, skipping");
            return;
        }
        
        // 检查事件是否过期（超过5秒的事件不发送）
        int64_t current_time_us = esp_timer_get_time();
        if (current_time_us - event.end_time > EventNotificationConfig::CACHE_TIMEOUT_MS * 1000) {
            ESP_LOGW(TAG_EVENT_UPLOADER, "Event is too old (>5s), dropping it");
            return;
        }
        
        // 构建单事件JSON
        std::vector<CachedEvent> event_vec;
        event_vec.push_back(std::move(event));
        
        std::string payload = BuildEventPayload(event_vec.begin(), event_vec.end());
        
        // 验证JSON有效性
        cJSON* json = cJSON_Parse(payload.c_str());
        if (json) {
            ESP_LOGD(TAG_EVENT_UPLOADER, "✓ JSON valid, sending to server");
            cJSON_Delete(json);
            
            // 发送到服务器（Application::SendEventMessage会处理连接逻辑）
            // - 如果没有网络，会失败
            // - 如果有网络但无WebSocket，会尝试建立连接
            Application::GetInstance().SendEventMessage(payload);
        } else {
            ESP_LOGE(TAG_EVENT_UPLOADER, "✗ JSON invalid, not sending");
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG_EVENT_UPLOADER, "Exception in SendSingleEvent: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG_EVENT_UPLOADER, "Unknown exception in SendSingleEvent");
    }
}

void EventUploader::SendBatchEvents(std::vector<CachedEvent>&& events) {
    try {
        if (events.empty()) {
            ESP_LOGW(TAG_EVENT_UPLOADER, "Empty events batch, nothing to send");
            return;
        }
        
        // 验证并过滤有效事件
        std::vector<CachedEvent> valid_events;
        int64_t current_time_us = esp_timer_get_time();
        
        for (auto& event : events) {
            // 验证事件
            if (!ValidateEvent(event)) {
                ESP_LOGW(TAG_EVENT_UPLOADER, "Event validation failed, skipping: %s", 
                         event.event_type.c_str());
                continue;
            }
            
            // 检查事件是否过期
            if (current_time_us - event.end_time > EventNotificationConfig::CACHE_TIMEOUT_MS * 1000) {
                ESP_LOGW(TAG_EVENT_UPLOADER, "Event is too old (>5s), dropping: %s", 
                         event.event_type.c_str());
                continue;
            }
            
            valid_events.push_back(std::move(event));
        }
        
        if (valid_events.empty()) {
            ESP_LOGW(TAG_EVENT_UPLOADER, "No valid events in batch, nothing to send");
            return;
        }
        
        // 构建批量事件JSON payload
        std::string payload = BuildEventPayload(valid_events.begin(), valid_events.end());
        
        // 验证JSON有效性
        cJSON* json = cJSON_Parse(payload.c_str());
        if (json) {
            ESP_LOGI(TAG_EVENT_UPLOADER, "✓ Batch JSON valid, sending %zu events to server", 
                     valid_events.size());
            cJSON_Delete(json);
            
            // 发送到服务器
            Application::GetInstance().SendEventMessage(payload);
            
            ESP_LOGI(TAG_EVENT_UPLOADER, "✓ Batch events sent successfully");
        } else {
            ESP_LOGE(TAG_EVENT_UPLOADER, "✗ Batch JSON invalid, not sending");
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG_EVENT_UPLOADER, "Exception in SendBatchEvents: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG_EVENT_UPLOADER, "Unknown exception in SendBatchEvents");
    }
}

bool EventUploader::ValidateEvent(const CachedEvent& event) const {
    // 检查基本字段
    if (event.event_type.empty()) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Invalid event: empty event_type");
        return false;
    }
    
    if (event.event_text.empty()) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Invalid event: empty event_text");
        return false;
    }
    
    // 检查时间戳逻辑
    if (event.start_time <= 0 || event.end_time <= 0) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Invalid event: invalid timestamps");
        return false;
    }
    
    // 检查时间顺序
    if (event.end_time < event.start_time) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Invalid event: end_time before start_time");
        return false;
    }
    
    // 检查字符串长度限制（防止JSON过大）
    if (event.event_type.length() > 100 || event.event_text.length() > 500) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Invalid event: string too long");
        return false;
    }
    
    return true;
}