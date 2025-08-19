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
        int64_t timestamp_ms;   // 事件发生时间（Unix时间戳，毫秒）
        uint32_t duration_ms;   // 事件持续时间（毫秒，0表示瞬时事件）
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
    uint32_t GetEventDuration(const Event& event);       // 获取事件持续时间
    
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
            cJSON_AddNumberToObject(event_obj, "timestamp", event.timestamp_ms);  // 整型毫秒
            
            // 只有持续时间大于0时才添加duration_ms字段
            if (event.duration_ms > 0) {
                cJSON_AddNumberToObject(event_obj, "duration_ms", event.duration_ms);
            }
            
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