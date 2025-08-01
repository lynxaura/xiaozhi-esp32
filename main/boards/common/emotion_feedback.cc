#include "emotion_feedback.h"
#include <esp_log.h>
#include <map>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "EmotionFeedback"
#define EMOTION_QUEUE_SIZE 10

EmotionFeedback::EmotionFeedback(QMI8658* imu, VibrationMotor* motor, gpio_num_t motor_pin)
    : imu_(imu)
    , motor_(motor)
    , patterns_(nullptr)
    , shake_detector_(nullptr)
    , queue_task_(nullptr)
    , emotion_queue_(nullptr)
    , running_(false)
    , playing_(false)
    , shake_detection_enabled_(false)
    , shake_response_emotion_("happy") {
    
    if (!imu_ || !motor_) {
        ESP_LOGE(TAG, "IMU or Motor is null");
        return;
    }
    
    patterns_ = new VibrationPatterns(motor_);
    shake_detector_ = new ShakeDetector(imu_);
}

EmotionFeedback::~EmotionFeedback() {
    Stop();
    
    if (patterns_) {
        delete patterns_;
    }
    
    if (shake_detector_) {
        delete shake_detector_;
    }
    
    if (emotion_queue_) {
        vQueueDelete(emotion_queue_);
    }
}

bool EmotionFeedback::Initialize() {
    ESP_LOGI(TAG, "Initializing emotion feedback system");
    
    if (!motor_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize vibration motor");
        return false;
    }
    
    if (!imu_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize IMU sensor");
        return false;
    }
    
    // Create emotion queue
    emotion_queue_ = xQueueCreate(EMOTION_QUEUE_SIZE, sizeof(std::string*));
    if (!emotion_queue_) {
        ESP_LOGE(TAG, "Failed to create emotion queue");
        return false;
    }
    
    // Initialize default emotions
    InitializeDefaultEmotions();
    
    // Set shake detection callback
    if (shake_detector_) {
        shake_detector_->SetShakeCallback([this]() {
            OnShakeDetected();
        });
    }
    
    // Set pattern completion callback
    if (patterns_) {
        patterns_->SetCompletionCallback([this]() {
            playing_ = false;
        });
    }
    
    running_ = true;
    
    // Start queue processor task
    xTaskCreate(QueueProcessorTask, "emotion_queue", 4096, this, 5, &queue_task_);
    
    ESP_LOGI(TAG, "Emotion feedback system initialized successfully");
    return true;
}

void EmotionFeedback::Happy() {
    TriggerEmotion("happy");
}

void EmotionFeedback::Excited() {
    TriggerEmotion("excited");
}

void EmotionFeedback::Calm() {
    TriggerEmotion("calm");
}

void EmotionFeedback::Alert() {
    TriggerEmotion("alert");
}

void EmotionFeedback::Sad() {
    TriggerEmotion("sad");
}

void EmotionFeedback::Angry() {
    TriggerEmotion("angry");
}

void EmotionFeedback::Surprised() {
    TriggerEmotion("surprised");
}

void EmotionFeedback::Love() {
    TriggerEmotion("love");
}

void EmotionFeedback::DefineEmotion(const std::string& name, 
                                   const std::vector<std::string>& patterns,
                                   EmotionMode mode,
                                   uint32_t delay) {
    EmotionCommand command;
    command.emotion_name = name;
    command.pattern_names = patterns;
    command.mode = mode;
    command.delay_between_patterns = delay;
    
    emotion_definitions_[name] = command;
    ESP_LOGI(TAG, "Defined emotion '%s' with %zu patterns", name.c_str(), patterns.size());
}

void EmotionFeedback::TriggerEmotion(const std::string& emotion_name) {
    if (!running_) {
        ESP_LOGW(TAG, "Emotion feedback system not running");
        return;
    }
    
    auto it = emotion_definitions_.find(emotion_name);
    if (it == emotion_definitions_.end()) {
        ESP_LOGW(TAG, "Emotion '%s' not defined", emotion_name.c_str());
        return;
    }
    
    ESP_LOGI(TAG, "Triggering emotion: %s", emotion_name.c_str());
    ExecuteEmotion(it->second);
}

