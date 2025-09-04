#include "motion_engine.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cmath>
#include <algorithm>
#include <cJSON.h>

#define TAG "MotionEngine"

MotionEngine::MotionEngine() 
    : imu_(nullptr)
    , enabled_(false)
    , first_reading_(true)
    , last_debug_time_us_(0)
    , debug_output_(false)
    , free_fall_start_time_(0)
    , in_free_fall_(false)
    , is_upside_down_(false)
    , upside_down_count_(0)
    , is_picked_up_(false)
    , stable_count_(0)
    , stable_z_reference_(1.0f)
    , pickup_start_time_(0)
    , last_significant_motion_time_(0)
    , consecutive_stable_readings_(0) {
}

MotionEngine::~MotionEngine() {
}

void MotionEngine::Initialize(Qmi8658* imu) {
    imu_ = imu;
    if (imu_) {
        enabled_ = true;
        ESP_LOGI(TAG, "Motion engine initialized with IMU support");
    } else {
        ESP_LOGW(TAG, "Motion engine initialized without IMU");
    }
}

void MotionEngine::RegisterCallback(MotionEventCallback callback) {
    callbacks_.push_back(callback);
}

void MotionEngine::Process() {
    if (enabled_ && imu_) {
        ProcessMotionDetection();
    }
}

