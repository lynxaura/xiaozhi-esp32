#include "multitouch_engine.h"
#include "../config/touch_config.h"
#include "../../i2c_bus_manager.h"
#include <esp_log.h>
#include <esp_timer.h>

#define TAG "MultitouchEngine"

MultitouchEngine::MultitouchEngine() 
    : enabled_(false)
    , left_touched_(false)
    , right_touched_(false)
    , left_baseline_(0)
    , right_baseline_(0)
    , touch_threshold_(12)
    , release_threshold_(6)
    , stuck_detection_count_(0)
    , both_touch_start_time_(0)
    , cradled_triggered_(false)
    , task_handle_(nullptr)
    , i2c_bus_(nullptr)
    , mpr121_device_(nullptr) {
    
    // 初始化触摸状态
    left_state_ = {false, false, 0, 0, false, false};
    right_state_ = {false, false, 0, 0, false, false};
}

MultitouchEngine::MultitouchEngine(i2c_master_bus_handle_t i2c_bus) 
    : enabled_(false)
    , left_touched_(false)
    , right_touched_(false)
    , left_baseline_(0)
    , right_baseline_(0)
    , touch_threshold_(12)
    , release_threshold_(6)
    , stuck_detection_count_(0)
    , both_touch_start_time_(0)
    , cradled_triggered_(false)
    , task_handle_(nullptr)
    , i2c_bus_(i2c_bus)
    , mpr121_device_(nullptr) {
    
    // 初始化触摸状态
    left_state_ = {false, false, 0, 0, false, false};
    right_state_ = {false, false, 0, 0, false, false};
}

MultitouchEngine::~MultitouchEngine() {
    // 停止任务
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    
    // 清理MPR121设备句柄
    if (mpr121_device_) {
        i2c_master_bus_rm_device(mpr121_device_);
        mpr121_device_ = nullptr;
    }
}

void MultitouchEngine::Initialize() {
    ESP_LOGI(TAG, "Initializing MPR121 multitouch engine");
    
    // 1. 加载配置（使用默认路径）
    LoadConfiguration();
    
    // 2. 初始化I2C
    InitializeI2C();
    
    // 3. 初始化MPR121芯片
    if (!InitializeMPR121()) {
        ESP_LOGE(TAG, "MPR121 initialization failed!");
        return;
    }
    
    // 5. 读取基准值
    ReadBaseline();
    
    // 6. 创建触摸处理任务
    BaseType_t task_result = xTaskCreate(TouchTask, "multitouch_task", 3072, this, 10, &task_handle_);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create multitouch task");
        return;
    }
    
    enabled_ = true;
    ESP_LOGI(TAG, "Multitouch engine initialized - MPR121 @ 0x%02X (polling mode)", MPR121_I2C_ADDR);
}

void MultitouchEngine::LoadConfiguration(const char* config_path) {
    // 如果没有指定路径，使用默认路径
    const char* path = config_path ? config_path : "/spiffs/event_config.json";
    
    // 尝试从文件加载配置
    if (!TouchConfigLoader::LoadFromFile(path, config_)) {
        // 如果失败，使用默认配置
        config_ = TouchConfigLoader::LoadDefaults();
    }
    
    ESP_LOGI(TAG, "Touch detection configuration loaded:");
    ESP_LOGI(TAG, "  tap_max: %ldms, hold_min: %ldms, debounce: %ldms",
            config_.tap_max_duration_ms, 
            config_.hold_min_duration_ms,
            config_.debounce_time_ms);
    ESP_LOGI(TAG, "  threshold_ratio: %.1f", config_.touch_threshold_ratio);
}

void MultitouchEngine::InitializeI2C() {
    if (!i2c_bus_) {
        ESP_LOGE(TAG, "I2C bus handle not provided! Please use constructor with i2c_bus parameter");
        return;
    }
    
    // 创建MPR121设备句柄
    i2c_device_config_t mpr121_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPR121_I2C_ADDR,
        .scl_speed_hz = 100000,  // 100kHz
    };
    
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &mpr121_cfg, &mpr121_device_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add MPR121 device to I2C bus: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "MPR121 I2C device configured at address 0x%02X", MPR121_I2C_ADDR);
}


