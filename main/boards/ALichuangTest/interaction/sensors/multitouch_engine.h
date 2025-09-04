#ifndef ALICHUANGTEST_MULTITOUCH_ENGINE_H
#define ALICHUANGTEST_MULTITOUCH_ENGINE_H

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include <vector>
#include <algorithm>
#include "../config/touch_config.h"

// 触摸事件类型 - 与touch_engine保持一致
enum class TouchEventType {
    NONE,
    SINGLE_TAP,     // 单击（左或右，<500ms）
    HOLD,           // 长按（>500ms）
    RELEASE,        // 释放（之前有HOLD）
    CRADLED,        // 摇篮模式（双侧持续触摸>2秒且IMU静止）
    TICKLED,        // 挠痒模式（2秒内多次无规律触摸>4次）
};

// 触摸位置 - 与touch_engine保持一致
enum class TouchPosition {
    LEFT,           // 左侧电极
    RIGHT,          // 右侧电极
    BOTH,           // 双侧同时
    ANY,            // 任意侧（用于tickled事件）
};

// 触摸事件数据结构 - 与touch_engine保持一致
struct TouchEvent {
    TouchEventType type;
    TouchPosition position;
    int64_t timestamp_us;
    uint32_t duration_ms;  // 对于HOLD和RELEASE事件，记录持续时间
    
    TouchEvent() : type(TouchEventType::NONE), position(TouchPosition::LEFT), 
                   timestamp_us(0), duration_ms(0) {}
};

// 多点触摸引擎类 - 基于MPR121芯片的I2C触摸传感器
class MultitouchEngine {
public:
    using TouchEventCallback = std::function<void(const TouchEvent&)>;
    using IMUStabilityCallback = std::function<bool()>;
    
    MultitouchEngine();
    MultitouchEngine(i2c_master_bus_handle_t i2c_bus);  // 新增构造函数支持外部I2C总线
    ~MultitouchEngine();
    
    // 初始化引擎
    void Initialize();
    
    // 加载配置
    void LoadConfiguration(const char* config_path = nullptr);
    
    // 注册事件回调
    void RegisterCallback(TouchEventCallback callback);
    
    // 设置IMU稳定性查询回调
    void SetIMUStabilityCallback(IMUStabilityCallback callback);
    
    // 启用/禁用触摸检测
    void Enable(bool enable) { enabled_ = enable; }
    bool IsEnabled() const { return enabled_; }
    
    // 处理函数（由任务调用）
    void Process();
    
    // 获取触摸状态 - 与touch_engine接口保持一致
    bool IsLeftTouched() const { return left_touched_; }
    bool IsRightTouched() const { return right_touched_; }
    
private:
    // MPR121 I2C配置
    static constexpr uint8_t MPR121_I2C_ADDR = 0x5A;
    
    // MPR121寄存器地址
    static constexpr uint8_t MPR121_TOUCHSTATUS_L = 0x00;
    static constexpr uint8_t MPR121_TOUCHSTATUS_H = 0x01;
    static constexpr uint8_t MPR121_FILTDATA_0L = 0x04;
    static constexpr uint8_t MPR121_FILTDATA_0H = 0x05;
    static constexpr uint8_t MPR121_BASELINE_0 = 0x1E;
    static constexpr uint8_t MPR121_MHDR = 0x2B;
    static constexpr uint8_t MPR121_NHDR = 0x2C;
    static constexpr uint8_t MPR121_NCLR = 0x2D;
    static constexpr uint8_t MPR121_FDLR = 0x2E;
    static constexpr uint8_t MPR121_MHDF = 0x2F;
    static constexpr uint8_t MPR121_NHDF = 0x30;
    static constexpr uint8_t MPR121_NCLF = 0x31;
    static constexpr uint8_t MPR121_FDLF = 0x32;
    static constexpr uint8_t MPR121_NHDT = 0x33;
    static constexpr uint8_t MPR121_NCLT = 0x34;
    static constexpr uint8_t MPR121_FDLT = 0x35;
    static constexpr uint8_t MPR121_TOUCHTH_0 = 0x41;
    static constexpr uint8_t MPR121_RELEASETH_0 = 0x42;
    static constexpr uint8_t MPR121_DEBOUNCE = 0x5B;
    static constexpr uint8_t MPR121_CONFIG1 = 0x5C;
    static constexpr uint8_t MPR121_CONFIG2 = 0x5D;
    static constexpr uint8_t MPR121_CHARGECURR_0 = 0x5F;
    static constexpr uint8_t MPR121_CHARGETIME_1 = 0x6C;
    static constexpr uint8_t MPR121_ECR = 0x5E;
    static constexpr uint8_t MPR121_AUTOCONFIG0 = 0x7B;
    static constexpr uint8_t MPR121_AUTOCONFIG1 = 0x7C;
    static constexpr uint8_t MPR121_UPLIMIT = 0x7D;
    static constexpr uint8_t MPR121_LOWLIMIT = 0x7E;
    static constexpr uint8_t MPR121_TARGETLIMIT = 0x7F;
    static constexpr uint8_t MPR121_GPIODIR = 0x76;
    static constexpr uint8_t MPR121_GPIOEN = 0x77;
    static constexpr uint8_t MPR121_GPIOSET = 0x78;
    static constexpr uint8_t MPR121_GPIOCLR = 0x79;
    static constexpr uint8_t MPR121_GPIOTOGGLE = 0x7A;
    