void MotionEngine::ProcessMotionDetection() {
    // 读取IMU数据和角度
    if (imu_->ReadDataWithAngles(current_imu_data_) != ESP_OK) {
        return;
    }
    
    // 第一次读取，初始化last_data_
    if (first_reading_) {
        last_imu_data_ = current_imu_data_;
        first_reading_ = false;
        return;
    }
    
    int64_t current_time = esp_timer_get_time();
    
    // 检测各种运动事件
    MotionEventType motion_type = MotionEventType::NONE;
    
    // 按优先级检测运动事件（优先级从高到低）
    // 1. 自由落体（最危险，需要立即检测）
    if (DetectFreeFall(current_imu_data_, current_time) &&
        (current_time - last_event_times_[MotionEventType::FREE_FALL] > FREE_FALL_COOLDOWN_US)) {
        motion_type = MotionEventType::FREE_FALL;
        ESP_LOGW(TAG, "Motion detected: FREE_FALL! Duration: %lld ms | Magnitude: %.3f g", 
                (current_time - free_fall_start_time_) / 1000, CalculateAccelMagnitude(current_imu_data_));
    } 
    // 2. 剧烈摇晃（可能损坏设备）
    else if (DetectShakeViolently(current_imu_data_) &&
             (current_time - last_event_times_[MotionEventType::SHAKE_VIOLENTLY] > SHAKE_VIOLENTLY_COOLDOWN_US)) {
        motion_type = MotionEventType::SHAKE_VIOLENTLY;
        float accel_delta = CalculateAccelDelta(current_imu_data_, last_imu_data_);
        ESP_LOGW(TAG, "Motion detected: SHAKE_VIOLENTLY! AccelDelta: %.2f g", accel_delta);
    } 
    // 3. 翻转（快速旋转）
    else if (DetectFlip(current_imu_data_) &&
             (current_time - last_event_times_[MotionEventType::FLIP] > FLIP_COOLDOWN_US)) {
        motion_type = MotionEventType::FLIP;
        float gyro_mag = std::sqrt(current_imu_data_.gyro_x * current_imu_data_.gyro_x + 
                                  current_imu_data_.gyro_y * current_imu_data_.gyro_y + 
                                  current_imu_data_.gyro_z * current_imu_data_.gyro_z);
        ESP_LOGI(TAG, "Motion detected: FLIP | Gyro: %.1f deg/s (X:%.1f Y:%.1f Z:%.1f)", 
                gyro_mag, current_imu_data_.gyro_x, current_imu_data_.gyro_y, current_imu_data_.gyro_z);
    } 
    // 4. 普通摇晃
    else if (DetectShake(current_imu_data_) &&
             (current_time - last_event_times_[MotionEventType::SHAKE] > SHAKE_COOLDOWN_US)) {
        motion_type = MotionEventType::SHAKE;
        float accel_delta = CalculateAccelDelta(current_imu_data_, last_imu_data_);
        ESP_LOGI(TAG, "Motion detected: SHAKE | AccelDelta: %.2f g", accel_delta);
    } 
    // 5. 拿起
    else if (DetectPickup(current_imu_data_) &&
             (current_time - last_event_times_[MotionEventType::PICKUP] > PICKUP_COOLDOWN_US)) {
        motion_type = MotionEventType::PICKUP;
        float z_diff = current_imu_data_.accel_z - last_imu_data_.accel_z;
        ESP_LOGI(TAG, "Motion detected: PICKUP | Z-diff: %.3f g, Current Z: %.2f g (State: picked up)", 
                z_diff, current_imu_data_.accel_z);
    }
    // 6. 倒置状态（持续状态检测）
    else if (DetectUpsideDown(current_imu_data_) &&
             (current_time - last_event_times_[MotionEventType::UPSIDE_DOWN] > UPSIDE_DOWN_COOLDOWN_US)) {
        motion_type = MotionEventType::UPSIDE_DOWN;
        ESP_LOGI(TAG, "Motion detected: UPSIDE_DOWN | Z-axis: %.2f g, Count: %d", 
                current_imu_data_.accel_z, upside_down_count_);
    }
    
    if (motion_type != MotionEventType::NONE) {
        // 更新该事件类型的时间戳
        last_event_times_[motion_type] = current_time;
        
        // 记录显著运动事件的时间（除了PICKUP，因为PICKUP应该在相对稳定时触发）
        if (motion_type != MotionEventType::PICKUP) {
            last_significant_motion_time_ = current_time;
            
            // 根据运动类型决定是否重置稳定读数计数
            if (motion_type == MotionEventType::FREE_FALL || 
                motion_type == MotionEventType::SHAKE_VIOLENTLY || 
                motion_type == MotionEventType::FLIP) {
                // 剧烈运动完全重置
                consecutive_stable_readings_ = 0;
                ESP_LOGD(TAG, "Violent motion detected: %d, resetting stability counter", static_cast<int>(motion_type));
            } else {
                // 轻微运动只部分重置
                consecutive_stable_readings_ = std::max(0, consecutive_stable_readings_ - 2);
                ESP_LOGD(TAG, "Mild motion detected: %d, reducing stability counter to %d", 
                        static_cast<int>(motion_type), consecutive_stable_readings_);
            }
        }
        
        // 创建事件并分发
        MotionEvent event;
        event.type = motion_type;
        event.timestamp_us = current_time;
        event.imu_data = current_imu_data_;
        
        DispatchEvent(event);
    }
    
    // 更新稳定性计数器 - 使用简化的稳定性判断
    float accel_delta_simple = CalculateAccelDelta(current_imu_data_, last_imu_data_);
    if (accel_delta_simple < 0.2f) { // 使用与pickup检测相同的标准
        consecutive_stable_readings_++;
    } else {
        consecutive_stable_readings_ = 0;
    }
    
    last_imu_data_ = current_imu_data_;
}

void MotionEngine::DispatchEvent(const MotionEvent& event) {
    for (const auto& callback : callbacks_) {
        if (callback) {
            callback(event);
        }
    }
}

float MotionEngine::CalculateAccelMagnitude(const ImuData& data) const {
    return std::sqrt(data.accel_x * data.accel_x + 
                    data.accel_y * data.accel_y + 
                    data.accel_z * data.accel_z);
}

