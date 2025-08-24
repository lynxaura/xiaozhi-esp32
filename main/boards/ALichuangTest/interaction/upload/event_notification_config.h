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

#endif // EVENT_NOTIFICATION_CONFIG_H