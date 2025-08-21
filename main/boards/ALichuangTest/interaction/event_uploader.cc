#include "event_uploader.h"
#include "application.h"     // for Application::GetInstance()
#include <esp_log.h>
#include <esp_timer.h>
#include <sys/time.h>        // for gettimeofday
#include <inttypes.h>        // for PRIu32, PRId64

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
    
    // 发送事件（单事件）
    std::vector<CachedEvent> event_vec;
    event_vec.push_back(std::move(cached));
    
    std::string payload = BuildEventPayload(event_vec.begin(), event_vec.end());
    ESP_LOGI(TAG_EVENT_UPLOADER, "Generated JSON: %s", payload.c_str());
    
    // 验证JSON有效性
    cJSON* json = cJSON_Parse(payload.c_str());
    if (json) {
        ESP_LOGI(TAG_EVENT_UPLOADER, "✓ JSON valid, sending to server");
        cJSON_Delete(json);
        
        // 发送到服务器
        Application::GetInstance().SendEventMessage(payload);
    } else {
        ESP_LOGE(TAG_EVENT_UPLOADER, "✗ JSON invalid, not sending");
    }
    
    ESP_LOGI(TAG_EVENT_UPLOADER, "✓ Event processing completed");
}

void EventUploader::OnConnectionOpened() {
    ESP_LOGI(TAG_EVENT_UPLOADER, "Connection opened");
    // TODO: Phase 5会实现连接管理逻辑
}

void EventUploader::OnConnectionClosed() {
    ESP_LOGI(TAG_EVENT_UPLOADER, "Connection closed");
    // TODO: Phase 5会实现连接管理逻辑
}

void EventUploader::OnTimeSynced() {
    time_synced_ = true;
    ESP_LOGI(TAG_EVENT_UPLOADER, "Time synchronized");
    // TODO: Phase 6会实现时间同步处理逻辑
}

EventUploader::CachedEvent EventUploader::ConvertEvent(const Event& event) {
    CachedEvent cached;
    cached.event_type = GetEventTypeString(event);
    cached.event_text = GenerateEventText(event);
    
    // 记录单调时钟时间（始终可用）
    cached.mono_ms = esp_timer_get_time() / 1000;  // 微秒转毫秒
    
    // 计算持续时间
    cached.duration_ms = CalculateDuration(event);
    
    // 如果时间已同步，计算Unix时间戳
    if (time_synced_) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        cached.start_time = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
        cached.end_time = cached.start_time + cached.duration_ms;
    } else {
        // 时间未同步，先设为0，等同步后再回填
        cached.start_time = 0;
        cached.end_time = 0;
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