float MotionEngine::CalculateAccelDelta(const ImuData& current, const ImuData& last) const {
    float dx = current.accel_x - last.accel_x;
    float dy = current.accel_y - last.accel_y;
    float dz = current.accel_z - last.accel_z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool MotionEngine::DetectPickup(const ImuData& data) {
    // 拿起检测
    
    int64_t current_time = esp_timer_get_time();
    
    // 计算运动参数
    float z_diff = data.accel_z - last_imu_data_.accel_z;
    float current_magnitude = CalculateAccelMagnitude(data);
    float accel_delta = CalculateAccelDelta(data, last_imu_data_);
    
    // 稳定性检查
    bool is_relatively_stable = accel_delta < 0.2f;
    
    if (!is_picked_up_) {
        // 当前未拿起状态，检测是否被拿起
        
        // 1. 运动事件冲突检查
        int64_t time_since_motion = current_time - last_significant_motion_time_;
        bool recent_violent_motion = time_since_motion < 800000; // 800ms内有剧烈运动
        bool recent_mild_motion = time_since_motion < 300000;   // 300ms内有轻微运动
        
        if (recent_violent_motion) {
            if (debug_output_) {
                ESP_LOGV(TAG, "Pickup blocked - recent violent motion (%.1fs ago)", 
                        time_since_motion / 1000000.0f);
            }
            stable_count_ = 0;
            return false;
        }
        
        // 2. 基本稳定性检查
        if (!recent_mild_motion && consecutive_stable_readings_ < 3) {
            if (debug_output_) {
                ESP_LOGV(TAG, "Pickup blocked - insufficient stable readings (%d < 3)", 
                        consecutive_stable_readings_);
            }
            return false;
        }
        
        // 3. 关键改进：检测触碰桌面的反冲模式
        // 触碰桌面的特征：
        // - 上一帧Z轴加速度较小（下降过程中）
        // - 当前帧突然增大（反冲）
        // - 总加速度变化很大（突然停止）
        // - 设备位置接近水平（Z轴绝对值接近1g）
        
        bool previous_low_z = last_imu_data_.accel_z < 0.9f;  // 上一帧Z轴较低
        bool current_near_1g = std::abs(data.accel_z) > 0.8f && std::abs(data.accel_z) < 1.2f; // 当前接近1g
        bool sudden_large_change = accel_delta > 0.8f;  // 突然的大幅变化
        bool impact_pattern = previous_low_z && current_near_1g && sudden_large_change; // 撞击模式
        
        // 如果检测到撞击模式，直接排除
        if (impact_pattern) {
            if (debug_output_) {
                ESP_LOGV(TAG, "Pickup blocked - impact pattern detected (prev_z:%.3f, curr_z:%.3f, delta:%.3f)", 
                        last_imu_data_.accel_z, data.accel_z, accel_delta);
            }
            stable_count_ = 0;
            return false;
        }
        
        // 4. 进一步检查：排除反冲后的短暂向上运动
        // 检查设备是否处于接近水平静止状态（刚放下的状态）
        bool device_horizontal = std::abs(data.accel_z) > 0.85f && std::abs(data.accel_z) < 1.15f;
        bool small_xy_movement = std::sqrt(data.accel_x * data.accel_x + data.accel_y * data.accel_y) < 0.5f;
        bool likely_on_surface = device_horizontal && small_xy_movement;
        
        // 如果设备可能在表面上且有向上运动，需要更谨慎
        if (likely_on_surface && z_diff > 0) {
            // 检查这是否是真正的拿起（需要持续的向上运动和姿态改变）
            if (z_diff < config_.pickup_threshold_g * 2.0f) { // 如果向上运动不够明显
                if (debug_output_) {
                    ESP_LOGV(TAG, "Pickup blocked - weak upward motion on surface (z_diff:%.3f)", z_diff);
                }
                stable_count_ = 0;
                return false;
            }
        }
        
        // 更新稳定时的Z轴参考值
        if (is_relatively_stable) {
            stable_z_reference_ = data.accel_z;
            stable_count_++;
        } else {
            stable_count_ = 0;
        }
        
        // 5. 检测拿起动作 - 要求更明确的条件
        bool clear_upward_motion = z_diff > config_.pickup_threshold_g;
        bool gradual_upward_motion = (z_diff > config_.pickup_threshold_g) && 
                                    is_relatively_stable && 
                                    !likely_on_surface; // 不在表面上的渐进运动
        bool magnitude_change = std::abs(current_magnitude - CalculateAccelMagnitude(last_imu_data_)) > config_.pickup_threshold_g;
        bool attitude_change = std::abs(data.accel_z - stable_z_reference_) > 0.4f; // 提高姿态变化阈值
        
        // 排除向下运动
        bool downward_motion = z_diff < -config_.pickup_threshold_g;
        
        // 6. 综合判断拿起条件
        bool pickup_detected = false;
        std::string detection_reason;
        
        // 模式1：明显向上运动且不是撞击反冲
        if (clear_upward_motion && !downward_motion && !impact_pattern) {
            pickup_detected = true;
            detection_reason = "clear_upward";
        } 
        // 模式2：渐进向上运动 + 姿态变化 + 不在表面
        else if (gradual_upward_motion && attitude_change && !downward_motion) {
            pickup_detected = true;
            detection_reason = "gradual_upward+attitude";
        } 
        // 模式3：幅值变化 + 姿态变化 + 稳定状态 + 不是撞击
        else if (magnitude_change && attitude_change && !downward_motion && 
                is_relatively_stable && !impact_pattern) {
            pickup_detected = true;
            detection_reason = "magnitude+attitude";
        }
        
        if (pickup_detected) {
            is_picked_up_ = true;
            stable_count_ = 0;
            pickup_start_time_ = current_time;
            
            if (debug_output_) {
                ESP_LOGI(TAG, "Pickup detected: %s | Z_diff:%.3f Current_Z:%.3f", 
                        detection_reason.c_str(), z_diff, data.accel_z);
            }
            return true;
        } else {
            if (debug_output_ && (clear_upward_motion || gradual_upward_motion || magnitude_change)) {
                ESP_LOGV(TAG, "Pickup candidate rejected - %s Z_diff:%.3f Mag:%.3f Delta:%.3f OnSurf:%d Impact:%d", 
                        detection_reason.c_str(), z_diff, current_magnitude, accel_delta, 
                        likely_on_surface, impact_pattern);
            }
        }
        
    } else {
        // 当前已拿起状态，检测是否被放下
        
        int64_t pickup_duration = current_time - pickup_start_time_;
        bool timeout_mode = pickup_duration > 8000000; // 8秒后更容易放下
        
        if (is_relatively_stable) {
            stable_count_++;
            
            int required_stable_count = timeout_mode ? 5 : 10; 
            if (stable_count_ >= required_stable_count) {
                // 检查Z轴是否回到水平位置附近
                if (std::abs(data.accel_z) > 0.7f && std::abs(data.accel_z) < 1.3f) {
                    is_picked_up_ = false;
                    stable_count_ = 0;
                    consecutive_stable_readings_ = 0;
                    
                    if (debug_output_) {
                        ESP_LOGI(TAG, "Device put down - Z:%.3f stable for %d frames", 
                                data.accel_z, stable_count_);
                    }
                }
            }
        } else {
            stable_count_ = 0;
        }
        
        // 超时后检测明显向下运动
        if (timeout_mode && z_diff < -0.3f && current_magnitude < 1.4f) {
            is_picked_up_ = false;
            stable_count_ = 0;
            consecutive_stable_readings_ = 0;
            if (debug_output_) {
                ESP_LOGI(TAG, "Device put down - Detected downward motion after timeout");
            }
        }
        
        return false;
    }
    
    return false;
}

bool MotionEngine::DetectUpsideDown(const ImuData& data) {
    // 倒置检测：Z轴持续接近-1g（设备倒置）
    // 需要稳定且不在剧烈运动中
    
    float accel_delta = CalculateAccelDelta(data, last_imu_data_);
    bool is_stable = accel_delta < 0.5f;  // 相对稳定，没有剧烈运动
    
    // 检查Z轴是否接近-1g（倒置）
    bool z_axis_inverted = data.accel_z < config_.upside_down_threshold_g;
    
    if (z_axis_inverted && is_stable) {
        upside_down_count_++;
        
        // 需要持续一定帧数才判定为倒置
        if (!is_upside_down_ && upside_down_count_ >= config_.upside_down_stable_count) {
            is_upside_down_ = true;
            ESP_LOGD(TAG, "Device is now upside down: Z=%.2f g", data.accel_z);
            return true;
        }
    } else {
        // 不再倒置
        if (is_upside_down_ && !z_axis_inverted) {
            ESP_LOGD(TAG, "Device is no longer upside down: Z=%.2f g", data.accel_z);
            is_upside_down_ = false;
        }
        upside_down_count_ = 0;
    }
    
    return false;
}

bool MotionEngine::DetectShake(const ImuData& data) {
    // 检测快速来回运动
    float accel_delta = CalculateAccelDelta(data, last_imu_data_);
    return accel_delta > config_.shake_normal_threshold_g;
}

bool MotionEngine::DetectFreeFall(const ImuData& data, int64_t current_time) {
    // 自由落体检测：总加速度接近0g，持续超过200ms
    float magnitude = CalculateAccelMagnitude(data);
    
    // 检测是否处于自由落体状态（总加速度小于阈值）
    bool is_falling = magnitude < config_.free_fall_threshold_g;
    
    if (is_falling) {
        if (!in_free_fall_) {
            // 刚开始自由落体
            in_free_fall_ = true;
            free_fall_start_time_ = current_time;
            ESP_LOGD(TAG, "Free fall started: magnitude=%.3f g", magnitude);
        } else {
            // 检查是否持续足够长时间
            int64_t fall_duration = current_time - free_fall_start_time_;
            if (fall_duration >= config_.free_fall_min_duration_ms * 1000) {
                ESP_LOGD(TAG, "Free fall confirmed: duration=%lld ms, magnitude=%.3f g", 
                        fall_duration / 1000, magnitude);
                return true;
            }
        }
    } else {
        if (in_free_fall_) {
            // 自由落体结束
            int64_t fall_duration = current_time - free_fall_start_time_;
            ESP_LOGD(TAG, "Free fall ended: duration=%lld ms", fall_duration / 1000);
            in_free_fall_ = false;
        }
    }
    
    return false;
}

bool MotionEngine::DetectShakeViolently(const ImuData& data) {
    // 剧烈摇晃检测：加速度变化超过3g
    float accel_delta = CalculateAccelDelta(data, last_imu_data_);
    
    // 同时检查陀螺仪是否有剧烈旋转
    float gyro_magnitude = std::sqrt(data.gyro_x * data.gyro_x + 
                                   data.gyro_y * data.gyro_y + 
                                   data.gyro_z * data.gyro_z);
    
    // 剧烈摇晃：大幅度加速度变化 或 高速旋转伴随加速度变化
    bool violent_shake = (accel_delta > config_.shake_violently_threshold_g) || 
                        (accel_delta > 2.0f && gyro_magnitude > 300.0f);
    
    if (violent_shake) {
        ESP_LOGD(TAG, "Violent shake: accel_delta=%.2f g, gyro=%.1f deg/s", 
                accel_delta, gyro_magnitude);
    }
    
    return violent_shake;
}

bool MotionEngine::DetectFlip(const ImuData& data) {
    // 改进的翻转检测：需要持续高速旋转，避免误触发
    float gyro_magnitude = std::sqrt(data.gyro_x * data.gyro_x + 
                                   data.gyro_y * data.gyro_y + 
                                   data.gyro_z * data.gyro_z);
    
    // 检查是否有主导轴的高速旋转（真正的翻转通常在某个轴上）
    float max_single_axis = std::max({std::abs(data.gyro_x), 
                                      std::abs(data.gyro_y), 
                                      std::abs(data.gyro_z)});
    
    // 需要同时满足：
    // 1. 总旋转速度超过阈值
    // 2. 至少有一个轴的旋转速度超过阈值的70%（避免轻微晃动的累加效应）
    bool high_rotation = gyro_magnitude > config_.flip_threshold_deg_s;
    bool dominant_axis = max_single_axis > (config_.flip_threshold_deg_s * 0.7f);
    
    // 3. 加速度也要有明显变化（真正翻转时会有）
    float accel_change = CalculateAccelDelta(data, last_imu_data_);
    bool accel_detected = accel_change > 0.5f;  // 至少0.5g的加速度变化
    
    bool flip_detected = high_rotation && dominant_axis && accel_detected;
    
    if (flip_detected && debug_output_) {
        ESP_LOGD(TAG, "Flip details - Gyro:%.1f MaxAxis:%.1f AccelDelta:%.2f", 
                gyro_magnitude, max_single_axis, accel_change);
    }
    
    return flip_detected;
}

bool MotionEngine::IsStable(const ImuData& data, const ImuData& last_data) const {
    // 改进的稳定性判断：同时检查加速度和陀螺仪数据
    
    // 1. 计算加速度变化量
    float accel_delta = CalculateAccelDelta(data, last_data);
    
    // 2. 计算陀螺仪变化量（角速度）
    float gyro_magnitude = std::sqrt(data.gyro_x * data.gyro_x + 
                                   data.gyro_y * data.gyro_y + 
                                   data.gyro_z * data.gyro_z);
    
    // 3. 检查总加速度是否接近1g（表示静止状态）
    float accel_magnitude = CalculateAccelMagnitude(data);
    bool near_1g = std::abs(accel_magnitude - 1.0f) < 0.3f; // 允许±0.3g的误差
    
    // 4. 综合判断稳定性
    bool accel_stable = accel_delta < 0.1f;           // 加速度变化小
    bool gyro_stable = gyro_magnitude < 30.0f;       // 角速度小于30°/s
    bool magnitude_stable = near_1g;                  // 总加速度接近重力加速度
    
    bool is_stable = accel_stable && gyro_stable && magnitude_stable;
    
    if (debug_output_ && !is_stable) {
        ESP_LOGV(TAG, "Stability check: AccelDelta=%.3f Gyro=%.1f Mag=%.2f Near1g=%d -> %s",
                accel_delta, gyro_magnitude, accel_magnitude, near_1g, 
                is_stable ? "STABLE" : "UNSTABLE");
    }
    
    return is_stable;
}

bool MotionEngine::IsCurrentlyStable() const {
    // 检查是否有有效的IMU数据
    if (!enabled_ || !imu_) {
        return false;  // 没有IMU或未启用时认为不稳定
    }
    
    // 使用当前和上一次的IMU数据进行稳定性检查
    return IsStable(current_imu_data_, last_imu_data_);
}

void MotionEngine::UpdateConfigFromJson(const cJSON* json) {
    if (!json) return;
    
    // 查找 motion_detection_parameters 节点
    const cJSON* motion_params = cJSON_GetObjectItem(json, "motion_detection_parameters");
    if (!motion_params) {
        ESP_LOGW(TAG, "No motion_detection_parameters found in config");
        return;
    }
    
    // 自由落体参数
    const cJSON* free_fall = cJSON_GetObjectItem(motion_params, "free_fall");
    if (free_fall) {
        const cJSON* item = cJSON_GetObjectItem(free_fall, "threshold_g");
        if (item && cJSON_IsNumber(item)) {
            config_.free_fall_threshold_g = item->valuedouble;
        }
        item = cJSON_GetObjectItem(free_fall, "min_duration_ms");
        if (item && cJSON_IsNumber(item)) {
            config_.free_fall_min_duration_ms = item->valueint;
        }
    }
    
    // 摇晃参数
    const cJSON* shake = cJSON_GetObjectItem(motion_params, "shake");
    if (shake) {
        const cJSON* item = cJSON_GetObjectItem(shake, "normal_threshold_g");
        if (item && cJSON_IsNumber(item)) {
            config_.shake_normal_threshold_g = item->valuedouble;
        }
        item = cJSON_GetObjectItem(shake, "violently_threshold_g");
        if (item && cJSON_IsNumber(item)) {
            config_.shake_violently_threshold_g = item->valuedouble;
        }
    }
    
    // 翻转参数
    const cJSON* flip = cJSON_GetObjectItem(motion_params, "flip");
    if (flip) {
        const cJSON* item = cJSON_GetObjectItem(flip, "threshold_deg_s");
        if (item && cJSON_IsNumber(item)) {
            config_.flip_threshold_deg_s = item->valuedouble;
        }
    }
    
    // 拿起参数
    const cJSON* pickup = cJSON_GetObjectItem(motion_params, "pickup");
    if (pickup) {
        const cJSON* item = cJSON_GetObjectItem(pickup, "threshold_g");
        if (item && cJSON_IsNumber(item)) {
            config_.pickup_threshold_g = item->valuedouble;
        }
        item = cJSON_GetObjectItem(pickup, "stable_threshold_g");
        if (item && cJSON_IsNumber(item)) {
            config_.pickup_stable_threshold_g = item->valuedouble;
        }
        item = cJSON_GetObjectItem(pickup, "stable_count");
        if (item && cJSON_IsNumber(item)) {
            config_.pickup_stable_count = item->valueint;
        }
        item = cJSON_GetObjectItem(pickup, "min_duration_ms");
        if (item && cJSON_IsNumber(item)) {
            config_.pickup_min_duration_ms = item->valueint;
        }
    }
    
    // 倒置参数
    const cJSON* upside_down = cJSON_GetObjectItem(motion_params, "upside_down");
    if (upside_down) {
        const cJSON* item = cJSON_GetObjectItem(upside_down, "threshold_g");
        if (item && cJSON_IsNumber(item)) {
            config_.upside_down_threshold_g = item->valuedouble;
        }
        item = cJSON_GetObjectItem(upside_down, "stable_count");
        if (item && cJSON_IsNumber(item)) {
            config_.upside_down_stable_count = item->valueint;
        }
    }
    
    // 调试参数
    const cJSON* debug = cJSON_GetObjectItem(motion_params, "debug");
    if (debug) {
        const cJSON* item = cJSON_GetObjectItem(debug, "interval_ms");
        if (item && cJSON_IsNumber(item)) {
            config_.debug_interval_ms = item->valueint;
        }
        item = cJSON_GetObjectItem(debug, "enabled");
        if (item && cJSON_IsBool(item)) {
            config_.debug_enabled = cJSON_IsTrue(item);
            debug_output_ = config_.debug_enabled;
        }
    }
    
    ESP_LOGI(TAG, "Motion config updated from JSON:");
    ESP_LOGI(TAG, "  Free fall: threshold=%.2fg, duration=%lldms", 
             config_.free_fall_threshold_g, config_.free_fall_min_duration_ms);
    ESP_LOGI(TAG, "  Shake: normal=%.2fg, violently=%.2fg", 
             config_.shake_normal_threshold_g, config_.shake_violently_threshold_g);
    ESP_LOGI(TAG, "  Flip: threshold=%.1f°/s", config_.flip_threshold_deg_s);
    ESP_LOGI(TAG, "  Pickup: threshold=%.2fg, stable=%.2fg, count=%d", 
             config_.pickup_threshold_g, config_.pickup_stable_threshold_g, 
             config_.pickup_stable_count);
    ESP_LOGI(TAG, "  Upside down: threshold=%.2fg, count=%d", 
             config_.upside_down_threshold_g, config_.upside_down_stable_count);
}