bool MultitouchEngine::InitializeMPR121() {
    ESP_LOGI(TAG, "Initializing MPR121 chip...");
    
    // 软复位 - 停止运行
    if (!WriteRegister(MPR121_ECR, 0x00)) {
        ESP_LOGE(TAG, "Failed to stop MPR121");
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 配置触摸/释放阈值
    for (uint8_t i = 0; i < NUM_ELECTRODES; i++) {
        if (!WriteRegister(MPR121_TOUCHTH_0 + 2*i, touch_threshold_)) {
            ESP_LOGE(TAG, "Failed to set touch threshold for electrode %d", i);
            return false;
        }
        if (!WriteRegister(MPR121_RELEASETH_0 + 2*i, release_threshold_)) {
            ESP_LOGE(TAG, "Failed to set release threshold for electrode %d", i);
            return false;
        }
    }
    
    // 配置滤波器设置
    WriteRegister(MPR121_MHDR, 0x01);
    WriteRegister(MPR121_NHDR, 0x01);
    WriteRegister(MPR121_NCLR, 0x0E);
    WriteRegister(MPR121_FDLR, 0x00);
    WriteRegister(MPR121_MHDF, 0x01);
    WriteRegister(MPR121_NHDF, 0x05);
    WriteRegister(MPR121_NCLF, 0x01);
    WriteRegister(MPR121_FDLF, 0x00);
    WriteRegister(MPR121_NHDT, 0x00);
    WriteRegister(MPR121_NCLT, 0x00);
    WriteRegister(MPR121_FDLT, 0x00);
    
    // 配置消抖
    WriteRegister(MPR121_DEBOUNCE, 0);
    
    // 配置AFE设置
    WriteRegister(MPR121_CONFIG1, 0x10);  // FFI = 00, RETRY = 00, CDC = 16uA
    WriteRegister(MPR121_CONFIG2, 0x20);  // CDT = 0.5us, ESI = 0
    
    // 配置电极充电设置
    for (uint8_t i = 0; i < NUM_ELECTRODES; i++) {
        WriteRegister(MPR121_CHARGECURR_0 + i, 0x20);  // 32uA
    }
    
    // 配置充电时间
    WriteRegister(MPR121_CHARGETIME_1, 0x01);  // 0.5us
    
    // 启用自动配置
    WriteRegister(MPR121_AUTOCONFIG0, 0x0B);
    WriteRegister(MPR121_AUTOCONFIG1, 0x9C);
    WriteRegister(MPR121_UPLIMIT, 200);
    WriteRegister(MPR121_LOWLIMIT, 130);
    WriteRegister(MPR121_TARGETLIMIT, 180);
    
    // 启动MPR121 - 启用前2个电极
    uint8_t ecr = 0x80 | NUM_ELECTRODES;  // CL[1:0] = 00, ELEPROX_EN = 0, ELE_EN[3:0] = NUM_ELECTRODES
    if (!WriteRegister(MPR121_ECR, ecr)) {
        ESP_LOGE(TAG, "Failed to start MPR121");
        return false;
    }
    
    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 验证初始化
    uint8_t config1_val = 0;
    if (!ReadRegister(MPR121_CONFIG1, &config1_val)) {
        ESP_LOGE(TAG, "Failed to verify MPR121 initialization");
        return false;
    }
    
    ESP_LOGI(TAG, "MPR121 initialized successfully (CONFIG1=0x%02X)", config1_val);
    return true;
}

bool MultitouchEngine::WriteRegister(uint8_t reg, uint8_t value) {
    if (!mpr121_device_) {
        ESP_LOGE(TAG, "MPR121 device not initialized");
        return false;
    }
    
    // 获取I2C总线锁
    I2cBusManager::Lock lock(I2cBusManager::GetInstance(), 150);
    if (!lock.IsLocked()) {
        ESP_LOGE(TAG, "Failed to acquire I2C bus lock for write operation");
        return false;
    }
    
    uint8_t data[2] = {reg, value};
    
    // 添加重试机制
    const int max_retries = 3;
    for (int retry = 0; retry < max_retries; retry++) {
        esp_err_t ret = i2c_master_transmit(mpr121_device_, data, sizeof(data), pdMS_TO_TICKS(200));
        
        if (ret == ESP_OK) {
            return true;
        }
        
        if (retry < max_retries - 1) {
            ESP_LOGW(TAG, "I2C write retry %d/%d: reg=0x%02X, error=%s", 
                    retry + 1, max_retries, reg, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));  // 短暂延时后重试
        } else {
            ESP_LOGE(TAG, "I2C write failed after %d retries: reg=0x%02X, value=0x%02X, error=%s", 
                    max_retries, reg, value, esp_err_to_name(ret));
        }
    }
    
    return false;
}

