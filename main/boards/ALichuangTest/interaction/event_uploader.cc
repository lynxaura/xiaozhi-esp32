#include "event_uploader.h"
#include "application.h"     // for Application::GetInstance()
#include <esp_log.h>
#include <esp_timer.h>
#include <sys/time.h>        // for gettimeofday
#include <inttypes.h>        // for PRIu32, PRId64
#include <algorithm>         // for std::min, std::remove_if

EventUploader::EventUploader() 
    : enabled_(false), time_synced_(false) {
    
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

void EventUploader::HandleEvent(const Event& event) {
    if (!enabled_) {
        ESP_LOGD(TAG_EVENT_UPLOADER, "EventUploader disabled, ignoring event");
        return;
    }
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "=== Event Processing Debug ===");
    ESP_LOGI(TAG_EVENT_UPLOADER, "Raw event type: %d", (int)event.type);
    ESP_LOGI(TAG_EVENT_UPLOADER, "Touch data: x=%d, y=%d", 
             event.data.touch_data.x, event.data.touch_data.y);
    
    // 转换事件
    auto cached = ConvertEvent(event);
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "Event converted: %s -> %s", 
             cached.event_type.c_str(), cached.event_text.c_str());
    ESP_LOGI(TAG_EVENT_UPLOADER, "Duration: %lums, Start: %lld, End: %lld", 
             (long unsigned)cached.duration_ms, (long long)cached.start_time, (long long)cached.end_time);
    
    // 检查是否应该立即发送或缓存
    bool can_send = time_synced_;  // 暂时只检查时间同步，连接检查在SendEventMessage中处理
    
    if (can_send) {
        // 立即发送
        SendSingleEvent(std::move(cached));
    } else {
        // 添加到缓存
        ESP_LOGI(TAG_EVENT_UPLOADER, "Time not synced, caching event");
        AddToCache(std::move(cached));
    }
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "✓ Event processing completed");
    
    // 定期清理过期事件（性能优化）
    static uint32_t cleanup_counter = 0;
    if (++cleanup_counter % 50 == 0) {  // 每50个事件清理一次
        ClearExpiredEvents();
    }
}

void EventUploader::OnConnectionOpened() {
    ESP_LOGI(TAG_EVENT_UPLOADER, "Connection opened - processing cached events");
    ProcessCachedEvents();
}

void EventUploader::OnConnectionClosed() {
    ESP_LOGW(TAG_EVENT_UPLOADER, "Connection closed - events will be cached");
}

void EventUploader::OnTimeSynced() {
    time_synced_ = true;
    ESP_LOGI(TAG_EVENT_UPLOADER, "Time synchronized - backfilling cached event timestamps");
    
    // 获取当前时间用于回填计算
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t current_unix_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    int64_t current_mono_ms = esp_timer_get_time() / 1000;
    
    // 计算时间偏移量
    int64_t time_offset = current_unix_ms - current_mono_ms;
    
    // 回填缓存事件的时间戳
    BackfillCachedTimestamps(time_offset);
    
    // 处理缓存事件
    ProcessCachedEvents();
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
    if (!time_synced_) {
        ESP_LOGD(TAG_EVENT_UPLOADER, "Time not synced, skipping cache processing");
        return;
    }
    
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
    
    int64_t current_time = esp_timer_get_time() / 1000;  // 转换为毫秒
    int64_t expiry_time = current_time - EventNotificationConfig::CACHE_TIMEOUT_MS;
    
    // 移除过期的事件
    auto it = std::remove_if(event_cache_.begin(), event_cache_.end(),
        [expiry_time](const CachedEvent& event) {
            return event.mono_ms < expiry_time;
        });
    
    if (it != event_cache_.end()) {
        size_t removed = std::distance(it, event_cache_.end());
        event_cache_.erase(it, event_cache_.end());
        ESP_LOGI(TAG_EVENT_UPLOADER, "Removed %d expired events from cache", removed);
    }
}

void EventUploader::BackfillCachedTimestamps(int64_t time_offset) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    if (event_cache_.empty()) {
        ESP_LOGD(TAG_EVENT_UPLOADER, "No cached events to backfill");
        return;
    }
    
    int backfilled_count = 0;
    for (auto& cached_event : event_cache_) {
        if (cached_event.start_time == 0 && cached_event.end_time == 0) {
            // 使用单调时钟时间 + 偏移量计算Unix时间戳
            cached_event.start_time = cached_event.mono_ms + time_offset - cached_event.duration_ms;
            cached_event.end_time = cached_event.mono_ms + time_offset;
            backfilled_count++;
        }
    }
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "Backfilled timestamps for %d cached events", backfilled_count);
}

