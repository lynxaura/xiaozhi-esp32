#include "vibration_patterns.h"
#include <esp_log.h>
#include <map>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "VibrationPatterns"

// Helper function to create vibration step
static VibrationStep CreateStep(uint32_t hz, uint32_t duration) {
    VibrationStep step;
    step.hz = hz;
    step.duration = duration;
    return step;
}

// Helper function to create patterns                                                   //心跳第一层的.h
static VibrationPattern CreateHeartbeatPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(100, 200));  // Strong beat
    pattern.push_back(CreateStep(0, 100));    // Pause
    pattern.push_back(CreateStep(100, 200));  // Strong beat  
    pattern.push_back(CreateStep(0, 100));    // Pause
    pattern.push_back(CreateStep(100, 200));  // Strong beat
    pattern.push_back(CreateStep(0, 300));    // Long pause
    return pattern;
}

static VibrationPattern CreateShortBuzzPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(80, 100));   // Quick buzz
    return pattern;
}

static VibrationPattern CreateLongBuzzPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(120, 500));  // Long buzz
    return pattern;
}

static VibrationPattern CreateDoubleTapPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(150, 100));  // First tap
    pattern.push_back(CreateStep(0, 100));    // Pause
    pattern.push_back(CreateStep(150, 100));  // Second tap
    return pattern;
}

static VibrationPattern CreateTripleTapPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(150, 100));  // First tap
    pattern.push_back(CreateStep(0, 100));    // Pause
    pattern.push_back(CreateStep(150, 100));  // Second tap
    pattern.push_back(CreateStep(0, 100));    // Pause
    pattern.push_back(CreateStep(150, 100));  // Third tap
    return pattern;
}

static VibrationPattern CreatePulsePattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(200, 150));  // Pulse
    pattern.push_back(CreateStep(0, 150));    // Pause
    pattern.push_back(CreateStep(200, 150));  // Pulse
    pattern.push_back(CreateStep(0, 150));    // Pause
    pattern.push_back(CreateStep(200, 150));  // Pulse
    pattern.push_back(CreateStep(0, 150));    // Pause
    return pattern;
}

static VibrationPattern CreateWavePattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(50, 200));   // Low intensity
    pattern.push_back(CreateStep(100, 200));  // Medium intensity
    pattern.push_back(CreateStep(200, 200));  // High intensity
    pattern.push_back(CreateStep(100, 200));  // Medium intensity
    pattern.push_back(CreateStep(50, 200));   // Low intensity
    return pattern;
}

static VibrationPattern CreateAlertPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(250, 100));  // High intensity short burst
    pattern.push_back(CreateStep(0, 50));     // Brief pause
    pattern.push_back(CreateStep(250, 100));  // High intensity short burst
    pattern.push_back(CreateStep(0, 50));     // Brief pause
    pattern.push_back(CreateStep(250, 100));  // High intensity short burst
    pattern.push_back(CreateStep(0, 200));    // Longer pause
    pattern.push_back(CreateStep(250, 100));  // High intensity short burst
    pattern.push_back(CreateStep(0, 50));     // Brief pause
    pattern.push_back(CreateStep(250, 100));  // High intensity short burst
    return pattern;
}

static VibrationPattern CreateSuccessPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(80, 100));   // Gentle start
    pattern.push_back(CreateStep(120, 150));  // Build up
    pattern.push_back(CreateStep(200, 200));  // Strong finish
    return pattern;
}

static VibrationPattern CreateErrorPattern() {
    VibrationPattern pattern;
    pattern.push_back(CreateStep(300, 50));   // Sharp buzz
    pattern.push_back(CreateStep(0, 100));    // Pause
    pattern.push_back(CreateStep(300, 50));   // Sharp buzz
    pattern.push_back(CreateStep(0, 100));    // Pause
    pattern.push_back(CreateStep(300, 50));   // Sharp buzz
    return pattern;
}

// Define predefined vibration patterns
const VibrationPattern HEARTBEAT_PATTERN = CreateHeartbeatPattern();
const VibrationPattern SHORT_BUZZ_PATTERN = CreateShortBuzzPattern();
const VibrationPattern LONG_BUZZ_PATTERN = CreateLongBuzzPattern();
const VibrationPattern DOUBLE_TAP_PATTERN = CreateDoubleTapPattern();
const VibrationPattern TRIPLE_TAP_PATTERN = CreateTripleTapPattern();
const VibrationPattern PULSE_PATTERN = CreatePulsePattern();
const VibrationPattern WAVE_PATTERN = CreateWavePattern();
const VibrationPattern ALERT_PATTERN = CreateAlertPattern();
const VibrationPattern SUCCESS_PATTERN = CreateSuccessPattern();
const VibrationPattern ERROR_PATTERN = CreateErrorPattern();

VibrationPatterns::VibrationPatterns(VibrationMotor* motor)
    : motor_(motor)
    , pattern_task_(nullptr)
    , playing_(false) {
    
    if (!motor_) {
        ESP_LOGE(TAG, "VibrationMotor is null");
    }
}

