#ifndef EVENT_NOTIFICATION_CONFIG_H
#define EVENT_NOTIFICATION_CONFIG_H

// 事件上传配置
struct EventNotificationConfig {
    // 基本开关
    static constexpr bool ENABLED = true;
    
    // 通知策略
    static constexpr bool IMMEDIATE_SEND = true;         // 立即发送
    static constexpr int MAX_CACHE_SIZE = 10;            // 最大缓存数（减少到10个）
    static constexpr int CACHE_TIMEOUT_MS = 5000;        // 5秒超时（避免发送过期事件）
    static constexpr int BATCH_SIZE = 5;                 // 批量发送大小（减少延迟）
    
    // 事件类型过滤
    static constexpr bool NOTIFY_TOUCH_EVENTS = true;
    static constexpr bool NOTIFY_MOTION_EVENTS = true;
    static constexpr bool NOTIFY_DEVICE_STATE = true;
    
    // 调试选项
    static constexpr bool LOG_NOTIFICATIONS = true;
    static constexpr bool LOG_VERBOSE = false;
};

#endif // EVENT_NOTIFICATION_CONFIG_H