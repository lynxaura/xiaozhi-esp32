#ifndef EVENT_UPLOADER_H
#define EVENT_UPLOADER_H

#include "../core/event_engine.h"
#include "event_notification_config.h"
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>      // for std::min
#include <functional>     // for std::function
#include <string>
#include <cJSON.h>

#define TAG_EVENT_UPLOADER "EventUploader"

// cJSON智能指针，防止double free
struct CJsonDeleter { 
    void operator()(cJSON* p) const { 
        if (p) cJSON_Delete(p); 
    } 
};
using cjson_uptr = std::unique_ptr<cJSON, CJsonDeleter>;

class EventUploader {
public:
    // 前向声明缓存事件结构体
    struct CachedEvent {
        std::string event_type;      // 事件类型（如 "Touch_Left_Tap"）
        std::string event_text;      // 事件描述文本（中文）
        int64_t start_time;          // 事件开始时间（esp_timer微秒）
        int64_t end_time;            // 事件结束时间（esp_timer微秒）
        uint32_t duration_ms;        // 事件持续时间（毫秒）
        cjson_uptr event_payload;    // 额外数据（通常为空），智能指针管理
        
        // 情感状态字段
        bool has_emotion_state;      // 是否包含情感状态
        float valence;               // 效价值 [-1.0, +1.0]
        float arousal;               // 激活度 [-1.0, +1.0]
        
        // 默认构造函数，unique_ptr自动初始化为nullptr
        CachedEvent() : has_emotion_state(false), valence(0.0f), arousal(0.0f) {}
        
        // 支持移动，禁止拷贝（防止double free）
        CachedEvent(CachedEvent&&) = default;
        CachedEvent& operator=(CachedEvent&&) = default;
        CachedEvent(const CachedEvent&) = delete;
        CachedEvent& operator=(const CachedEvent&) = delete;
    };
    
    EventUploader();
    ~EventUploader();
    
    // 处理事件（决定立即发送或缓存）
    void HandleEvent(const Event& event);
    
    // 尝试发送或缓存事件（新方法）
    void TrySendOrCache(CachedEvent&& event);
    
    // 启用/禁用通知
    void Enable(bool enable) { enabled_ = enable; }
    
    // 设置当前情感状态
    void SetCurrentEmotionState(float valence, float arousal);
    
    // 连接状态回调
    void OnConnectionOpened();
    void OnConnectionClosed();
    
    // 缓存管理
    void AddToCache(CachedEvent&& event);
    void ProcessCachedEvents();
    void ClearExpiredEvents();
    size_t GetCacheSize() const;
    
private:
    // 基础成员变量
    bool enabled_;
    std::string device_id_;  // 设备唯一标识
    
    // 事件缓存相关
    std::vector<CachedEvent> event_cache_;
    mutable std::mutex cache_mutex_;
    
    // 事件序列号，用于生成唯一event_id
    std::atomic<uint32_t> event_sequence_{0};
    
    // 当前情感状态
    std::mutex emotion_mutex_;
    bool current_has_emotion_state_;
    float current_valence_;
    float current_arousal_;
    
    // 获取设备ID的方法
    std::string GenerateDeviceId();
    
    // 事件转换方法
    CachedEvent ConvertEvent(const Event& event);
    std::string GetEventTypeString(const Event& event);
    std::string GenerateEventText(const Event& event);
    uint32_t CalculateDuration(const Event& event);
    
    // JSON构建方法（模板定义在头文件）
    template<class It>
    std::string BuildEventPayload(It first, It last) {
        // 只构建payload部分，session_id和type由Application添加
        cJSON* payload = cJSON_CreateObject();
        cJSON* events_array = cJSON_CreateArray();
        
        for (auto it = first; it != last; ++it) {
            const auto& event = *it;
            cJSON* event_obj = cJSON_CreateObject();
            
            // 添加事件字段
            cJSON_AddStringToObject(event_obj, "event_type", event.event_type.c_str());
            cJSON_AddStringToObject(event_obj, "event_text", event.event_text.c_str());
            cJSON_AddNumberToObject(event_obj, "start_time", event.start_time);
            cJSON_AddNumberToObject(event_obj, "end_time", event.end_time);
            
            // 添加情感状态字段
            if (event.has_emotion_state) {
                cJSON* emotion_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(emotion_obj, "valence", event.valence);
                cJSON_AddNumberToObject(emotion_obj, "arousal", event.arousal);
                cJSON_AddItemToObject(event_obj, "emotion_state", emotion_obj);
            }
            
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
    
    // 私有辅助方法
    void SendSingleEvent(CachedEvent&& event);
    bool ValidateEvent(const CachedEvent& event) const;
};

#endif // EVENT_UPLOADER_H