VibrationPatterns::~VibrationPatterns() {
    Stop();
}

void VibrationPatterns::PlayHeartbeat() {
    PlayPattern(HEARTBEAT_PATTERN);
}

void VibrationPatterns::PlayShortBuzz() {
    PlayPattern(SHORT_BUZZ_PATTERN);
}

void VibrationPatterns::PlayLongBuzz() {
    PlayPattern(LONG_BUZZ_PATTERN);
}

void VibrationPatterns::PlayDoubleTap() {
    PlayPattern(DOUBLE_TAP_PATTERN);
}

void VibrationPatterns::PlayTripleTap() {
    PlayPattern(TRIPLE_TAP_PATTERN);
}

void VibrationPatterns::PlayPulse() {
    PlayPattern(PULSE_PATTERN);
}

void VibrationPatterns::PlayWave() {
    PlayPattern(WAVE_PATTERN);
}

void VibrationPatterns::PlayAlert() {
    PlayPattern(ALERT_PATTERN);
}

void VibrationPatterns::PlaySuccess() {
    PlayPattern(SUCCESS_PATTERN);
}

void VibrationPatterns::PlayError() {
    PlayPattern(ERROR_PATTERN);
}

void VibrationPatterns::PlayPattern(const VibrationPattern& pattern, PlaybackMode mode) {
    if (!motor_ || pattern.empty()) {
        ESP_LOGW(TAG, "Cannot play pattern: motor is null or pattern is empty");
        return;
    }
    
    // Stop any currently playing pattern
    Stop();
    
    ESP_LOGI(TAG, "Starting pattern with %zu steps", pattern.size());
    playing_ = true;
    
    // Create a copy of the pattern for the task
    VibrationPattern* pattern_copy = new VibrationPattern(pattern);
    
    struct TaskParams {
        VibrationPatterns* patterns;
        VibrationPattern* pattern;
        PlaybackMode mode;
    };
    
    TaskParams* params = new TaskParams{this, pattern_copy, mode};
    
    xTaskCreate(PatternTask, "vibration_pattern", 4096, params, 5, &pattern_task_);
}

void VibrationPatterns::Stop() {
    if (!playing_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping vibration pattern");
    playing_ = false;
    
    if (motor_) {
        motor_->Stop();
    }
    
    if (pattern_task_) {
        vTaskDelete(pattern_task_);
        pattern_task_ = nullptr;
    }
}

bool VibrationPatterns::IsPlaying() const {
    return playing_;
}

void VibrationPatterns::RegisterCustomPattern(const std::string& name, const VibrationPattern& pattern) {
    custom_patterns_[name] = pattern;
    ESP_LOGI(TAG, "Registered custom pattern '%s' with %zu steps", name.c_str(), pattern.size());
}

void VibrationPatterns::PlayCustomPattern(const std::string& name) {
    auto it = custom_patterns_.find(name);
    if (it != custom_patterns_.end()) {
        PlayPattern(it->second);
    } else {
        ESP_LOGW(TAG, "Custom pattern '%s' not found", name.c_str());
    }
}

void VibrationPatterns::SetCompletionCallback(std::function<void()> callback) {
    completion_callback_ = callback;
}

void VibrationPatterns::PatternTask(void* pvParameters) {
    struct TaskParams {
        VibrationPatterns* patterns;
        VibrationPattern* pattern;
        PlaybackMode mode;
    };
    
    TaskParams* params = static_cast<TaskParams*>(pvParameters);
    VibrationPatterns* patterns = params->patterns;
    VibrationPattern* pattern = params->pattern;
    PlaybackMode mode = params->mode;
    
    patterns->ExecutePattern(*pattern, mode);
    
    // Cleanup
    delete pattern;
    delete params;
    
    // Mark as completed
    patterns->playing_ = false;
    patterns->pattern_task_ = nullptr;
    
    // Call completion callback if set
    if (patterns->completion_callback_) {
        patterns->completion_callback_();
    }
    
    vTaskDelete(nullptr);
}

void VibrationPatterns::ExecutePattern(const VibrationPattern& pattern, PlaybackMode mode) {
    if (mode == PlaybackMode::SEQUENTIAL) {
        // Execute steps one after another
        for (const auto& step : pattern) {
            if (!playing_) {
                break; // Pattern was stopped
            }
            
            ESP_LOGD(TAG, "Pattern step: %lu Hz for %lu ms", step.hz, step.duration);
            motor_->Motor(step.hz, step.duration);
            
            // Wait for this step to complete
            vTaskDelay(pdMS_TO_TICKS(step.duration));
        }
    } else {
        // PARALLEL mode - not really applicable for single motor
        // We'll just play the first step for now
        ESP_LOGW(TAG, "Parallel mode not supported for single motor, playing first step only");
        if (!pattern.empty()) {
            const auto& step = pattern[0];
            motor_->Motor(step.hz, step.duration);
            vTaskDelay(pdMS_TO_TICKS(step.duration));
        }
    }
    
    ESP_LOGI(TAG, "Pattern execution completed");
}