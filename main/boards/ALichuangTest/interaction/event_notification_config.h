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

// 事件优先级定义
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

#endif // EVENT_NOTIFICATION_CONFIG_H