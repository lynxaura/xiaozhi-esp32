#include "vibration.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>
#include <cmath>

static const char* TAG = "Vibration";

// 振动控制 - 通过PCA9685的12位PWM控制振动强度 (0-4095)

// 音频提示功能 - 简单的日志提示（可以替换为实际音频播放）
void PlayBeepSound() {
    ESP_LOGI(TAG, "🔊 BEEP! Pattern starting in 1 second...");
    
    // 等待1秒作为提示持续时间
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// 振动任务配置
#define VIBRATION_TASK_STACK_SIZE 4096  // 增加栈大小，确保4字节对齐
#define VIBRATION_TASK_PRIORITY   3
#define VIBRATION_QUEUE_SIZE      8

// 振动模式数据定义 - 存储在Flash中节省RAM (12位PWM: 0-4095)
static const vibration_keyframe_t vibration_short_buzz[] = {
    {4095, 100},  // 高强度100ms
    {0, 0}       // 结束标记
};

static const vibration_keyframe_t vibration_purr_short[] = {
    {2520, 50}, {3300, 50}, {3060, 50}, {3540, 50}, {2640, 50},
    {2880, 50}, {2400, 100}, {0, 0}
};

static const vibration_keyframe_t vibration_purr_pattern[] = {
    {2808, 100}, {3423, 100}, {3115, 100}, {3730, 100}, 
    {2962, 100}, {3577, 100}, {3269, 200},
    {2500, 100}, {3115, 100}, {2808, 100}, {3423, 100},
    {0, 300}, // 300ms暂停后重复
    {2808, 100}, {3423, 100}, {3115, 100}, {3730, 100},
    {0, 0}
};

static const vibration_keyframe_t vibration_gentle_heartbeat[] = {
    {2800, 100}, {1780, 50},   // 强弱心跳
    {0, 600},                  // 心跳间隔
    {2800, 100}, {1780, 50},   // 强弱心跳
    {0, 600},                  // 心跳间隔
    {2800, 100}, {1780, 50},   // 强弱心跳
    {0, 0}
};

static const vibration_keyframe_t vibration_struggle_pattern[] = {
    {3200, 80}, {0, 60}, {4000, 120}, {0, 40}, 
    {2880, 100}, {0, 80}, {3520, 150}, {0, 50},
    {3040, 90}, {0, 70}, {3840, 110}, {0, 90},
    {3360, 130}, {0, 0}
};

static const vibration_keyframe_t vibration_sharp_buzz[] = {
    {4095, 200},  // 高强度200ms
    {0, 0}
};

static const vibration_keyframe_t vibration_tremble_pattern[] = {
    {3429, 60}, {0, 50}, {3858, 60}, {0, 30}, {3000, 60}, {0, 30},
    {4095, 60}, {0, 50}, {3600, 60}, {0, 30}, {3258, 60}, {0, 30},
    {4029, 60}, {0, 50}, {3516, 60}, {0, 30}, {3087, 60}, {0, 30},
    {0, 200}, // 短暂停顿
    {3429, 50}, {0, 50}, {3858, 60}, {0, 30}, {3000, 60}, {0, 30},
    {0, 0}
};

static const vibration_keyframe_t vibration_giggle_pattern[] = {
    {3131, 78}, {0, 52}, {3392, 65}, {0, 39}, {3653, 78}, {0, 52},
    {3261, 65}, {0, 39}, {3523, 78}, {0, 52}, {3000, 65}, {0, 39},
    {3783, 78}, {0, 52}, {3131, 65}, {0, 39}, {3392, 78}, {0, 260},
    {0, 0}
};

static const vibration_keyframe_t vibration_heartbeat_strong[] = {
    {3600, 120}, {2320, 80},  // 强心跳
    {0, 800},                 // 更长的心跳间隔
    {3600, 120}, {2320, 80},  // 强心跳
    {0, 800},                 // 更长的心跳间隔
    {3600, 120}, {2320, 80},  // 强心跳
    {0, 0}
};

static const vibration_keyframe_t vibration_erratic_strong[] = {
    {4095, 70}, {0, 30}, {3200, 120}, {0, 60}, {3840, 90}, {0, 20},
    {2880, 140}, {0, 80}, {3520, 60}, {0, 40}, {4095, 100}, {0, 90},
    {2560, 110}, {0, 50}, {3680, 80}, {0, 30}, {3040, 130}, {0, 70},
    {4095, 90}, {0, 40}, {3360, 100}, {0, 0}
};

// 振动模式查找表
static const vibration_keyframe_t* const vibration_patterns[] = {
    [VIBRATION_SHORT_BUZZ] = vibration_short_buzz,
    [VIBRATION_PURR_SHORT] = vibration_purr_short,
    [VIBRATION_PURR_PATTERN] = vibration_purr_pattern,
    [VIBRATION_GENTLE_HEARTBEAT] = vibration_gentle_heartbeat,
    [VIBRATION_STRUGGLE_PATTERN] = vibration_struggle_pattern,
    [VIBRATION_SHARP_BUZZ] = vibration_sharp_buzz,
    [VIBRATION_TREMBLE_PATTERN] = vibration_tremble_pattern,
    [VIBRATION_GIGGLE_PATTERN] = vibration_giggle_pattern,
    [VIBRATION_HEARTBEAT_STRONG] = vibration_heartbeat_strong,
    [VIBRATION_ERRATIC_STRONG] = vibration_erratic_strong
};

static const char* const vibration_pattern_names[] = {
    [VIBRATION_SHORT_BUZZ] = "SHORT_BUZZ",
    [VIBRATION_PURR_SHORT] = "PURR_SHORT", 
    [VIBRATION_PURR_PATTERN] = "PURR_PATTERN",
    [VIBRATION_GENTLE_HEARTBEAT] = "GENTLE_HEARTBEAT",
    [VIBRATION_STRUGGLE_PATTERN] = "STRUGGLE_PATTERN",
    [VIBRATION_SHARP_BUZZ] = "SHARP_BUZZ",
    [VIBRATION_TREMBLE_PATTERN] = "TREMBLE_PATTERN",
    [VIBRATION_GIGGLE_PATTERN] = "GIGGLE_PATTERN",
    [VIBRATION_HEARTBEAT_STRONG] = "HEARTBEAT_STRONG",
    [VIBRATION_ERRATIC_STRONG] = "ERRATIC_STRONG",
    [VIBRATION_STOP] = "STOP"
};

// Vibration类实现
Vibration::Vibration(Pca9685* pca9685, uint8_t channel) 
    : pca9685_(pca9685),
      vibration_channel_(channel),
      vibration_queue_(nullptr),
      vibration_task_handle_(nullptr),
      initialized_(false),
      current_pattern_(VIBRATION_MAX),
      current_emotion_("neutral"),
      emotion_based_enabled_(true),
      test_button_pin_(VIBRATION_TEST_BUTTON_GPIO),  // GPIO11测试按键
      button_test_task_handle_(nullptr),
      current_test_pattern_(VIBRATION_SHORT_BUZZ),  // 默认测试模式
      button_test_enabled_(false),
      cycle_test_mode_(false) {
}

Vibration::~Vibration() {
    if (initialized_) {
        // 停止振动
        Stop();
        
        // 禁用按键测试
        DisableButtonTest();
        
        // 删除任务
        if (vibration_task_handle_) {
            vTaskDelete(vibration_task_handle_);
            vibration_task_handle_ = nullptr;
        }
        
        // 删除队列
        if (vibration_queue_) {
            vQueueDelete(vibration_queue_);
            vibration_queue_ = nullptr;
        }
        
        initialized_ = false;
    }
}

esp_err_t Vibration::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "Vibration already initialized");
        return ESP_OK;
    }
    
    if (pca9685_ == nullptr) {
        ESP_LOGE(TAG, "PCA9685 pointer is null");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = InitVibrationPwm();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize vibration PWM");
        return ret;
    }
    
    initialized_ = true;
    return ESP_OK;
}

