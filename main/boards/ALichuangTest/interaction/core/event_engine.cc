#include "event_engine.h"
#include "../sensors/motion_engine.h"
#include "../sensors/multitouch_engine.h"
#include "event_processor.h"
#include "../config/event_config_loader.h"
#include "emotion_engine.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <vector>

#define TAG "EventEngine"

EventEngine::EventEngine() 
    : motion_engine_(nullptr)
    , owns_motion_engine_(false)
    , multitouch_engine_(nullptr)
    , owns_multitouch_engine_(false)
    , event_processor_(nullptr)
    , emotion_engine_initialized_(false)
    , last_event_time_(0)
    , batch_callback_(nullptr) {
    // 创建事件处理器
    event_processor_ = new EventProcessor();
    
    // 初始化批量上传配置（默认值）
    upload_config_.batch_upload_enabled = true;
    upload_config_.batch_window_ms = 500;
    upload_config_.max_batch_size = 10;
    
    // 预留事件列表空间
    pending_events_.reserve(upload_config_.max_batch_size);
}

EventEngine::~EventEngine() {
    // 清理内部创建的引擎
    if (owns_motion_engine_ && motion_engine_) {
        delete motion_engine_;
        motion_engine_ = nullptr;
    }
    if (owns_multitouch_engine_ && multitouch_engine_) {
        delete multitouch_engine_;
        multitouch_engine_ = nullptr;
    }
    if (event_processor_) {
        delete event_processor_;
        event_processor_ = nullptr;
    }
}

void EventEngine::Initialize() {
    // 尝试从配置文件加载，如果失败则使用默认配置
    LoadEventConfiguration();
    ESP_LOGI(TAG, "Event engine initialized with event processor");
}

void EventEngine::LoadEventConfiguration() {
    // 首先尝试从文件系统加载配置
    const char* config_path = "/spiffs/event_config.json";
    bool loaded = EventConfigLoader::LoadFromFile(config_path, this);
    
    if (!loaded) {
        // 如果文件不存在或加载失败，使用嵌入的默认配置
        ESP_LOGI(TAG, "Loading embedded default event configuration");
        EventConfigLoader::LoadFromEmbedded(this);
    }
}

void EventEngine::ConfigureDefaultEventProcessing() {
    // 这个方法现在作为备用，如果配置加载完全失败时使用
    // 触摸事件：使用冷却策略，避免连续触发
    event_processor_->ConfigureEventType(EventType::TOUCH_TAP, 
        EventProcessingPresets::TouchTapConfig());
    
    // 运动事件：使用节流策略
    event_processor_->ConfigureEventType(EventType::MOTION_SHAKE, 
        EventProcessingPresets::MotionEventConfig());
    event_processor_->ConfigureEventType(EventType::MOTION_FLIP, 
        EventProcessingPresets::MotionEventConfig());
    
    // 紧急事件：立即处理
    event_processor_->ConfigureEventType(EventType::MOTION_FREE_FALL, 
        EventProcessingPresets::EmergencyEventConfig());
    
    ESP_LOGI(TAG, "Fallback event processing strategies configured");
}

void EventEngine::ConfigureEventProcessing(EventType type, const EventProcessingConfig& config) {
    if (event_processor_) {
        event_processor_->ConfigureEventType(type, config);
    }
}

void EventEngine::SetDefaultProcessingStrategy(const EventProcessingConfig& config) {
    if (event_processor_) {
        event_processor_->SetDefaultStrategy(config);
    }
}

EventProcessor::EventStats EventEngine::GetEventStats(EventType type) const {
    if (event_processor_) {
        return event_processor_->GetStats(type);
    }
    return EventProcessor::EventStats{0, 0, 0, 0, 0};
}

void EventEngine::UpdateMotionEngineConfig(const cJSON* json) {
    if (motion_engine_ && json) {
        motion_engine_->UpdateConfigFromJson(json);
    }
}

void EventEngine::InitializeMotionEngine(Qmi8658* imu, bool enable_debug) {
    if (!imu) {
        ESP_LOGW(TAG, "Cannot initialize motion engine without IMU");
        return;
    }
    
    // 如果已经存在旧的引擎，先清理
    if (owns_motion_engine_ && motion_engine_) {
        delete motion_engine_;
    }
    
    // 创建新的运动引擎
    motion_engine_ = new MotionEngine();
    motion_engine_->Initialize(imu);
    
    if (enable_debug) {
        motion_engine_->SetDebugOutput(true);
    }
    
    owns_motion_engine_ = true;
    
    // 设置回调
    SetupMotionEngineCallbacks();
    
    ESP_LOGI(TAG, "Motion engine initialized and registered with event engine");
}

