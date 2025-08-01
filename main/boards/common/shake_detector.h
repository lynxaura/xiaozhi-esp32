#ifndef SHAKE_DETECTOR_H
#define SHAKE_DETECTOR_H

#include "qmi8658.h"
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Shake detection parameters
struct ShakeConfig {
    float threshold;           // Acceleration threshold for shake detection (g)
    uint32_t min_shake_time;   // Minimum time for valid shake (ms)
    uint32_t max_shake_time;   // Maximum time for valid shake (ms)
    uint32_t debounce_time;    // Time to wait between shake detections (ms)
    uint8_t min_shakes;        // Minimum number of shake events
};

// Default shake configuration
static const ShakeConfig DEFAULT_SHAKE_CONFIG = {
    .threshold = 1.2f,         // 1.2g threshold (balanced sensitivity)
    .min_shake_time = 100,     // 100ms minimum
    .max_shake_time = 1000,    // 1s maximum
    .debounce_time = 2000,     // 2s debounce (prevent continuous triggering)
    .min_shakes = 2            // At least 2 shake events
};

class ShakeDetector {
public:
    using ShakeCallback = std::function<void()>;
    
    ShakeDetector(QMI8658* imu_sensor, ShakeConfig config = DEFAULT_SHAKE_CONFIG);
    ~ShakeDetector();
    
    // Control methods
    void Start();
    void Stop();
    bool IsRunning() const;
    
    // Configuration
    void SetConfig(const ShakeConfig& config);
    void SetShakeCallback(ShakeCallback callback);
    
    // Statistics
    uint32_t GetShakeCount() const;
    void ResetShakeCount();
    
private:
    static void DetectionTask(void* pvParameters);
    void DetectionLoop();
    bool IsShakeMotion(const ImuData& data);
    
    QMI8658* imu_sensor_;
    ShakeConfig config_;
    ShakeCallback shake_callback_;
    
    TaskHandle_t detection_task_;
    bool running_;
    
    // Detection state
    uint32_t shake_count_;
    uint32_t last_shake_time_;
    uint32_t current_shake_start_;
    uint8_t consecutive_shakes_;
    bool in_shake_motion_;
    
    // Low-pass filter for noise reduction
    float filtered_magnitude_;
    static const float FILTER_ALPHA;
};

#endif // SHAKE_DETECTOR_H