esp_err_t Vibration::StartTask() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Vibration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (vibration_task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Vibration task already started");
        return ESP_OK;
    }
    
    
    // 创建消息队列
    vibration_queue_ = xQueueCreate(VIBRATION_QUEUE_SIZE, sizeof(vibration_id_t));
    if (vibration_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create vibration queue");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建振动处理任务
    BaseType_t task_ret = xTaskCreate(
        VibrationTask,
        "vibration_task",
        VIBRATION_TASK_STACK_SIZE,
        this,
        VIBRATION_TASK_PRIORITY,
        &vibration_task_handle_
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create vibration task");
        vQueueDelete(vibration_queue_);
        vibration_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Vibration task started successfully on PCA9685 channel %d", vibration_channel_);
    return ESP_OK;
}

void Vibration::Play(vibration_id_t id) {
    if (!initialized_) {
        ESP_LOGE(TAG, "❌ Vibration not initialized, call Initialize() first");
        return;
    }
    
    if (vibration_queue_ == nullptr || vibration_task_handle_ == nullptr) {
        ESP_LOGW(TAG, "Vibration task not started, call StartTask() first");
        return;
    }
    
    if (id >= VIBRATION_MAX && id != VIBRATION_STOP) {
        ESP_LOGW(TAG, "⚠️ Invalid vibration ID: %d (max: %d)", id, VIBRATION_MAX-1);
        return;
    }
    
    // 非阻塞发送振动命令到队列
    BaseType_t ret = xQueueSend(vibration_queue_, &id, 0);
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Vibration queue full, pattern request dropped");
    } else {
        ESP_LOGD(TAG, "Queued vibration pattern: %s", vibration_pattern_names[id]);
    }
}