size_t EventUploader::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return event_cache_.size();
}

EventUploader::CachedEvent EventUploader::ConvertEvent(const Event& event) {
    CachedEvent cached;
    cached.event_type = GetEventTypeString(event);
    cached.event_text = GenerateEventText(event);
    
    // 记录单调时钟时间（始终可用）
    cached.mono_ms = esp_timer_get_time() / 1000;  // 微秒转毫秒
    
    // 计算持续时间
    cached.duration_ms = CalculateDuration(event);
    
    // 获取系统时间
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t system_time_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    
    // 如果时间已同步，使用真实时间戳；否则使用系统时间（从1970开始但可能不准确）
    if (time_synced_) {
        cached.start_time = system_time_ms - cached.duration_ms;
        cached.end_time = system_time_ms;
        ESP_LOGD(TAG_EVENT_UPLOADER, "Using synced time: %lld", system_time_ms);
    } else {
        // 使用系统时间，但标记为未同步
        cached.start_time = system_time_ms - cached.duration_ms;
        cached.end_time = system_time_ms;
        ESP_LOGD(TAG_EVENT_UPLOADER, "Using unsynced system time: %lld (may be 1970-based)", system_time_ms);
    }
    
    cached.event_payload = nullptr; // 通常为空
    return cached; // 移动语义自动生效
}

std::string EventUploader::GetEventTypeString(const Event& event) {
    // 根据事件日志中看到的事件类型来映射
    switch (event.type) {
        // 触摸事件 - 需要结合位置信息
        case EventType::TOUCH_TAP: {
            // 从event.data.touch_data.x获取位置信息
            // 根据现有日志，LEFT position=0, RIGHT position=1
            if (event.data.touch_data.x == -1) {
                return "Touch_Left_Tap";
            } else if (event.data.touch_data.x == 1) {
                return "Touch_Right_Tap";
            } else {
                return "Touch_Both_Tap";
            }
        }
        
        case EventType::TOUCH_LONG_PRESS: {
            if (event.data.touch_data.x == -1) {
                return "Touch_Left_LongPress";
            } else if (event.data.touch_data.x == 1) {
                return "Touch_Right_LongPress";
            } else {
                return "Touch_Both_LongPress";
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
            if (event.data.touch_data.x == -1) {
                return "主人轻轻拍了我的左侧";
            } else if (event.data.touch_data.x == 1) {
                return "主人轻轻拍了我的右侧";
            } else {
                return "主人同时拍了我的两侧";
            }
        }
        
        case EventType::TOUCH_LONG_PRESS: {
            if (event.data.touch_data.x == -1) {
                return "主人长时间按住了我的左侧";
            } else if (event.data.touch_data.x == 1) {
                return "主人长时间按住了我的右侧";
            } else {
                return "主人同时长按了我的两侧";
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
    // 从event.data.touch_data.y中获取持续时间（毫秒）
    if (event.type == EventType::TOUCH_LONG_PRESS || 
        event.type == EventType::TOUCH_TAP ||
        event.type == EventType::TOUCH_CRADLED) {
        // touch_data.y存储了duration_ms
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

void EventUploader::SendSingleEvent(CachedEvent&& event) {
    try {
        // 验证事件
        if (!ValidateEvent(event)) {
            ESP_LOGW(TAG_EVENT_UPLOADER, "Event validation failed, skipping");
            return;
        }
        
        // 构建单事件JSON
        std::vector<CachedEvent> event_vec;
        event_vec.push_back(std::move(event));
        
        std::string payload = BuildEventPayload(event_vec.begin(), event_vec.end());
        ESP_LOGI(TAG_EVENT_UPLOADER, "Generated JSON: %s", payload.c_str());
        
        // 验证JSON有效性
        cJSON* json = cJSON_Parse(payload.c_str());
        if (json) {
            ESP_LOGD(TAG_EVENT_UPLOADER, "✓ JSON valid, sending to server");
            cJSON_Delete(json);
            
            // 发送到服务器
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
    if (time_synced_ && (event.start_time <= 0 || event.end_time <= 0)) {
        ESP_LOGW(TAG_EVENT_UPLOADER, "Invalid event: invalid timestamps when synced");
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