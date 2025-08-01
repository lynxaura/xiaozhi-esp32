#ifndef EMOTION_FEEDBACK_H
#define EMOTION_FEEDBACK_H

#include "vibration_patterns.h"
#include "shake_detector.h"
#include "qmi8658.h"
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Execution modes for emotion feedback
enum class EmotionMode {
    SEQUENTIAL,     // Execute patterns one after another
    SIMULTANEOUS,   // Try to execute patterns simultaneously (limited by single motor)
    OVERLAY         // Overlay patterns (advanced mixing)
};

// Structure for emotion feedback commands
struct EmotionCommand {
    std::string emotion_name;
    std::vector<std::string> pattern_names;
    EmotionMode mode;
    uint32_t delay_between_patterns; // Only used in SEQUENTIAL mode
};

class EmotionFeedback {
public:
    EmotionFeedback(QMI8658* imu, VibrationMotor* motor, gpio_num_t motor_pin);
    ~EmotionFeedback();
    
    // Initialize the emotion feedback system
    bool Initialize();
    
    // High-level emotion functions
    void Happy();
    void Excited();
    void Calm();
    void Alert();
    void Sad();
    void Angry();
    void Surprised();
    void Love();
    
    // Custom emotion definition
    void DefineEmotion(const std::string& name, 
                      const std::vector<std::string>& patterns,
                      EmotionMode mode = EmotionMode::SEQUENTIAL,
                      uint32_t delay = 200);
    
    void TriggerEmotion(const std::string& emotion_name);
    
    // Queue management
    void QueueEmotion(const std::string& emotion_name);
    void ClearQueue();
    bool IsQueueEmpty() const;
    
    // Control functions
    void Stop();
    bool IsPlaying() const;
    
    // Shake detection integration
    void EnableShakeDetection(bool enable = true);
    void SetShakeResponseEmotion(const std::string& emotion_name);
    
    // Event callbacks
    void SetEmotionCompletedCallback(std::function<void(const std::string&)> callback);
    void SetShakeDetectedCallback(std::function<void()> callback);

private:
    static void QueueProcessorTask(void* pvParameters);
    void ProcessQueue();
    void ExecuteEmotion(const EmotionCommand& command);
    void OnShakeDetected();
    
    // Core components
    QMI8658* imu_;
    VibrationMotor* motor_;
    VibrationPatterns* patterns_;
    ShakeDetector* shake_detector_;
    
    // Task and queue management
    TaskHandle_t queue_task_;
    QueueHandle_t emotion_queue_;
    bool running_;
    bool playing_;
    
    // Emotion definitions
    std::map<std::string, EmotionCommand> emotion_definitions_;
    
    // Settings
    bool shake_detection_enabled_;
    std::string shake_response_emotion_;
    
    // Callbacks
    std::function<void(const std::string&)> emotion_completed_callback_;
    std::function<void()> shake_detected_callback_;
    
    void InitializeDefaultEmotions();
};

#endif // EMOTION_FEEDBACK_H