void EventEngine::InitializeMultitouchEngine(i2c_master_bus_handle_t i2c_bus) {
    // 如果已经存在旧的引擎，先清理
    if (owns_multitouch_engine_ && multitouch_engine_) {
        delete multitouch_engine_;
    }
    
    // 创建新的多点触摸引擎
    if (i2c_bus) {
        multitouch_engine_ = new MultitouchEngine(i2c_bus);
    } else {
        ESP_LOGW(TAG, "No I2C bus provided, using default constructor (may fail)");
        multitouch_engine_ = new MultitouchEngine();
    }
    multitouch_engine_->Initialize();
    owns_multitouch_engine_ = true;
    
    // 设置回调
    SetupMultitouchEngineCallbacks();
    
    ESP_LOGI(TAG, "Multitouch engine initialized and registered with event engine - MPR121 @ 0x5A (polling mode)");
}

void EventEngine::SetupMotionEngineCallbacks() {
    if (motion_engine_) {
        motion_engine_->RegisterCallback(
            [this](const MotionEvent& event) {
                this->OnMotionEvent(event);
            }
        );
    }
}

void EventEngine::SetupMultitouchEngineCallbacks() {
    if (multitouch_engine_) {
        ESP_LOGI(TAG, "Registering multitouch engine callback");
        multitouch_engine_->RegisterCallback(
            [this](const TouchEvent& event) {
                ESP_LOGI(TAG, "Lambda callback invoked for multitouch event");
                this->OnTouchEvent(event);
            }
        );
        
        // 设置IMU稳定性查询回调
        multitouch_engine_->SetIMUStabilityCallback(
            [this]() -> bool {
                return this->IsIMUStable();
            }
        );
        
        ESP_LOGI(TAG, "Multitouch engine callback and IMU stability callback registered");
    } else {
        ESP_LOGW(TAG, "Multitouch engine is null, cannot register callback");
    }
}

void EventEngine::InitializeEmotionEngine() {
    if (emotion_engine_initialized_) {
        ESP_LOGW(TAG, "Emotion engine already initialized");
        return;
    }
    
    // 获取情感引擎单例并初始化
    EmotionEngine& emotion_engine = EmotionEngine::GetInstance();
    emotion_engine.Initialize();
    emotion_engine_initialized_ = true;
    
    ESP_LOGI(TAG, "Emotion engine initialized and integrated with event engine");
}

void EventEngine::SetEmotionReportCallback(EmotionEngine::EmotionReportCallback callback) {
    if (!emotion_engine_initialized_) {
        ESP_LOGW(TAG, "Emotion engine not initialized, call InitializeEmotionEngine() first");
        return;
    }
    
    EmotionEngine& emotion_engine = EmotionEngine::GetInstance();
    emotion_engine.SetEmotionReportCallback(callback);
    ESP_LOGI(TAG, "Emotion report callback set");
}

void EventEngine::RegisterCallback(EventCallback callback) {
    global_callback_ = callback;
}

void EventEngine::RegisterCallback(EventType type, EventCallback callback) {
    type_callbacks_.push_back({type, callback});
}

void EventEngine::RegisterBatchCallback(BatchEventCallback callback) {
    batch_callback_ = callback;
}

void EventEngine::Process() {
    // 处理运动引擎
    if (motion_engine_) {
        motion_engine_->Process();
    }
    
    // 检查批量上传超时
    CheckBatchUploadTimeout();
    
    // 注意：MultitouchEngine有自己的任务，不需要在这里调用Process
    // MultitouchEngine的事件会通过回调异步到达
}

void EventEngine::TriggerEvent(const Event& event) {
    DispatchEvent(event);
}

void EventEngine::TriggerEvent(EventType type) {
    Event event;
    event.type = type;
    event.timestamp_us = esp_timer_get_time();
    DispatchEvent(event);
}

void EventEngine::OnMotionEvent(const MotionEvent& motion_event) {
    // 将MotionEvent转换为Event
    Event event;
    event.type = ConvertMotionEventType(motion_event.type);
    event.timestamp_us = motion_event.timestamp_us;
    event.data.imu_data = motion_event.imu_data;
    
    // 分发事件
    DispatchEvent(event);
}

