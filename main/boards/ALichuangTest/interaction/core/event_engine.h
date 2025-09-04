#ifndef ALICHUANGTEST_EVENT_ENGINE_H
#define ALICHUANGTEST_EVENT_ENGINE_H

#include "../sensors/motion_engine.h"
#include "../sensors/multitouch_engine.h"
#include <driver/i2c_master.h>
#include "emotion_engine.h"
#include <functional>
#include <memory>
#include <vector>
#include <cJSON.h>

// 事件类型枚举 - 包含所有可能的事件
enum class EventType {
    // 运动事件
    MOTION_NONE,
    MOTION_FREE_FALL,      // 自由落体
    MOTION_SHAKE_VIOLENTLY,// 剧烈摇晃
    MOTION_FLIP,           // 设备被快速翻转
    MOTION_SHAKE,          // 设备被摇晃
    MOTION_PICKUP,         // 设备被拿起
    MOTION_UPSIDE_DOWN,    // 设备被倒置（持续状态）
    
    // 触摸事件
    TOUCH_TAP,             // 单击
    TOUCH_DOUBLE_TAP,      // 双击（预留）
    TOUCH_LONG_PRESS,      // 长按
    TOUCH_CRADLED,         // 摇篮模式（双侧持续触摸>2秒且IMU静止）
    TOUCH_TICKLED,         // 挠痒模式（2秒内多次无规律触摸>4次）
    TOUCH_HOLD,            // 持续按住（预留）
    TOUCH_RELEASE,         // 释放（预留）
    
    // 音频事件（预留）
    AUDIO_WAKE_WORD,
    AUDIO_SPEAKING,
    AUDIO_LISTENING,
    
    // 系统事件（预留）
    SYSTEM_BOOT,
    SYSTEM_SHUTDOWN,
    SYSTEM_ERROR
};

// 触摸事件特定数据
// 使用 touch_engine.h 中定义的 TouchPosition 枚举
struct TouchEventData {
    TouchPosition position;     // 触摸位置
    uint32_t duration_ms;      // 持续时间（毫秒）
    uint32_t tap_count;        // 点击次数（用于合并事件）
};

// 事件数据结构
struct Event {
    EventType type;
    int64_t timestamp_us;
    union {
        ImuData imu_data;           // 运动事件的IMU数据
        TouchEventData touch_data;  // 触摸事件的专用数据
        int audio_level;            // 音频事件的音量级别
        int error_code;             // 系统事件的错误码
    } data;
    
    Event() : type(EventType::MOTION_NONE), timestamp_us(0) {
        // 初始化触摸数据为默认值
        data.touch_data.position = TouchPosition::ANY;
        data.touch_data.duration_ms = 0;
        data.touch_data.tap_count = 1;
    }
    Event(EventType t) : type(t), timestamp_us(0) {
        // 初始化触摸数据为默认值
        data.touch_data.position = TouchPosition::ANY;
        data.touch_data.duration_ms = 0;
        data.touch_data.tap_count = 1;
    }
};

// 现在可以包含 event_processor.h，因为 Event 和 EventType 已定义
#include "event_processor.h"

//前向声明
class MotionEngine;
class MultitouchEngine;
class Qmi8658;

// 批量上传配置结构
struct EventUploadConfig {
    bool batch_upload_enabled = true;
    uint32_t batch_window_ms = 400;
    uint32_t max_batch_size = 10;
};

// 事件引擎类 - 作为各种事件源的协调器
class EventEngine {
public:
    using EventCallback = std::function<void(const Event&)>;
    using BatchEventCallback = std::function<void(const std::vector<Event>&)>;
    
    EventEngine();
    ~EventEngine();
    
    // 初始化引擎 - 内部创建和管理子引擎
    void Initialize();
    
    // 初始化运动引擎（内部创建）
    void InitializeMotionEngine(Qmi8658* imu, bool enable_debug = false);
    
    // 初始化多点触摸引擎（内部创建）
    void InitializeMultitouchEngine(i2c_master_bus_handle_t i2c_bus = nullptr);
    
    // 注册事件回调
    void RegisterCallback(EventCallback callback);
    void RegisterCallback(EventType type, EventCallback callback);
    void RegisterBatchCallback(BatchEventCallback callback);
    
    // 处理函数（在主循环中调用）
    void Process();
    
    // 手动触发事件
    void TriggerEvent(const Event& event);
    void TriggerEvent(EventType type);
    
    // 获取运动状态（通过MotionEngine）
    bool IsPickedUp() const;
    bool IsUpsideDown() const;
    
    // 获取触摸状态（通过MultitouchEngine）
    bool IsLeftTouched() const;
    bool IsRightTouched() const;
    
    // 获取IMU稳定状态（供MultitouchEngine使用）
    bool IsIMUStable() const;
    
    // 配置事件处理策略
    void ConfigureEventProcessing(EventType type, const EventProcessingConfig& config);
    void SetDefaultProcessingStrategy(const EventProcessingConfig& config);
    
    // 获取事件统计
    EventProcessor::EventStats GetEventStats(EventType type) const;
    
    // 更新运动引擎配置
    void UpdateMotionEngineConfig(const cJSON* json);
    
    // 批量上传配置
    void LoadUploadConfig(const cJSON* json);
    
    // 情感引擎集成
    void InitializeEmotionEngine();
    void SetEmotionReportCallback(EmotionEngine::EmotionReportCallback callback);
    
private:
    // 运动引擎（内部创建和管理）
    MotionEngine* motion_engine_;
    bool owns_motion_engine_;
    // 多点触摸引擎（内部创建和管理）
    MultitouchEngine* multitouch_engine_;
    bool owns_multitouch_engine_;
    
    // 事件处理器
    EventProcessor* event_processor_;
    
    // 情感引擎集成标记
    bool emotion_engine_initialized_;
    
    // 批量上传相关
    EventUploadConfig upload_config_;
    std::vector<Event> pending_events_;
    int64_t last_event_time_;
    BatchEventCallback batch_callback_;
    
    // 初始化子引擎的回调
    void SetupMotionEngineCallbacks();
    void SetupMultitouchEngineCallbacks();
    
    // 配置默认的事件处理策略
    void ConfigureDefaultEventProcessing();
    
    // 从配置文件或默认配置加载
    void LoadEventConfiguration();
    
    // 事件回调
    EventCallback global_callback_;
    std::vector<std::pair<EventType, EventCallback>> type_callbacks_;
    
    // 运动事件回调处理
    void OnMotionEvent(const MotionEvent& motion_event);
    
    // 触摸事件回调处理
    void OnTouchEvent(const TouchEvent& touch_event);
    
    // 事件分发
    void DispatchEvent(const Event& event);
    
    // 事件类型转换
    EventType ConvertMotionEventType(MotionEventType motion_type);
    EventType ConvertTouchEventType(TouchEventType touch_type, TouchPosition position);
    
    // 批量上传相关方法
    void AddToPendingBatch(const Event& event);
    void CheckBatchUploadTimeout();
    void FlushPendingEvents();
};

#endif // ALICHUANGTEST_EVENT_ENGINE_H