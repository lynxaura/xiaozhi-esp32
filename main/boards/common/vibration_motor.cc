#include "vibration_motor.h"
#include <esp_log.h>
#include <esp_err.h>

#define TAG "VibrationMotor"

VibrationMotor::VibrationMotor(gpio_num_t pin)
    : pin_(pin)
    , ledc_channel_(LEDC_CHANNEL_0)
    , ledc_timer_(LEDC_TIMER_0)
    , stop_timer_(nullptr)
    , initialized_(false)
    , running_(false)
    , max_frequency_(DEFAULT_MAX_HZ)
    , duty_cycle_(DEFAULT_DUTY_CYCLE) {
}

VibrationMotor::~VibrationMotor() {
    Stop();
    if (stop_timer_) {
        xTimerDelete(stop_timer_, pdMS_TO_TICKS(100));
    }
}

bool VibrationMotor::Initialize() {
    if (initialized_) {
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing vibration motor on GPIO %d", pin_);
    
    // Configure LEDC timer  
    ledc_timer_config_t timer_config;
    timer_config.speed_mode = LEDC_MODE;
    timer_config.duty_resolution = LEDC_TIMER_10_BIT;
    timer_config.timer_num = ledc_timer_;
    timer_config.freq_hz = LEDC_FREQUENCY;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Configure LEDC channel
    ledc_channel_config_t channel_config;
    channel_config.gpio_num = pin_;
    channel_config.speed_mode = LEDC_MODE;
    channel_config.channel = ledc_channel_;
    channel_config.timer_sel = ledc_timer_;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    
    ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Create stop timer
    stop_timer_ = xTimerCreate("motor_stop", pdMS_TO_TICKS(1000), pdFALSE, this, TimerCallback);
    if (!stop_timer_) {
        ESP_LOGE(TAG, "Failed to create stop timer");
        return false;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "Vibration motor initialized successfully");
    return true;
}

void VibrationMotor::Motor(uint32_t hz, uint32_t time) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Motor not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Motor command: %lu Hz for %lu ms", hz, time);
    
    if (hz == 0 || time == 0) {
        Stop();
        return;
    }
    
    // Start the motor
    Start(hz);
    
    // Set timer to stop after specified time
    if (stop_timer_) {
        xTimerChangePeriod(stop_timer_, pdMS_TO_TICKS(time), pdMS_TO_TICKS(10));
        xTimerStart(stop_timer_, pdMS_TO_TICKS(10));
    }
}

void VibrationMotor::Start(uint32_t hz) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Motor not initialized");
        return;
    }
    
    if (hz > max_frequency_) {
        hz = max_frequency_;
    }
    
    if (hz == 0) {
        Stop();
        return;
    }
    
    // Set PWM frequency - for vibration motors, we use frequency to control intensity
    esp_err_t ret = ledc_set_freq(LEDC_MODE, ledc_timer_, hz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set frequency: %s", esp_err_to_name(ret));
        return;
    }
    
    // Calculate duty cycle (10-bit resolution = 1024 levels)
    uint32_t duty = (1024 * duty_cycle_) / 100;
    
    ret = ledc_set_duty(LEDC_MODE, ledc_channel_, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = ledc_update_duty(LEDC_MODE, ledc_channel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(ret));
        return;
    }
    
    running_ = true;
    ESP_LOGD(TAG, "Motor started at %lu Hz", hz);
}

void VibrationMotor::Stop() {
    if (!initialized_ || !running_) {
        return;
    }
    
    // Stop the timer if running
    if (stop_timer_) {
        xTimerStop(stop_timer_, pdMS_TO_TICKS(10));
    }
    
    StopMotor();
}

void VibrationMotor::StopMotor() {
    if (!initialized_) {
        return;
    }
    
    // Set duty cycle to 0 to stop the motor
    esp_err_t ret = ledc_set_duty(LEDC_MODE, ledc_channel_, 0);
    if (ret == ESP_OK) {
        ledc_update_duty(LEDC_MODE, ledc_channel_);
    }
    
    running_ = false;
    ESP_LOGD(TAG, "Motor stopped");
}

bool VibrationMotor::IsRunning() const {
    return running_;
}

void VibrationMotor::SetMaxFrequency(uint32_t max_hz) {
    max_frequency_ = max_hz;
    ESP_LOGI(TAG, "Max frequency set to %lu Hz", max_hz);
}

void VibrationMotor::SetDutyCycle(uint8_t duty_percent) {
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    duty_cycle_ = duty_percent;
    ESP_LOGI(TAG, "Duty cycle set to %d%%", duty_percent);
}

void VibrationMotor::TimerCallback(TimerHandle_t timer) {
    VibrationMotor* motor = static_cast<VibrationMotor*>(pvTimerGetTimerID(timer));
    if (motor) {
        motor->StopMotor();
    }
}