void Vibration::Stop() {
    Play(VIBRATION_STOP);
}

void Vibration::SetVibrationStrength(uint16_t strength) {
    if (pca9685_ == nullptr) {
        ESP_LOGE(TAG, "❌ PCA9685 is null, cannot set vibration strength");
        return;
    }
    
    // 确保强度值在有效范围内 (0-4095)
    if (strength > 4095) {
        strength = 4095;
    }

    pca9685_->SetPwm(vibration_channel_, strength);
}

void Vibration::PlayVibrationPattern(const vibration_keyframe_t* pattern) {
    if (!pattern || !initialized_) return;
    
    const vibration_keyframe_t* frame = pattern;
    
    pca9685_->IsDevicePresent();
    while (frame->duration_ms > 0 || frame->strength > 0) {
        // 设置振动强度
        SetVibrationStrength(frame->strength);
        
        // 等待指定时间
        if (frame->duration_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(frame->duration_ms));
        }
        
        frame++;
        
        // 检查是否收到停止命令
        vibration_id_t stop_cmd;
        if (xQueueReceive(vibration_queue_, &stop_cmd, 0) == pdTRUE) {
            if (stop_cmd == VIBRATION_STOP) {
                ESP_LOGI(TAG, "Received stop command, terminating current pattern");
                break;
            } else {
                // 如果收到新的振动命令，将其放回队列稍后处理
                xQueueSendToFront(vibration_queue_, &stop_cmd, 0);
                ESP_LOGI(TAG, "Received new pattern command while playing, will handle after current");
                break;
            }
        }
    }
    
    // 确保振动停止
    SetVibrationStrength(0);
}