void EventEngine::DispatchEvent(const Event& event) {
    ESP_LOGI(TAG, "DispatchEvent called with event type=%d", (int)event.type);
    
    // 通过事件处理器处理事件
    Event processed_event;
    bool should_process = event_processor_->ProcessEvent(event, processed_event);
    
    if (!should_process) {
        // 事件被丢弃（防抖、节流、冷却等）
        return;
    }
    
    // 如果情感引擎已初始化，则更新情感状态
    if (emotion_engine_initialized_) {
        ESP_LOGD(TAG, "Updating emotion state for event type=%d", (int)processed_event.type);
        EmotionEngine& emotion_engine = EmotionEngine::GetInstance();
        emotion_engine.OnEvent(processed_event);
    } else {
        ESP_LOGW(TAG, "Emotion engine not initialized, skipping emotion update for event type=%d", (int)processed_event.type);
    }
    
    // 调用全局回调（单个事件处理，保持兼容性）
    if (global_callback_) {
        global_callback_(processed_event);
    }
    
    // 调用特定类型的回调（单个事件处理，保持兼容性）
    for (const auto& pair : type_callbacks_) {
        if (pair.first == processed_event.type) {
            pair.second(processed_event);
        }
    }
    
    // 批量上传处理
    if (upload_config_.batch_upload_enabled && batch_callback_) {
        AddToPendingBatch(processed_event);
    }
    
    // 处理队列中的事件
    Event queued_event;
    while (event_processor_->GetNextQueuedEvent(queued_event)) {
        // 队列中的事件也需要更新情感状态
        if (emotion_engine_initialized_) {
            ESP_LOGD(TAG, "Updating emotion state for queued event type=%d", (int)queued_event.type);
            EmotionEngine& emotion_engine = EmotionEngine::GetInstance();
            emotion_engine.OnEvent(queued_event);
        }
        
        if (global_callback_) {
            global_callback_(queued_event);
        }
        for (const auto& pair : type_callbacks_) {
            if (pair.first == queued_event.type) {
                pair.second(queued_event);
            }
        }
    }
}

EventType EventEngine::ConvertMotionEventType(MotionEventType motion_type) {
    switch (motion_type) {
        case MotionEventType::FREE_FALL:
            return EventType::MOTION_FREE_FALL;
        case MotionEventType::SHAKE_VIOLENTLY:
            return EventType::MOTION_SHAKE_VIOLENTLY;
        case MotionEventType::FLIP:
            return EventType::MOTION_FLIP;
        case MotionEventType::SHAKE:
            return EventType::MOTION_SHAKE;
        case MotionEventType::PICKUP:
            return EventType::MOTION_PICKUP;
        case MotionEventType::UPSIDE_DOWN:
            return EventType::MOTION_UPSIDE_DOWN;
        case MotionEventType::NONE:
        default:
            return EventType::MOTION_NONE;
    }
}

bool EventEngine::IsPickedUp() const {
    if (motion_engine_) {
        return motion_engine_->IsPickedUp();
    }
    return false;
}

bool EventEngine::IsUpsideDown() const {
    if (motion_engine_) {
        return motion_engine_->IsUpsideDown();
    }
    return false;
}

bool EventEngine::IsLeftTouched() const {
    if (multitouch_engine_) {
        return multitouch_engine_->IsLeftTouched();
    }
    return false;
}

bool EventEngine::IsRightTouched() const {
    if (multitouch_engine_) {
        return multitouch_engine_->IsRightTouched();
    }
    return false;
}

bool EventEngine::IsIMUStable() const {
    if (motion_engine_) {
        return motion_engine_->IsCurrentlyStable();
    }
    return false;  // 没有motion_engine时认为不稳定
}

void EventEngine::OnTouchEvent(const TouchEvent& touch_event) {
    // 将TouchEvent转换为Event
    Event event;
    event.type = ConvertTouchEventType(touch_event.type, touch_event.position);
    event.timestamp_us = touch_event.timestamp_us;
    
    // 如果事件类型无效，不处理
    if (event.type == EventType::MOTION_NONE) {
        ESP_LOGD(TAG, "Touch event type %d not mapped, ignoring", (int)touch_event.type);
        return;
    }
    
    // 使用新的TouchEventData结构
    if (event.type == EventType::TOUCH_CRADLED || event.type == EventType::TOUCH_TICKLED) {
        event.data.touch_data.position = TouchPosition::BOTH;
        event.data.touch_data.duration_ms = touch_event.duration_ms;
    } else {
        event.data.touch_data.position = touch_event.position;
        event.data.touch_data.duration_ms = touch_event.duration_ms;
    }
    event.data.touch_data.tap_count = 1;  // 初始点击次数为1
    
    const char* position_str = "UNKNOWN";
    switch (event.data.touch_data.position) {
        case TouchPosition::LEFT: position_str = "LEFT"; break;
        case TouchPosition::RIGHT: position_str = "RIGHT"; break;
        case TouchPosition::BOTH: position_str = "BOTH"; break;
        case TouchPosition::ANY: position_str = "ANY"; break;
    }
    
    ESP_LOGI(TAG, "Touch event received: touch_type=%d -> event_type=%d, position=%s, duration=%lums", 
            (int)touch_event.type,
            (int)event.type,
            position_str,
            (unsigned long)event.data.touch_data.duration_ms);
    
    // 分发事件
    DispatchEvent(event);
}

