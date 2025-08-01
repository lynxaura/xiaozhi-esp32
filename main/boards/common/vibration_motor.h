#ifndef VIBRATION_MOTOR_H
#define VIBRATION_MOTOR_H

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <cstdint>

class VibrationMotor {
public:
    VibrationMotor(gpio_num_t pin);
    ~VibrationMotor();
    
    // Initialize the motor driver
    bool Initialize();
    
    // Main motor control function
    // hz: vibration frequency (0-1000 Hz, 0 = off)
    // time: duration in milliseconds
    void Motor(uint32_t hz, uint32_t time);
    
    // Immediate control functions
    void Start(uint32_t hz);
    void Stop();
    bool IsRunning() const;
    
    // Configuration
    void SetMaxFrequency(uint32_t max_hz);
    void SetDutyCycle(uint8_t duty_percent); // 0-100%
    
private:
    static void TimerCallback(TimerHandle_t timer);
    void StopMotor();
    
    gpio_num_t pin_;
    ledc_channel_t ledc_channel_;
    ledc_timer_t ledc_timer_;
    TimerHandle_t stop_timer_;
    
    bool initialized_;
    bool running_;
    uint32_t max_frequency_;
    uint8_t duty_cycle_;
    
    static const uint32_t LEDC_FREQUENCY = 1000;    // Base frequency for LEDC
    static const ledc_mode_t LEDC_MODE = LEDC_LOW_SPEED_MODE;
    static const uint32_t DEFAULT_MAX_HZ = 1000;
    static const uint8_t DEFAULT_DUTY_CYCLE = 50;   // 50% duty cycle
};

#endif // VIBRATION_MOTOR_H