esp_err_t Vibration::InitVibrationPwm() {
    if (pca9685_ == nullptr) {
        ESP_LOGE(TAG, "PCA9685 pointer is null");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查通道号是否有效
    if (vibration_channel_ > 15) {
        ESP_LOGE(TAG, "Invalid PWM channel: %d (max 15)", vibration_channel_);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化时关闭振动（设置PWM为0）
    pca9685_->SetPwm(vibration_channel_, 0);
    
    ESP_LOGI(TAG, "Vibration PWM initialized on PCA9685 channel %d", vibration_channel_);
    return ESP_OK;
}

esp_err_t Vibration::InitTestButton() {
    // 配置GPIO11为输入模式，启用下拉电阻
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << test_button_pin_);
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;  // 启用下拉，确保低电平
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure test button GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Test button initialized on GPIO%d", test_button_pin_);
    return ESP_OK;
}

void Vibration::VibrationTask(void* parameter) {
    // 添加nullptr检查
    if (parameter == nullptr) {
        ESP_LOGE(TAG, "VibrationTask received null parameter");
        vTaskDelete(NULL);
        return;
    }
    
    Vibration* skill = static_cast<Vibration*>(parameter);
    ESP_LOGI(TAG, "Vibration task started");
    
    vibration_id_t pattern_id;
    
    while (true) {
        // 检查队列是否有效
        if (skill->vibration_queue_ == nullptr) {
            ESP_LOGE(TAG, "Vibration queue is null, terminating task");
            vTaskDelete(NULL);
            return;
        }
        
        // 阻塞等待振动命令
        if (xQueueReceive(skill->vibration_queue_, &pattern_id, portMAX_DELAY) == pdTRUE) {
            
            if (pattern_id == VIBRATION_STOP) {
                skill->pca9685_->IsDevicePresent();
                ESP_LOGI(TAG, "🛑 Stopping all vibrations");
                skill->SetVibrationStrength(0);
                skill->current_pattern_ = VIBRATION_MAX;
                continue;
            }
            
            if (pattern_id >= VIBRATION_MAX) {
                ESP_LOGW(TAG, "⚠️ Invalid vibration pattern ID: %d", pattern_id);
                continue;
            }
            
            skill->current_pattern_ = pattern_id;
            ESP_LOGI(TAG, "🎵 Starting vibration pattern: %s", vibration_pattern_names[pattern_id]);
            
            // 获取对应的振动模式并播放
            const vibration_keyframe_t* pattern = vibration_patterns[pattern_id];
            if (pattern) {
                skill->PlayVibrationPattern(pattern);
            } else {
                ESP_LOGE(TAG, "❌ Pattern not found for ID: %d", pattern_id);
            }
            
            skill->current_pattern_ = VIBRATION_MAX;
            ESP_LOGD(TAG, "Finished playing pattern: %s", vibration_pattern_names[pattern_id]);
        }
    }
}

void Vibration::ButtonTestTask(void* parameter) {
    Vibration* skill = static_cast<Vibration*>(parameter);
    if (parameter == nullptr) {
        ESP_LOGE(TAG, "ButtonTestTask received null parameter");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Button test task started on GPIO%d", skill->test_button_pin_);
    
    bool last_button_state = false;
    bool current_button_state = false;
    
    while (skill->button_test_enabled_) {
        // 读取按键状态
        current_button_state = gpio_get_level(skill->test_button_pin_);
        
        // 检测按键按下（上升沿触发）
        if (current_button_state && !last_button_state) {
            if (skill->cycle_test_mode_) {
                ESP_LOGI(TAG, "🔘 Test button pressed! Starting cycle test of all patterns");
                
                // 循环测试所有振动模式
                for (int i = 0; i < VIBRATION_MAX; i++) {
                    vibration_id_t pattern_id = static_cast<vibration_id_t>(i);
                    
                    ESP_LOGI(TAG, "🎵 Preparing pattern %d/%d: %s", i + 1, VIBRATION_MAX, vibration_pattern_names[pattern_id]);
                    
                    // 播放音频提示
                    PlayBeepSound();
                    
                    ESP_LOGI(TAG, "▶️  Now playing vibration pattern: %s", vibration_pattern_names[pattern_id]);
                    
                    // 触发振动模式
                    skill->Play(pattern_id);
                    
                    // 等待振动结束（给足够时间让模式播放完成）
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    
                    // 2秒间隔
                    if (i < VIBRATION_MAX - 1) {  // 最后一个模式后不需要等待
                        ESP_LOGI(TAG, "⏳ Waiting 2 seconds before next pattern...");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    }
                }
                
                ESP_LOGI(TAG, "✅ Cycle test completed!");
            } else {
                ESP_LOGI(TAG, "🔘 Test button pressed! Playing pattern: %s", 
                        vibration_pattern_names[skill->current_test_pattern_]);
                
                // 触发当前测试振动模式
                skill->Play(skill->current_test_pattern_);
            }
        }
        
        last_button_state = current_button_state;
        
        // 短暂延迟，避免CPU占用过高
        vTaskDelay(pdMS_TO_TICKS(50));  // 20Hz检测频率
    }
    
    ESP_LOGI(TAG, "Button test task stopped");
    vTaskDelete(NULL);
}

void Vibration::EnableButtonTest(vibration_id_t pattern_id, bool cycle_test) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Vibration not initialized");
        return;
    }
    
    if (!cycle_test && pattern_id >= VIBRATION_MAX) {
        ESP_LOGW(TAG, "Invalid pattern ID for button test: %d", pattern_id);
        return;
    }
    
    if (button_test_enabled_) {
        ESP_LOGW(TAG, "Button test already enabled");
        return;
    }
    
    // 202509屏蔽可能的GPIO操作，初始化测试按键
    //esp_err_t ret = InitTestButton();
    //if (ret != ESP_OK) {
    //    ESP_LOGE(TAG, "Failed to initialize test button");
    //    return;
    //}
    
    current_test_pattern_ = pattern_id;
    button_test_enabled_ = true;
    cycle_test_mode_ = cycle_test;
    
    // 根据测试模式选择任务名称和栈大小
    const char* task_name = cycle_test ? "button_cycle_test_task" : "button_test_task";
    uint32_t stack_size = cycle_test ? 4096 : 2048;  // 循环测试需要更多栈空间
    
    // 创建按键测试任务
    BaseType_t task_ret = xTaskCreate(
        ButtonTestTask,
        task_name,
        stack_size,
        this,
        2,  // 中等优先级
        &button_test_task_handle_
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button test task");
        button_test_enabled_ = false;
        return;
    }
    
    if (cycle_test) {
        ESP_LOGI(TAG, "Button cycle test enabled - press button to test all %d patterns", VIBRATION_MAX);
    } else {
        ESP_LOGI(TAG, "Button test enabled with pattern: %s", vibration_pattern_names[pattern_id]);
    }
}

void Vibration::DisableButtonTest() {
    if (!button_test_enabled_) {
        return;
    }
    
    button_test_enabled_ = false;
    
    // 等待任务自然结束
    if (button_test_task_handle_) {
        vTaskDelay(pdMS_TO_TICKS(100));  // 给任务时间退出
        button_test_task_handle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Button test disabled");
}

void Vibration::SetTestPattern(vibration_id_t pattern_id) {
    if (pattern_id >= VIBRATION_MAX) {
        ESP_LOGW(TAG, "Invalid pattern ID: %d", pattern_id);
        return;
    }
    
    current_test_pattern_ = pattern_id;
    ESP_LOGI(TAG, "Test pattern changed to: %s", vibration_pattern_names[pattern_id]);
}