EventType EventEngine::ConvertTouchEventType(TouchEventType touch_type, TouchPosition position) {
    // 根据触摸类型和位置映射到事件类型
    switch (touch_type) {
        case TouchEventType::SINGLE_TAP:
            return EventType::TOUCH_TAP;  // 左右侧单击都映射为TAP
            
        case TouchEventType::HOLD:
            return EventType::TOUCH_LONG_PRESS;
            
        case TouchEventType::RELEASE:
            // 释放事件暂时不处理
            return EventType::MOTION_NONE;
            
        case TouchEventType::CRADLED:
            ESP_LOGI(TAG, "CRADLED event detected, mapping to TOUCH_CRADLED");
            return EventType::TOUCH_CRADLED;
            
        case TouchEventType::TICKLED:
            ESP_LOGI(TAG, "TICKLED event detected, mapping to TOUCH_TICKLED");
            return EventType::TOUCH_TICKLED;
            
        default:
            return EventType::MOTION_NONE;
    }
}

// 批量上传相关方法实现
void EventEngine::AddToPendingBatch(const Event& event) {
    int64_t current_time = esp_timer_get_time();
    
    // 添加事件到待上传队列
    pending_events_.push_back(event);
    last_event_time_ = current_time;
    
    ESP_LOGD(TAG, "Added event type=%d to batch, total events=%zu", 
             (int)event.type, pending_events_.size());
    
    // 检查是否达到最大批量大小，立即上传
    if (pending_events_.size() >= upload_config_.max_batch_size) {
        ESP_LOGI(TAG, "Batch size limit reached (%d), flushing immediately", 
                 upload_config_.max_batch_size);
        FlushPendingEvents();
    }
}

void EventEngine::CheckBatchUploadTimeout() {
    if (pending_events_.empty()) {
        return;
    }
    
    int64_t current_time = esp_timer_get_time();
    int64_t time_since_last_event = current_time - last_event_time_;
    
    // 检查是否超过批量窗口时间
    if (time_since_last_event >= upload_config_.batch_window_ms * 1000) { // 转换为微秒
        ESP_LOGI(TAG, "Batch window timeout (%.1fms), flushing %zu events",
                 time_since_last_event / 1000.0f, pending_events_.size());
        FlushPendingEvents();
    }
}

void EventEngine::FlushPendingEvents() {
    if (pending_events_.empty() || !batch_callback_) {
        return;
    }
    
    ESP_LOGI(TAG, "Flushing batch with %zu events", pending_events_.size());
    
    // 按时间戳排序事件（确保顺序正确）
    std::sort(pending_events_.begin(), pending_events_.end(), 
              [](const Event& a, const Event& b) {
                  return a.timestamp_us < b.timestamp_us;
              });
    
    // 调用批量回调
    batch_callback_(pending_events_);
    
    // 清空待上传队列
    pending_events_.clear();
    last_event_time_ = 0;
}

void EventEngine::LoadUploadConfig(const cJSON* json) {
    if (!json) return;
    
    // 查找事件上传配置节点
    const cJSON* upload_config = cJSON_GetObjectItem(json, "event_upload_config");
    if (!upload_config) {
        ESP_LOGW(TAG, "No event_upload_config found, using defaults");
        return;
    }
    
    // 加载批量上传启用状态
    const cJSON* enabled = cJSON_GetObjectItem(upload_config, "batch_upload_enabled");
    if (enabled && cJSON_IsBool(enabled)) {
        upload_config_.batch_upload_enabled = cJSON_IsTrue(enabled);
    }
    
    // 加载批量窗口时间
    const cJSON* window_ms = cJSON_GetObjectItem(upload_config, "batch_window_ms");
    if (window_ms && cJSON_IsNumber(window_ms)) {
        upload_config_.batch_window_ms = window_ms->valueint;
    }
    
    // 加载最大批量大小
    const cJSON* max_size = cJSON_GetObjectItem(upload_config, "max_batch_size");
    if (max_size && cJSON_IsNumber(max_size)) {
        upload_config_.max_batch_size = max_size->valueint;
        // 重新预留事件列表空间
        pending_events_.reserve(upload_config_.max_batch_size);
    }
    
    ESP_LOGI(TAG, "Upload config loaded: enabled=%d, window=%ums, max_size=%u", 
             upload_config_.batch_upload_enabled, 
             upload_config_.batch_window_ms,
             upload_config_.max_batch_size);
}