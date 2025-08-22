#ifndef ALICHUANGTEST_MOTION_ENGINE_H
#define ALICHUANGTEST_MOTION_ENGINE_H

#include "../qmi8658.h"
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cJSON.h>

// 运动事件类型
enum class MotionEventType {
    NONE,
    FREE_FALL,          // 自由落体
    SHAKE_VIOLENTLY,    // 剧烈摇晃
    FLIP,               // 设备被快速翻转
    SHAKE,              // 设备被摇晃
    PICKUP,             // 设备被拿起
    UPSIDE_DOWN,        // 设备被倒置（持续状态）
};

// 运动事件数据结构
struct MotionEvent {
    MotionEventType type;
    int64_t timestamp_us;
    ImuData imu_data;
    
    MotionEvent() : type(MotionEventType::NONE), timestamp_us(0) {}
    MotionEvent(MotionEventType t) : type(t), timestamp_us(0) {}
};

// 运动引擎类 - 专门处理IMU相关的运动检测
// 运动检测配置结构
struct MotionDetectionConfig {
    // 自由落体参数
    float free_fall_threshold_g = 0.3f;
    int64_t free_fall_min_duration_ms = 200;
    
    // 摇晃参数
    float shake_normal_threshold_g = 1.5f;
    float shake_violently_threshold_g = 3.0f;
    
    // 翻转参数
    float flip_threshold_deg_s = 400.0f;
    
    // 拿起参数
    float pickup_threshold_g = 0.15f;
    float pickup_stable_threshold_g = 0.05f;
    int pickup_stable_count = 5;
    int64_t pickup_min_duration_ms = 300;
    
    // 倒置参数
    float upside_down_threshold_g = -0.8f;
    int upside_down_stable_count = 10;
    
    // 调试参数
    int64_t debug_interval_ms = 1000;
    bool debug_enabled = false;
};

class MotionEngine {
public:
    using MotionEventCallback = std::function<void(const MotionEvent&)>;
    
    MotionEngine();
    ~MotionEngine();
    
    // 初始化引擎
    void Initialize(Qmi8658* imu);
    
    // 注册事件回调
    void RegisterCallback(MotionEventCallback callback);
    
    // 处理函数（在主循环中调用）
    void Process();
    
    // 启用/禁用运动检测
    void Enable(bool enable) { enabled_ = enable; }
    bool IsEnabled() const { return enabled_; }
    
    // 获取运动状态
    bool IsPickedUp() const { return is_picked_up_; }
    bool IsUpsideDown() const { return is_upside_down_; }
    const ImuData& GetCurrentImuData() const { return current_imu_data_; }
    
    // 配置相关方法
    void SetConfig(const MotionDetectionConfig& config) { config_ = config; debug_output_ = config.debug_enabled; }
    const MotionDetectionConfig& GetConfig() const { return config_; }
    void UpdateConfigFromJson(const cJSON* json);
    
    // 调试输出控制
    void SetDebugOutput(bool enable) { debug_output_ = enable; }
    
private:
    // IMU相关
    Qmi8658* imu_;
    bool enabled_;
    
    // 事件回调
    std::vector<MotionEventCallback> callbacks_;
    
    // 运动检测状态
    ImuData current_imu_data_;
    ImuData last_imu_data_;
    bool first_reading_;
    std::unordered_map<MotionEventType, int64_t> last_event_times_;
    int64_t last_debug_time_us_;
    bool debug_output_;
    
    // 自由落体检测的状态跟踪
    int64_t free_fall_start_time_;
    bool in_free_fall_;
    
    // 倒置检测的状态跟踪
    bool is_upside_down_;
    int upside_down_count_;
    
    // 拿起检测的状态跟踪
    bool is_picked_up_;
    int stable_count_;
    float stable_z_reference_;
    int64_t pickup_start_time_;
    
    // 运动检测配置
    MotionDetectionConfig config_;
    
    // 事件特定的冷却时间（微秒）
    static constexpr int64_t FREE_FALL_COOLDOWN_US = 500000;      // 500ms
    static constexpr int64_t SHAKE_VIOLENTLY_COOLDOWN_US = 400000; // 400ms
    static constexpr int64_t FLIP_COOLDOWN_US = 300000;           // 300ms
    static constexpr int64_t SHAKE_COOLDOWN_US = 200000;          // 200ms
    static constexpr int64_t PICKUP_COOLDOWN_US = 1000000;        // 1s
    static constexpr int64_t UPSIDE_DOWN_COOLDOWN_US = 500000;    // 500ms
    
    // 运动检测方法
    void ProcessMotionDetection();
    bool DetectFreeFall(const ImuData& data, int64_t current_time);
    bool DetectShakeViolently(const ImuData& data);
    bool DetectFlip(const ImuData& data);
    bool DetectShake(const ImuData& data);
    bool DetectPickup(const ImuData& data);
    bool DetectUpsideDown(const ImuData& data);
    float CalculateAccelMagnitude(const ImuData& data);
    float CalculateAccelDelta(const ImuData& current, const ImuData& last);
    
    // 辅助函数
    bool IsStable(const ImuData& data, const ImuData& last_data);
    
    // 事件分发
    void DispatchEvent(const MotionEvent& event);
};

#endif // ALICHUANGTEST_MOTION_ENGINE_H