    // 使用的电极通道（可配置）
    static constexpr uint8_t ELECTRODE_LEFT = 0;   // 左侧电极
    static constexpr uint8_t ELECTRODE_RIGHT = 1;  // 右侧电极
    static constexpr uint8_t NUM_ELECTRODES = 2;   // 使用的电极数量
    
    // 配置参数（从配置文件加载）
    TouchDetectionConfig config_;
    
    // 触摸状态
    struct TouchState {
        bool is_touched;
        bool was_touched;
        int64_t touch_start_time;
        int64_t last_change_time;
        bool event_triggered;
    };
    
    // 挠痒检测状态
    struct TickleDetector {
        std::vector<int64_t> touch_times;  // 记录触摸时间戳
        int64_t window_start_time;
        
        TickleDetector() : window_start_time(0) {}
    };
    
    // 状态变量
    bool enabled_;
    bool left_touched_;
    bool right_touched_;
    TouchState left_state_;
    TouchState right_state_;
    
    // MPR121相关
    uint16_t left_baseline_;   // 左侧触摸基准值
    uint16_t right_baseline_;  // 右侧触摸基准值
    uint8_t touch_threshold_;  // 触摸阈值
    uint8_t release_threshold_; // 释放阈值
    
    // 传感器状态检测
    int stuck_detection_count_;
    static const int STUCK_THRESHOLD = 10; // 连续10次检测到卡死状态
    
    // 特殊事件检测
    TickleDetector tickle_detector_;
    int64_t both_touch_start_time_;  // 双侧同时触摸开始时间
    bool cradled_triggered_;
    
    // 任务相关
    TaskHandle_t task_handle_;
    static void TouchTask(void* param);
    
    // 事件回调
    std::vector<TouchEventCallback> callbacks_;
    
    // IMU稳定性查询回调
    IMUStabilityCallback imu_stability_callback_;
    
    // I2C初始化
    void InitializeI2C();
    
    
    // MPR121初始化
    bool InitializeMPR121();
    
    // I2C总线句柄（外部提供）
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t mpr121_device_;
    
    // MPR121寄存器读写
    bool WriteRegister(uint8_t reg, uint8_t value);
    bool ReadRegister(uint8_t reg, uint8_t* value);
    bool ReadRegisters(uint8_t reg, uint8_t* buffer, size_t length);
    
    // 读取基准值
    void ReadBaseline();
    
    // 重置触摸传感器
    void ResetTouchSensor();
    
    // 读取MPR121触摸状态
    bool ReadMPR121TouchStatus(uint16_t* touch_status);
    
    // 获取电极滤波数据
    bool GetElectrodeData(uint8_t electrode, uint16_t* filtered_data, uint16_t* baseline_data);
    
    // 处理触摸状态
    void ProcessSingleTouch(bool currently_touched, TouchPosition position, TouchState& state);
    void ProcessSpecialEvents();  // 处理特殊事件（cradled, tickled）
    bool IsIMUStable();  // 检查IMU是否稳定（用于cradled检测）
    
    // 事件分发
    void DispatchEvent(const TouchEvent& event);
    
};

#endif // ALICHUANGTEST_MULTITOUCH_ENGINE_H