bool MultitouchEngine::ReadRegister(uint8_t reg, uint8_t* value) {
    return ReadRegisters(reg, value, 1);
}

bool MultitouchEngine::ReadRegisters(uint8_t reg, uint8_t* buffer, size_t length) {
    if (!mpr121_device_) {
        ESP_LOGE(TAG, "MPR121 device not initialized");
        return false;
    }
    
    // 获取I2C总线锁
    I2cBusManager::Lock lock(I2cBusManager::GetInstance(), 150);
    if (!lock.IsLocked()) {
        ESP_LOGE(TAG, "Failed to acquire I2C bus lock for read operation");
        return false;
    }
    
    // 添加重试机制
    const int max_retries = 3;
    for (int retry = 0; retry < max_retries; retry++) {
        esp_err_t ret = i2c_master_transmit_receive(mpr121_device_, &reg, 1, buffer, length, pdMS_TO_TICKS(200));
        
        if (ret == ESP_OK) {
            return true;
        }
        
        if (retry < max_retries - 1) {
            ESP_LOGW(TAG, "I2C read retry %d/%d: reg=0x%02X, error=%s", 
                    retry + 1, max_retries, reg, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));  // 短暂延时后重试
        } else {
            ESP_LOGE(TAG, "I2C read failed after %d retries: reg=0x%02X, length=%zu, error=%s", 
                    max_retries, reg, length, esp_err_to_name(ret));
        }
    }
    
    return false;
}

void MultitouchEngine::ReadBaseline() {
    // 等待MPR121稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    
    uint16_t left_data = 0, right_data = 0;
    if (GetElectrodeData(ELECTRODE_LEFT, nullptr, &left_data)) {
        left_baseline_ = left_data;
    }
    if (GetElectrodeData(ELECTRODE_RIGHT, nullptr, &right_data)) {
        right_baseline_ = right_data;
    }
    
    ESP_LOGI(TAG, "MPR121 baselines - Left: %d, Right: %d", left_baseline_, right_baseline_);
}

void MultitouchEngine::ResetTouchSensor() {
    ESP_LOGW(TAG, "========== MPR121 SENSOR RESET START ==========");
    
    // 1. 清除内部状态
    left_state_ = {false, false, 0, 0, false};
    right_state_ = {false, false, 0, 0, false};
    left_touched_ = false;
    right_touched_ = false;
    both_touch_start_time_ = 0;
    cradled_triggered_ = false;
    
    ESP_LOGI(TAG, "Step 1: Internal state cleared");
    
    // 2. 重新初始化MPR121
    if (!InitializeMPR121()) {
        ESP_LOGE(TAG, "MPR121 reset failed!");
        return;
    }
    
    ESP_LOGI(TAG, "Step 2: MPR121 reinitialized");
    
    // 3. 重新读取基线
    vTaskDelay(pdMS_TO_TICKS(200));
    ReadBaseline();
    
    ESP_LOGI(TAG, "========== MPR121 SENSOR RESET COMPLETE ==========");
}

bool MultitouchEngine::ReadMPR121TouchStatus(uint16_t* touch_status) {
    uint8_t buffer[2];
    if (!ReadRegisters(MPR121_TOUCHSTATUS_L, buffer, 2)) {
        return false;
    }
    
    *touch_status = (buffer[1] << 8) | buffer[0];
    return true;
}

bool MultitouchEngine::GetElectrodeData(uint8_t electrode, uint16_t* filtered_data, uint16_t* baseline_data) {
    if (electrode >= NUM_ELECTRODES) {
        return false;
    }
    
    if (filtered_data) {
        uint8_t buffer[2];
        if (!ReadRegisters(MPR121_FILTDATA_0L + electrode * 2, buffer, 2)) {
            return false;
        }
        *filtered_data = (buffer[1] << 8) | buffer[0];
    }
    
    if (baseline_data) {
        uint8_t value;
        if (!ReadRegister(MPR121_BASELINE_0 + electrode, &value)) {
            return false;
        }
        *baseline_data = value << 2;  // MPR121基线值需要左移2位
    }
    
    return true;
}

