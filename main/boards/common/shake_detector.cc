#include "shake_detector.h"
#include <esp_log.h>
#include <cmath>

#define TAG "ShakeDetector"

const float ShakeDetector::FILTER_ALPHA = 0.1f;

ShakeDetector::ShakeDetector(QMI8658* imu_sensor, ShakeConfig config)
    : imu_sensor_(imu_sensor)
    , config_(config)
    , detection_task_(nullptr)
    , running_(false)
    , shake_count_(0)
    , last_shake_time_(0)
    , current_shake_start_(0)
    , consecutive_shakes_(0)
    , in_shake_motion_(false)
    , filtered_magnitude_(1.0f) {
    
    if (!imu_sensor_) {
        ESP_LOGE(TAG, "IMU sensor is null");
    }
}

ShakeDetector::~ShakeDetector() {
    Stop();
}

void ShakeDetector::Start() {
    if (running_ || !imu_sensor_) {
        return;
    }
    
    ESP_LOGI(TAG, "Starting shake detection");
    running_ = true;
    
    xTaskCreate(DetectionTask, "shake_detect", 4096, this, 5, &detection_task_);
}

void ShakeDetector::Stop() {
    if (!running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping shake detection");
    running_ = false;
    
    if (detection_task_) {
        vTaskDelete(detection_task_);
        detection_task_ = nullptr;
    }
}

bool ShakeDetector::IsRunning() const {
    return running_;
}

void ShakeDetector::SetConfig(const ShakeConfig& config) {
    config_ = config;
    ESP_LOGI(TAG, "Updated shake config: threshold=%.2f, min_time=%lu, max_time=%lu", 
             config_.threshold, config_.min_shake_time, config_.max_shake_time);
}

void ShakeDetector::SetShakeCallback(ShakeCallback callback) {
    shake_callback_ = callback;
}

uint32_t ShakeDetector::GetShakeCount() const {
    return shake_count_;
}

void ShakeDetector::ResetShakeCount() {
    shake_count_ = 0;
}

void ShakeDetector::DetectionTask(void* pvParameters) {
    ShakeDetector* detector = static_cast<ShakeDetector*>(pvParameters);
    detector->DetectionLoop();
    vTaskDelete(nullptr);
}

void ShakeDetector::DetectionLoop() {
    ESP_LOGI(TAG, "Shake detection loop started");
    
    const TickType_t sample_period = pdMS_TO_TICKS(20); // 50Hz sampling
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (running_) {
        ImuData imu_data;
        
        if (imu_sensor_->ReadImuData(&imu_data)) {
            if (IsShakeMotion(imu_data)) {
                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                
                if (!in_shake_motion_) {
                    // Start of new shake motion
                    in_shake_motion_ = true;
                    current_shake_start_ = current_time;
                    consecutive_shakes_++;
                    
                    ESP_LOGI(TAG, "Shake motion started (count: %d)", consecutive_shakes_);
                }
            } else {
                if (in_shake_motion_) {
                    // End of shake motion
                    in_shake_motion_ = false;
                    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    uint32_t shake_duration = current_time - current_shake_start_;
                    
                    // Check if shake duration is valid
                    if (shake_duration >= config_.min_shake_time && 
                        shake_duration <= config_.max_shake_time) {
                        
                        // Check if enough time has passed since last valid shake
                        if (current_time - last_shake_time_ >= config_.debounce_time) {
                            
                            // Check if we have enough consecutive shakes
                            if (consecutive_shakes_ >= config_.min_shakes) {
                                ESP_LOGI(TAG, "Shake detected! Duration: %lums, Shakes: %d", 
                                        shake_duration, consecutive_shakes_);
                                
                                shake_count_++;
                                last_shake_time_ = current_time;
                                
                                // Trigger callback
                                if (shake_callback_) {
                                    shake_callback_();
                                }
                            }
                            
                            consecutive_shakes_ = 0; // Reset counter
                        }
                    } else {
                        ESP_LOGI(TAG, "Invalid shake duration: %lums", shake_duration);
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "Failed to read IMU data");
        }
        
        vTaskDelayUntil(&last_wake_time, sample_period);
    }
    
    ESP_LOGI(TAG, "Shake detection loop ended");
}

bool ShakeDetector::IsShakeMotion(const ImuData& data) {
    // Calculate total acceleration magnitude
    float magnitude = sqrtf(data.accel_x * data.accel_x + 
                           data.accel_y * data.accel_y + 
                           data.accel_z * data.accel_z);
    
    // Apply low-pass filter to reduce noise
    filtered_magnitude_ = FILTER_ALPHA * magnitude + (1.0f - FILTER_ALPHA) * filtered_magnitude_;
    
    // Detect significant deviation from 1g (gravity)
    float deviation = fabsf(filtered_magnitude_ - 1.0f);
    
    return deviation > config_.threshold;
}