void EmotionFeedback::QueueEmotion(const std::string& emotion_name) {
    if (!emotion_queue_) {
        ESP_LOGW(TAG, "Emotion queue not initialized");
        return;
    }
    
    std::string* emotion_copy = new std::string(emotion_name);
    
    if (xQueueSend(emotion_queue_, &emotion_copy, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue emotion '%s' - queue full", emotion_name.c_str());
        delete emotion_copy;
    } else {
        ESP_LOGI(TAG, "Queued emotion: %s", emotion_name.c_str());
    }
}

void EmotionFeedback::ClearQueue() {
    if (!emotion_queue_) {
        return;
    }
    
    std::string* emotion;
    while (xQueueReceive(emotion_queue_, &emotion, 0) == pdTRUE) {
        delete emotion;
    }
    
    ESP_LOGI(TAG, "Emotion queue cleared");
}

bool EmotionFeedback::IsQueueEmpty() const {
    if (!emotion_queue_) {
        return true;
    }
    
    return uxQueueMessagesWaiting(emotion_queue_) == 0;
}

void EmotionFeedback::Stop() {
    running_ = false;
    playing_ = false;
    
    if (patterns_) {
        patterns_->Stop();
    }
    
    if (shake_detector_) {
        shake_detector_->Stop();
    }
    
    if (queue_task_) {
        vTaskDelete(queue_task_);
        queue_task_ = nullptr;
    }
    
    ClearQueue();
    ESP_LOGI(TAG, "Emotion feedback system stopped");
}

bool EmotionFeedback::IsPlaying() const {
    return playing_;
}

void EmotionFeedback::EnableShakeDetection(bool enable) {
    shake_detection_enabled_ = enable;
    
    if (shake_detector_) {
        if (enable) {
            shake_detector_->Start();
            ESP_LOGI(TAG, "Shake detection enabled");
        } else {
            shake_detector_->Stop();
            ESP_LOGI(TAG, "Shake detection disabled");
        }
    }
}

void EmotionFeedback::SetShakeResponseEmotion(const std::string& emotion_name) {
    shake_response_emotion_ = emotion_name;
    ESP_LOGI(TAG, "Shake response emotion set to: %s", emotion_name.c_str());
}

void EmotionFeedback::SetEmotionCompletedCallback(std::function<void(const std::string&)> callback) {
    emotion_completed_callback_ = callback;
}

void EmotionFeedback::SetShakeDetectedCallback(std::function<void()> callback) {
    shake_detected_callback_ = callback;
}

void EmotionFeedback::QueueProcessorTask(void* pvParameters) {
    EmotionFeedback* feedback = static_cast<EmotionFeedback*>(pvParameters);
    feedback->ProcessQueue();
    vTaskDelete(nullptr);
}

void EmotionFeedback::ProcessQueue() {
    ESP_LOGI(TAG, "Queue processor started");
    
    while (running_) {
        std::string* emotion_name;
        
        if (xQueueReceive(emotion_queue_, &emotion_name, pdMS_TO_TICKS(100)) == pdTRUE) {
            TriggerEmotion(*emotion_name);
            delete emotion_name;
        }
    }
    
    ESP_LOGI(TAG, "Queue processor ended");
}

void EmotionFeedback::ExecuteEmotion(const EmotionCommand& command) {
    if (!patterns_) {
        return;
    }
    
    playing_ = true;
    
    switch (command.mode) {
        case EmotionMode::SEQUENTIAL:
            for (const auto& pattern_name : command.pattern_names) {
                if (!playing_) break; // Emotion was stopped
                
                // Play pattern based on name
                if (pattern_name == "heartbeat") {
                    patterns_->PlayHeartbeat();
                } else if (pattern_name == "short_buzz") {
                    patterns_->PlayShortBuzz();
                } else if (pattern_name == "long_buzz") {
                    patterns_->PlayLongBuzz();
                } else if (pattern_name == "double_tap") {
                    patterns_->PlayDoubleTap();
                } else if (pattern_name == "triple_tap") {
                    patterns_->PlayTripleTap();
                } else if (pattern_name == "pulse") {
                    patterns_->PlayPulse();
                } else if (pattern_name == "wave") {
                    patterns_->PlayWave();
                } else if (pattern_name == "alert") {
                    patterns_->PlayAlert();
                } else if (pattern_name == "success") {
                    patterns_->PlaySuccess();
                } else if (pattern_name == "error") {
                    patterns_->PlayError();
                } else {
                    patterns_->PlayCustomPattern(pattern_name);
                }
                
                // Wait for pattern to complete and add delay
                while (patterns_->IsPlaying() && playing_) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                
                if (command.delay_between_patterns > 0) {
                    vTaskDelay(pdMS_TO_TICKS(command.delay_between_patterns));
                }
            }
            break;
            
        case EmotionMode::SIMULTANEOUS:
        case EmotionMode::OVERLAY:
            // For single motor, we can only play one pattern at a time
            // So we'll play the first pattern
            ESP_LOGW(TAG, "Simultaneous/Overlay mode limited to first pattern for single motor");
            if (!command.pattern_names.empty()) {
                const auto& pattern_name = command.pattern_names[0];
                if (pattern_name == "heartbeat") {
                    patterns_->PlayHeartbeat();
                } else if (pattern_name == "short_buzz") {
                    patterns_->PlayShortBuzz();
                } else {
                    patterns_->PlayCustomPattern(pattern_name);
                }
            }
            break;
    }
    
    // Call completion callback
    if (emotion_completed_callback_) {
        emotion_completed_callback_(command.emotion_name);
    }
    
    playing_ = false;
}

void EmotionFeedback::OnShakeDetected() {
    ESP_LOGI(TAG, "Shake detected - triggering response emotion");
    
    if (shake_detected_callback_) {
        shake_detected_callback_();
    }
    
    if (shake_detection_enabled_ && !shake_response_emotion_.empty()) {
        QueueEmotion(shake_response_emotion_);
    }
}

void EmotionFeedback::InitializeDefaultEmotions() {
    // Happy: heartbeat + short_buzz combination
    std::vector<std::string> happy_patterns;
    happy_patterns.push_back("heartbeat");
    happy_patterns.push_back("short_buzz");
    DefineEmotion("happy", happy_patterns, EmotionMode::SEQUENTIAL, 300);
    
    // Excited: multiple quick buzzes
    std::vector<std::string> excited_patterns;
    excited_patterns.push_back("triple_tap");
    excited_patterns.push_back("pulse");
    excited_patterns.push_back("short_buzz");
    DefineEmotion("excited", excited_patterns, EmotionMode::SEQUENTIAL, 200);
    
    // Calm: gentle wave pattern
    std::vector<std::string> calm_patterns;
    calm_patterns.push_back("wave");
    DefineEmotion("calm", calm_patterns, EmotionMode::SEQUENTIAL, 0);
    
    // Alert: urgent attention pattern
    std::vector<std::string> alert_patterns;
    alert_patterns.push_back("alert");
    DefineEmotion("alert", alert_patterns, EmotionMode::SEQUENTIAL, 0);
    
    // Sad: slow, low-intensity pattern
    std::vector<std::string> sad_patterns;
    sad_patterns.push_back("pulse");
    DefineEmotion("sad", sad_patterns, EmotionMode::SEQUENTIAL, 0);
    
    // Angry: sharp, intense bursts
    std::vector<std::string> angry_patterns;
    angry_patterns.push_back("error");
    angry_patterns.push_back("alert");
    DefineEmotion("angry", angry_patterns, EmotionMode::SEQUENTIAL, 100);
    
    // Surprised: sudden burst
    std::vector<std::string> surprised_patterns;
    surprised_patterns.push_back("double_tap");
    DefineEmotion("surprised", surprised_patterns, EmotionMode::SEQUENTIAL, 0);
    
    // Love: heartbeat pattern repeated
    std::vector<std::string> love_patterns;
    love_patterns.push_back("heartbeat");
    love_patterns.push_back("heartbeat");
    DefineEmotion("love", love_patterns, EmotionMode::SEQUENTIAL, 500);
    
    ESP_LOGI(TAG, "Initialized %zu default emotions", emotion_definitions_.size());
}