void MultitouchEngine::RegisterCallback(TouchEventCallback callback) {
    callbacks_.push_back(callback);
}

void MultitouchEngine::SetIMUStabilityCallback(IMUStabilityCallback callback) {
    imu_stability_callback_ = callback;
}

void MultitouchEngine::TouchTask(void* param) {
    MultitouchEngine* engine = static_cast<MultitouchEngine*>(param);
    ESP_LOGI(TAG, "Multitouch task started");
    
    int counter = 0;
    while (true) {
        try {
            if (engine->enabled_) {
                engine->Process();
                
                // 每5秒输出一次任务运行状态
                if (++counter >= 250) {  // 250 * 20ms = 5s
                    ESP_LOGD(TAG, "Multitouch task running - baselines: L=%d, R=%d", 
                            engine->left_baseline_, engine->right_baseline_);
                    counter = 0;
                }
            }
        } catch (...) {
            ESP_LOGE(TAG, "Exception in multitouch task processing!");
            // 继续运行，不要让任务退出
        }
        
        // 50ms轮询间隔，降低I2C总线负载
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void MultitouchEngine::Process() {
    // 读取MPR121触摸状态
    uint16_t touch_status = 0;
    if (!ReadMPR121TouchStatus(&touch_status)) {
        static int read_error_count = 0;
        if (++read_error_count <= 10) {
            ESP_LOGE(TAG, "Failed to read MPR121 touch status (count: %d)", read_error_count);
        }
        
        // 如果连续失败超过10次，触发恢复机制
        if (read_error_count > 10) {
            ESP_LOGE(TAG, "MPR121 persistent failure, triggering recovery...");
            ResetTouchSensor();
            read_error_count = 0;
        }
        return;
    }
    
    // 检测触摸状态
    bool left_touched = (touch_status & (1 << ELECTRODE_LEFT)) != 0;
    bool right_touched = (touch_status & (1 << ELECTRODE_RIGHT)) != 0;
    
    // 调试输出
    static int debug_counter = 0;
    static bool last_left_touched = false;
    static bool last_right_touched = false;
    
    if (++debug_counter >= 100) {  // 100 * 20ms = 2s
        ESP_LOGD(TAG, "Touch status: 0x%04X, Left: %s, Right: %s", 
                touch_status,
                left_touched ? "TOUCHED" : "free",
                right_touched ? "TOUCHED" : "free");
        debug_counter = 0;
    }
    
    // 只在状态改变时输出详细日志
    if (left_touched != last_left_touched) {
        ESP_LOGI(TAG, "Left touch %s", left_touched ? "DETECTED" : "RELEASED");
        last_left_touched = left_touched;
    }
    
    if (right_touched != last_right_touched) {
        ESP_LOGI(TAG, "Right touch %s", right_touched ? "DETECTED" : "RELEASED");
        last_right_touched = right_touched;
    }
    
    // 处理单侧触摸事件
    ProcessSingleTouch(left_touched, TouchPosition::LEFT, left_state_);
    ProcessSingleTouch(right_touched, TouchPosition::RIGHT, right_state_);
    
    // 更新全局状态
    left_touched_ = left_touched;
    right_touched_ = right_touched;
    
    // 处理待定的长按事件（检查是否应该触发单侧长按或转为拥抱检测）
    ProcessPendingHoldEvents();
    
    // 处理特殊事件（cradled, tickled）
    ProcessSpecialEvents();
}

void MultitouchEngine::ProcessSingleTouch(bool currently_touched, TouchPosition position, TouchState& state) {
    int64_t current_time = esp_timer_get_time();
    
    // 消抖处理
    if (currently_touched != state.was_touched) {
        if ((current_time - state.last_change_time) < (config_.debounce_time_ms * 1000)) {
            return;  // 忽略抖动
        }
        state.last_change_time = current_time;
    }
    
    // 状态转换处理
    if (currently_touched && !state.is_touched) {
        // 按下事件
        ESP_LOGI(TAG, "Touch PRESSED on %s", 
                position == TouchPosition::LEFT ? "LEFT" : "RIGHT");
        
        state.is_touched = true;
        state.touch_start_time = current_time;
        state.event_triggered = false;
        
        // 记录触摸时间用于tickled检测
        tickle_detector_.touch_times.push_back(current_time);
        
    } else if (state.is_touched && currently_touched) {
        // 持续按住状态 - 检查是否应该设为长按待定状态
        uint32_t duration_ms = (current_time - state.touch_start_time) / 1000;
        
        // 如果超过长按阈值且还没有触发过事件
        if (!state.event_triggered && !state.hold_event_pending && duration_ms >= config_.hold_min_duration_ms) {
            // 设为长按待定状态，不立即触发
            state.hold_event_pending = true;
            
            ESP_LOGI(TAG, "HOLD event pending on %s (duration: %ld ms)", 
                    position == TouchPosition::LEFT ? "LEFT" : "RIGHT", duration_ms);
        }
        
    } else if (state.is_touched && !currently_touched) {
        // 释放事件
        uint32_t duration_ms = (current_time - state.touch_start_time) / 1000;
        
        ESP_LOGI(TAG, "Touch RELEASED on %s: duration=%ldms, triggered=%d, pending=%d, TAP_MAX=%ld", 
                position == TouchPosition::LEFT ? "LEFT" : "RIGHT",
                duration_ms, state.event_triggered, state.hold_event_pending, config_.tap_max_duration_ms);
        
        // 如果有待定的长按事件但没有被处理，现在释放了就不处理了
        if (state.hold_event_pending) {
            ESP_LOGI(TAG, "Cancelling pending hold event due to release on %s", 
                    position == TouchPosition::LEFT ? "LEFT" : "RIGHT");
        }
        
        if (!state.event_triggered && !state.hold_event_pending && duration_ms < config_.tap_max_duration_ms) {
            // 触发单击事件（只有在没有触发长按且时间短于TAP_MAX时）
            TouchEvent event;
            event.type = TouchEventType::SINGLE_TAP;
            event.position = position;
            event.timestamp_us = current_time;
            event.duration_ms = duration_ms;
            
            ESP_LOGI(TAG, "Creating SINGLE_TAP event: type=%d, position=%d, duration=%ld ms", 
                    (int)event.type, (int)event.position, event.duration_ms);
            
            DispatchEvent(event);
            
            ESP_LOGI(TAG, "SINGLE_TAP on %s dispatched (duration: %ld ms)", 
                    position == TouchPosition::LEFT ? "LEFT" : "RIGHT", duration_ms);
        }
        
        state.is_touched = false;
        state.event_triggered = false;
        state.hold_event_pending = false;
    }
    
    state.was_touched = currently_touched;
}

void MultitouchEngine::ProcessPendingHoldEvents() {
    int64_t current_time = esp_timer_get_time();
    
    // 检查是否双侧都有待定的长按事件
    if (left_state_.hold_event_pending && right_state_.hold_event_pending) {
        // 双侧同时长按，优先处理为拥抱事件的准备
        ESP_LOGI(TAG, "Both sides have pending hold events - preparing for cradle detection");
        
        // 清除单侧长按待定状态，让ProcessSpecialEvents处理拥抱逻辑
        left_state_.hold_event_pending = false;
        right_state_.hold_event_pending = false;
        left_state_.event_triggered = true;  // 标记已处理，避免后续单独触发
        right_state_.event_triggered = true;
        
        return;
    }
    
    // 添加短暂延迟（例如200ms），给另一侧触摸一个机会
    const uint32_t hold_delay_ms = 200;
    
    // 检查左侧待定长按事件
    if (left_state_.hold_event_pending) {
        uint32_t duration_ms = (current_time - left_state_.touch_start_time) / 1000;
        
        // 如果延迟时间已过且右侧没有触摸，则触发单侧长按
        if (duration_ms >= config_.hold_min_duration_ms + hold_delay_ms && !right_touched_) {
            TouchEvent event;
            event.type = TouchEventType::HOLD;
            event.position = TouchPosition::LEFT;
            event.timestamp_us = current_time;
            event.duration_ms = duration_ms - hold_delay_ms;  // 减去延迟时间
            
            ESP_LOGI(TAG, "Creating delayed LEFT HOLD event: duration=%ld ms", event.duration_ms);
            DispatchEvent(event);
            
            left_state_.event_triggered = true;
            left_state_.hold_event_pending = false;
        }
    }
    
    // 检查右侧待定长按事件
    if (right_state_.hold_event_pending) {
        uint32_t duration_ms = (current_time - right_state_.touch_start_time) / 1000;
        
        // 如果延迟时间已过且左侧没有触摸，则触发单侧长按
        if (duration_ms >= config_.hold_min_duration_ms + hold_delay_ms && !left_touched_) {
            TouchEvent event;
            event.type = TouchEventType::HOLD;
            event.position = TouchPosition::RIGHT;
            event.timestamp_us = current_time;
            event.duration_ms = duration_ms - hold_delay_ms;  // 减去延迟时间
            
            ESP_LOGI(TAG, "Creating delayed RIGHT HOLD event: duration=%ld ms", event.duration_ms);
            DispatchEvent(event);
            
            right_state_.event_triggered = true;
            right_state_.hold_event_pending = false;
        }
    }
}

void MultitouchEngine::ProcessSpecialEvents() {
    int64_t current_time = esp_timer_get_time();
    
    // 1. 检测cradled事件（双侧持续触摸>2秒且IMU静止）
    if (left_touched_ && right_touched_) {
        if (both_touch_start_time_ == 0) {
            both_touch_start_time_ = current_time;
            cradled_triggered_ = false;
        } else {
            uint32_t duration_ms = (current_time - both_touch_start_time_) / 1000;
            if (!cradled_triggered_ && duration_ms >= config_.cradled_min_duration_ms) {
                // 检查IMU是否稳定
                if (IsIMUStable()) {
                    cradled_triggered_ = true;
                    
                    TouchEvent event;
                    event.type = TouchEventType::CRADLED;
                    event.position = TouchPosition::BOTH;
                    event.timestamp_us = current_time;
                    event.duration_ms = duration_ms;
                    
                    DispatchEvent(event);
                    ESP_LOGI(TAG, "CRADLED detected (both sides held for %ld ms with stable IMU)", duration_ms);
                }
            }
        }
    } else {
        // 双侧触摸结束，重置cradled状态
        if (both_touch_start_time_ != 0 || cradled_triggered_) {
            ESP_LOGD(TAG, "Both touch ended - resetting cradled state (was_triggered=%d)", cradled_triggered_);
        }
        both_touch_start_time_ = 0;
        cradled_triggered_ = false;
    }
    
    // 2. 检测tickled事件（2秒内多次无规律触摸>4次）
    // 清理过时的触摸记录
    auto& times = tickle_detector_.touch_times;
    times.erase(
        std::remove_if(times.begin(), times.end(),
            [current_time, this](int64_t t) { 
                return (current_time - t) > (config_.tickled_window_ms * 1000); 
            }),
        times.end()
    );
    
    // 检查是否达到tickled条件
    if (times.size() >= config_.tickled_min_touches) {
        TouchEvent event;
        event.type = TouchEventType::TICKLED;
        event.position = TouchPosition::ANY;
        event.timestamp_us = current_time;
        event.duration_ms = 0;
        
        DispatchEvent(event);
        ESP_LOGI(TAG, "TICKLED detected (%zu touches in 2 seconds)", times.size());
        
        // 清空记录，避免重复触发
        times.clear();
    }
}

bool MultitouchEngine::IsIMUStable() {
    if (imu_stability_callback_) {
        bool is_stable = imu_stability_callback_();
        ESP_LOGD(TAG, "IMU stability check: %s", is_stable ? "STABLE" : "UNSTABLE");
        return is_stable;
    }
    
    // 没有回调时，保守地返回false（认为不稳定）
    ESP_LOGW(TAG, "No IMU stability callback set, assuming unstable");
    return false;
}

void MultitouchEngine::DispatchEvent(const TouchEvent& event) {
    ESP_LOGI(TAG, "Dispatching TouchEvent: type=%d, position=%d, callbacks=%zu", 
            (int)event.type, (int)event.position, callbacks_.size());
    
    for (size_t i = 0; i < callbacks_.size(); ++i) {
        if (callbacks_[i]) {
            ESP_LOGD(TAG, "Calling callback %zu with event type=%d", i, (int)event.type);
            try {
                callbacks_[i](event);
                ESP_LOGD(TAG, "Callback %zu completed", i);
            } catch (...) {
                ESP_LOGE(TAG, "Exception in callback %zu for event type=%d", i, (int)event.type);
            }
        }
    }
    ESP_LOGD(TAG, "Event dispatch completed for type=%d", (int)event.type);
}

