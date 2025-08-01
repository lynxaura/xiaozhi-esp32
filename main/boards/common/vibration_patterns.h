#ifndef VIBRATION_PATTERNS_H
#define VIBRATION_PATTERNS_H

#include "vibration_motor.h"
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Structure to define a single vibration step
struct VibrationStep {
    uint32_t hz;        // Frequency (0 = off)
    uint32_t duration;  // Duration in milliseconds
};

// Type alias for vibration pattern (sequence of steps)
using VibrationPattern = std::vector<VibrationStep>;

// Execution mode for pattern playback
enum class PlaybackMode {
    SEQUENTIAL,     // Play steps one after another
    PARALLEL        // Play all steps simultaneously (not recommended for single motor)
};

class VibrationPatterns {
public:
    VibrationPatterns(VibrationMotor* motor);
    ~VibrationPatterns();
    
    // Play predefined patterns
    void PlayHeartbeat();
    void PlayShortBuzz();
    void PlayLongBuzz();
    void PlayDoubleTap();
    void PlayTripleTap();
    void PlayPulse();
    void PlayWave();
    void PlayAlert();
    void PlaySuccess();
    void PlayError();
    
    // Play custom pattern
    void PlayPattern(const VibrationPattern& pattern, PlaybackMode mode = PlaybackMode::SEQUENTIAL);
    
    // Control playback
    void Stop();
    bool IsPlaying() const;
    
    // Pattern management
    void RegisterCustomPattern(const std::string& name, const VibrationPattern& pattern);
    void PlayCustomPattern(const std::string& name);
    
    // Callback for pattern completion
    void SetCompletionCallback(std::function<void()> callback);

private:
    static void PatternTask(void* pvParameters);
    void ExecutePattern(const VibrationPattern& pattern, PlaybackMode mode);
    
    VibrationMotor* motor_;
    TaskHandle_t pattern_task_;
    bool playing_;
    std::function<void()> completion_callback_;
    
    // Custom patterns storage
    std::map<std::string, VibrationPattern> custom_patterns_;
};

// Predefined vibration patterns - declared in vibration_patterns.cc
extern const VibrationPattern HEARTBEAT_PATTERN;
extern const VibrationPattern SHORT_BUZZ_PATTERN;
extern const VibrationPattern LONG_BUZZ_PATTERN;
extern const VibrationPattern DOUBLE_TAP_PATTERN;
extern const VibrationPattern TRIPLE_TAP_PATTERN;
extern const VibrationPattern PULSE_PATTERN;
extern const VibrationPattern WAVE_PATTERN;
extern const VibrationPattern ALERT_PATTERN;
extern const VibrationPattern SUCCESS_PATTERN;
extern const VibrationPattern ERROR_PATTERN;

#endif // VIBRATION_PATTERNS_H