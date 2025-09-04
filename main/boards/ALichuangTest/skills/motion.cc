#include "motion.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <cmath>

static const char* TAG = "Motion";

// 动作参数配置
#define MOTION_QUEUE_SIZE           10
#define MOTION_TASK_STACK_SIZE      4096
#define MOTION_TASK_PRIORITY        5

// PWM控制参数
#define PWM_MAX_VALUE              4095
#define PWM_CENTER_VALUE           (PWM_MAX_VALUE / 2)
#define ANGLE_MAX                  90.0f
#define ANGLE_MIN                  -90.0f

// 速度参数 (ms)
#define SPEED_SLOW_DELAY           50
#define SPEED_MEDIUM_DELAY         20
#define SPEED_FAST_DELAY           10

// 马达参数（242转减速马达）
#define MOTOR_RPM                  242.0f    // 马达转速：242转/分钟
#define MOTOR_RPS                  (MOTOR_RPM / 60.0f)  // 4.03转/秒
#define MOTOR_DEGREES_PER_SECOND   (MOTOR_RPS * 360.0f) // 1450.8度/秒
#define MOTOR_MIN_SPEED_PWM        600       // 最低速度PWM值（启动扭矩）
#define MOTOR_MAX_SPEED_PWM        4095      // 最高速度PWM值
#define MOTOR_SPEED_SLOW_PWM       400       // 慢速PWM值
#define MOTOR_SPEED_MEDIUM_PWM     1200      // 中速PWM值
#define MOTOR_SPEED_FAST_PWM       2000      // 快速PWM值

// 角度控制精度参数
#define ANGLE_TOLERANCE            0.5f      // 角度控制精度（度）
#define POSITION_UPDATE_INTERVAL   10        // 位置更新间隔(ms)
#define MOTOR_BRAKE_TIME           100       // 制动时间(ms)

static const char* const motion_pattern_names[] = {
    [MOTION_HAPPY_WIGGLE] = "HAPPY_WIGGLE",
    [MOTION_SHAKE_HEAD] = "SHAKE_HEAD", 
    [MOTION_DODGE_SUBTLE] = "DODGE_SUBTLE",
    [MOTION_NUZZLE_FORWARD] = "NUZZLE_FORWARD",
    [MOTION_TENSE_UP] = "TENSE_UP",
    [MOTION_DODGE_SLOWLY] = "DODGE_SLOWLY",
    [MOTION_QUICK_TURN_LEFT] = "QUICK_TURN_LEFT",
    [MOTION_QUICK_TURN_RIGHT] = "QUICK_TURN_RIGHT",
    [MOTION_CURIOUS_PEEK_LEFT] = "CURIOUS_PEEK_LEFT",
    [MOTION_CURIOUS_PEEK_RIGHT] = "CURIOUS_PEEK_RIGHT",
    [MOTION_SLOW_TURN_LEFT] = "SLOW_TURN_LEFT",
    [MOTION_SLOW_TURN_RIGHT] = "SLOW_TURN_RIGHT",
    [MOTION_DODGE_OPPOSITE_LEFT] = "DODGE_OPPOSITE_LEFT",
    [MOTION_DODGE_OPPOSITE_RIGHT] = "DODGE_OPPOSITE_RIGHT",
    [MOTION_BODY_SHIVER] = "BODY_SHIVER",
    [MOTION_EXCITED_JIGGLE] = "EXCITED_JIGGLE",
    [MOTION_RELAX_COMPLETELY] = "RELAX_COMPLETELY",
    [MOTION_TICKLE_TWIST_DANCE] = "TICKLE_TWIST_DANCE",
    [MOTION_ANNOYED_TWIST_TO_HAPPY] = "ANNOYED_TWIST_TO_HAPPY",
    [MOTION_STRUGGLE_TWIST] = "STRUGGLE_TWIST",
    [MOTION_UNWILLING_TURN_BACK] = "UNWILLING_TURN_BACK",
    [MOTION_RELAX_TO_CENTER] = "RELAX_TO_CENTER",
};

Motion::Motion(Pca9685* pca9685, uint8_t channel_a, uint8_t channel_b)
    : pca9685_(pca9685), channel_a_(channel_a), channel_b_(channel_b),
      motion_task_handle_(nullptr), command_queue_(nullptr), 
      task_running_(false), is_busy_(false), current_angle_(0.0f),
      target_angle_(0.0f), motor_enabled_(false), current_speed_pwm_(0) {
}

Motion::~Motion() {
    StopTask();
}

esp_err_t Motion::Initialize() {
    if (!pca9685_) {
        ESP_LOGE(TAG, "PCA9685 instance is null");
        return ESP_ERR_INVALID_ARG;
    }

    // 创建命令队列
    command_queue_ = xQueueCreate(MOTION_QUEUE_SIZE, sizeof(motion_command_t));
    if (!command_queue_) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return ESP_ERR_NO_MEM;
    }

    // 初始化马达到中心位置
    StopMotor();
    current_angle_ = 0.0f;

    ESP_LOGI(TAG, "Motion system initialized with PCA9685 channels %d and %d", 
             channel_a_, channel_b_);

    return ESP_OK;
}

esp_err_t Motion::StartTask() {
    if (task_running_) {
        ESP_LOGW(TAG, "Motion task already running");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        MotionTaskFunction,
        "motion_task",
        MOTION_TASK_STACK_SIZE,
        this,
        MOTION_TASK_PRIORITY,
        &motion_task_handle_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motion task");
        return ESP_ERR_NO_MEM;
    }

    task_running_ = true;
    ESP_LOGI(TAG, "Motion task started");
    
    return ESP_OK;
}

void Motion::StopTask() {
    if (!task_running_) {
        return;
    }

    // 发送关闭命令
    motion_command_t cmd = {.type = CMD_SHUTDOWN};
    xQueueSend(command_queue_, &cmd, portMAX_DELAY);

    // 等待任务结束
    if (motion_task_handle_) {
        while (eTaskGetState(motion_task_handle_) != eDeleted) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        motion_task_handle_ = nullptr;
    }

    task_running_ = false;
    ESP_LOGI(TAG, "Motion task stopped");
}

void Motion::Perform(motion_id_t id) {
    if (!task_running_) {
        ESP_LOGW(TAG, "Motion task not running, starting it now");
        if (StartTask() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start motion task");
            return;
        }
    }

    motion_command_t cmd = {
        .type = CMD_PERFORM_MOTION,
        .data = {.motion_id = id}
    };

    if (xQueueSend(command_queue_, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full, motion may be ignored");
    }
}

void Motion::SetAngle(float angle, motion_speed_t speed) {
    if (!task_running_) {
        ESP_LOGW(TAG, "Motion task not running, starting it now");
        if (StartTask() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start motion task");
            return;
        }
    }

    // 限制角度范围
    if (angle > ANGLE_MAX) angle = ANGLE_MAX;
    if (angle < ANGLE_MIN) angle = ANGLE_MIN;

    motion_command_t cmd = {
        .type = CMD_SET_ANGLE,
        .data = {.angle_cmd = {.angle = angle, .speed = speed}}
    };

    if (xQueueSend(command_queue_, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full, angle command may be ignored");
    }
}

bool Motion::IsBusy() {
    return is_busy_;
}

void Motion::Stop() {
    motion_command_t cmd = {.type = CMD_STOP};
    if (command_queue_) {
        xQueueSend(command_queue_, &cmd, 0);
    }
}

void Motion::MotionTaskFunction(void* arg) {
    Motion* motion = static_cast<Motion*>(arg);
    motion_command_t cmd;

    while (true) {
        if (xQueueReceive(motion->command_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case CMD_PERFORM_MOTION:
                    motion->is_busy_ = true;
                    motion->ExecuteMotionSequence(cmd.data.motion_id);
                    motion->is_busy_ = false;
                    break;

                case CMD_SET_ANGLE:
                    motion->is_busy_ = true;
                    motion->MotorTurnToAngle(cmd.data.angle_cmd.angle, cmd.data.angle_cmd.speed);
                    motion->is_busy_ = false;
                    break;

                case CMD_STOP:
                    motion->StopMotor();
                    motion->is_busy_ = false;
                    break;

                case CMD_SHUTDOWN:
                    motion->StopMotor();
                    motion->is_busy_ = false;
                    ESP_LOGI(TAG, "Motion task shutting down");
                    motion->task_running_ = false;
                    vTaskDelete(nullptr);
                    return;
            }
        }
    }
}

void Motion::MotorTurnToAngle(float target_angle, motion_speed_t speed) {
    if (!pca9685_) return;
    pca9685_->IsDevicePresent();
    
    // 限制目标角度
    if (target_angle > ANGLE_MAX) target_angle = ANGLE_MAX;
    if (target_angle < ANGLE_MIN) target_angle = ANGLE_MIN;
    
    target_angle_ = target_angle;
    float angle_diff = target_angle - current_angle_;
    
    // ESP_LOGI(TAG, "精确转动: 从%.1f°到%.1f° (差值%.1f°)", 
    //          current_angle_, target_angle, angle_diff);
    
    // 如果角度差异很小，直接返回
    if (std::abs(angle_diff) < ANGLE_TOLERANCE) {
        ESP_LOGI(TAG, "角度差异小于容差，无需转动");
        return;
    }
    
    // 对于极小角度，给出警告但仍然执行
    if (std::abs(angle_diff) < 2.0f) {
        ESP_LOGW(TAG, "⚠️  角度很小(%.1f°)，可能存在精度问题", angle_diff);
    }
    
    // 确定转动方向
    int8_t direction = (angle_diff > 0) ? 1 : -1;
    
    // 获取速度PWM值
    uint16_t speed_pwm = GetSpeedPwm(speed);
    
    // 计算转动时间
    uint32_t rotation_time = CalculateRotationTime(angle_diff, speed);
    
    // 开始转动
    SetMotorSpeed(direction, speed_pwm);
    
    // 转动指定时间
    vTaskDelay(pdMS_TO_TICKS(rotation_time));
    
    // 停止马达
    SetMotorSpeed(0, 0);
    
    // 更新当前角度
    current_angle_ = target_angle;
    
    // 制动时间，确保马达完全停止
    vTaskDelay(pdMS_TO_TICKS(MOTOR_BRAKE_TIME));
    
    ESP_LOGI(TAG, "转动完成: 当前角度=%.1f°", current_angle_);
}

void Motion::ExecuteMotionSequence(motion_id_t motion_id) {
    ESP_LOGI(TAG, "Executing motion sequence: %s", motion_pattern_names[motion_id]);

    switch (motion_id) {
        case MOTION_HAPPY_WIGGLE: {
            // 表达"开心"的、小幅度的、快速的左右摇摆
            for (int i = 0; i < 3; i++) {
                MotorTurnToAngle(10.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
                MotorTurnToAngle(-10.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_SHAKE_HEAD: {
            // 表达"不同意"或"烦躁"的、清晰的摇头动作
            for (int i = 0; i < 2; i++) {
                MotorTurnToAngle(30.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(200));
                MotorTurnToAngle(-30.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_DODGE_SUBTLE: {
            // 快速、小幅度的躲闪，然后缓慢恢复
            MotorTurnToAngle(15.0f, MOTION_SPEED_FAST);
            vTaskDelay(pdMS_TO_TICKS(300));
            MotorTurnToAngle(0.0f, MOTION_SPEED_SLOW);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_NUZZLE_FORWARD: {
            // 表达"亲昵"的、主动向前"蹭"的动作
            MotorTurnToAngle(20.0f, MOTION_SPEED_SLOW);
            vTaskDelay(pdMS_TO_TICKS(500));
            MotorTurnToAngle(0.0f, MOTION_SPEED_SLOW);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_TENSE_UP: {
            // 表达"紧张"或"害怕"的、身体瞬间绷紧的感觉
            // 通过短暂的高频小幅抖动模拟紧张
            for (int i = 0; i < 10; i++) {
                MotorTurnToAngle(5.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(30));
                MotorTurnToAngle(-5.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(30));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_FAST);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_DODGE_SLOWLY: {
            // 在不情愿的状态下，缓慢地躲开
            MotorTurnToAngle(20.0f, MOTION_SPEED_SLOW);
            vTaskDelay(pdMS_TO_TICKS(800));
            StopMotor(); // 确保马达停止在偏移位置
            break;
        }

        case MOTION_QUICK_TURN_LEFT: {
            // 快速、精准地转到左侧 ok
            MotorTurnToAngle(-30.0f, MOTION_SPEED_FAST);
            vTaskDelay(pdMS_TO_TICKS(300));
            StopMotor(); // 确保马达停止在转向位置
            break;
        }

        case MOTION_QUICK_TURN_RIGHT: {
            // 快速、精准地转到右侧 ok
            MotorTurnToAngle(30.0f, MOTION_SPEED_FAST);
            vTaskDelay(pdMS_TO_TICKS(300));
            StopMotor(); // 确保马达停止在转向位置
            break;
        }

        case MOTION_CURIOUS_PEEK_LEFT: {
            // 模拟"探头探脑"的好奇窥探动作 - 左侧
            MotorTurnToAngle(-25.0f, MOTION_SPEED_MEDIUM);  // 转动
            vTaskDelay(pdMS_TO_TICKS(400));                 // 停顿
            // 小幅晃动
            for (int i = 0; i < 2; i++) {
                MotorTurnToAngle(-20.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
                MotorTurnToAngle(-30.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);    // 回正
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_CURIOUS_PEEK_RIGHT: {
            // 模拟"探头探脑"的好奇窥探动作 - 右侧
            MotorTurnToAngle(25.0f, MOTION_SPEED_MEDIUM);   // 转动
            vTaskDelay(pdMS_TO_TICKS(400));                 // 停顿
            // 小幅晃动
            for (int i = 0; i < 2; i++) {
                MotorTurnToAngle(20.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
                MotorTurnToAngle(30.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);    // 回正
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_SLOW_TURN_LEFT: {
            // 慵懒地、慢悠悠地看一眼左侧
            MotorTurnToAngle(-20.0f, MOTION_SPEED_SLOW);
            vTaskDelay(pdMS_TO_TICKS(600));
            MotorTurnToAngle(0.0f, MOTION_SPEED_SLOW);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_SLOW_TURN_RIGHT: {
            // 慵懒地、慢悠悠地看一眼右侧
            MotorTurnToAngle(20.0f, MOTION_SPEED_SLOW);
            vTaskDelay(pdMS_TO_TICKS(600));
            MotorTurnToAngle(0.0f, MOTION_SPEED_SLOW);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_DODGE_OPPOSITE_LEFT: {
            // 被左侧触摸后，迅速向右侧躲开
            MotorTurnToAngle(25.0f, MOTION_SPEED_FAST);
            vTaskDelay(pdMS_TO_TICKS(400));
            StopMotor(); // 确保马达停止在躲避位置
            break;
        }

        case MOTION_DODGE_OPPOSITE_RIGHT: {
            // 被右侧触摸后，迅速向左侧躲开
            MotorTurnToAngle(-25.0f, MOTION_SPEED_FAST);
            vTaskDelay(pdMS_TO_TICKS(400));
            StopMotor(); // 确保马达停止在躲避位置
            break;
        }

        case MOTION_BODY_SHIVER: {
            // 表达"被打扰"或"冷"的、快速、小幅的身体抖动
            for (int i = 0; i < 5; i++) {
                MotorTurnToAngle(5.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(50));
                MotorTurnToAngle(-5.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_EXCITED_JIGGLE: {
            // 极度兴奋的、原地快速晃动 ok
            for (int i = 0; i < 3; i++) {
                MotorTurnToAngle(15.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(80));
                MotorTurnToAngle(-15.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_RELAX_COMPLETELY: {
            // 完全放松，降低到松弛状态
            MotorTurnToAngle(0.0f, MOTION_SPEED_SLOW);
            vTaskDelay(pdMS_TO_TICKS(200));
            // 通过降低PWM值模拟放松
            SetMotorPwm(PWM_CENTER_VALUE - 100, PWM_CENTER_VALUE + 100);
            vTaskDelay(pdMS_TO_TICKS(1000));
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_TICKLE_TWIST_DANCE: {
            // 无法控制的、开心的、大幅度的来回扭动
            for (int i = 0; i < 5; i++) {
                MotorTurnToAngle(40.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(120));
                MotorTurnToAngle(-40.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(120));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_ANNOYED_TWIST_TO_HAPPY: {
            // 从烦躁的扭动，逐渐过渡到开心的扭动
            // 初始不耐烦的动作
            for (int i = 0; i < 2; i++) {
                MotorTurnToAngle(25.0f, MOTION_SPEED_MEDIUM);
                vTaskDelay(pdMS_TO_TICKS(150));
                MotorTurnToAngle(-25.0f, MOTION_SPEED_MEDIUM);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            // 过渡到开心的动作
            for (int i = 0; i < 3; i++) {
                MotorTurnToAngle(20.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
                MotorTurnToAngle(-20.0f, MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            MotorTurnToAngle(0.0f, MOTION_SPEED_MEDIUM);
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_STRUGGLE_TWIST: {
            // 表达"慌乱挣扎"的、不规则的扭动
            int angles[] = {35, -20, 25, -40, 15, -30, 0};
            for (int i = 0; i < 7; i++) {
                MotorTurnToAngle(angles[i], MOTION_SPEED_FAST);
                vTaskDelay(pdMS_TO_TICKS(100 + (i % 3) * 50));
            }
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_UNWILLING_TURN_BACK: {
            // 从偏移角度，不情愿地、带停顿地恢复到中心
            float steps[] = {current_angle_ * 0.8f, current_angle_ * 0.6f, 
                           current_angle_ * 0.4f, current_angle_ * 0.2f, 0.0f};
            for (int i = 0; i < 5; i++) {
                MotorTurnToAngle(steps[i], MOTION_SPEED_SLOW);
                vTaskDelay(pdMS_TO_TICKS(200 + i * 100));
                // 添加小的反向运动表现不情愿
                if (i < 4) {
                    float temp_angle = current_angle_ + (current_angle_ > 0 ? -3.0f : 3.0f);
                    MotorTurnToAngle(temp_angle, MOTION_SPEED_FAST);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
            StopMotor(); // 确保马达停止
            break;
        }

        case MOTION_RELAX_TO_CENTER: {
            // 从偏移角度，非常放松、平滑地恢复到中心
            MotorTurnToAngle(0.0f, MOTION_SPEED_SLOW);
            vTaskDelay(pdMS_TO_TICKS(300));
            StopMotor(); // 确保马达停止
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown motion ID: %d", motion_id);
            break;
    }

    ESP_LOGI(TAG, "Motion sequence %d completed", motion_id);
}

void Motion::SetMotorPwm(uint16_t pwm_a, uint16_t pwm_b) {
    if (!pca9685_) {
        ESP_LOGE(TAG, "PCA9685 instance is null!");
        return;
    }

    // 限制PWM值范围
    if (pwm_a > PWM_MAX_VALUE) pwm_a = PWM_MAX_VALUE;
    if (pwm_b > PWM_MAX_VALUE) pwm_b = PWM_MAX_VALUE;

    //ESP_LOGI(TAG, "设置PWM: 通道%d=%d, 通道%d=%d", channel_a_, pwm_a, channel_b_, pwm_b);

    pca9685_->SetPwm(channel_a_, pwm_a);
    pca9685_->SetPwm(channel_b_, pwm_b);
    
    // 添加短暂延时确保信号稳定
    vTaskDelay(pdMS_TO_TICKS(10));
}

void Motion::StopMotor() {
    if (!pca9685_) return;
    
    ESP_LOGI(TAG, "停止马达运动");
    
    // 立即停止马达
    pca9685_->TurnOff(channel_a_);
    pca9685_->TurnOff(channel_b_);
    
    motor_enabled_ = false;
    current_speed_pwm_ = 0;
    
    ESP_LOGI(TAG, "马达已停止在角度: %.1f°", current_angle_);
}

void Motion::AngleToPwm(float angle, uint16_t& pwm_a, uint16_t& pwm_b) {
    // 限制角度范围
    if (angle > ANGLE_MAX) angle = ANGLE_MAX;
    if (angle < ANGLE_MIN) angle = ANGLE_MIN;

    // DRV883X直流马达驱动逻辑：
    // 正角度：IN1=HIGH, IN2=LOW - 正转
    // 负角度：IN1=LOW, IN2=HIGH - 反转  
    // 0度：IN1=LOW, IN2=LOW - 停止
    
    if (angle > 0) {
        // 正向转动 - IN1=HIGH, IN2=LOW
        pwm_a = current_speed_pwm_;  // IN1 = speed PWM
        pwm_b = 0;                   // IN2 = LOW
        ESP_LOGD(TAG, "正转: 角度=%.1f°, PWM_A=%d, PWM_B=%d", angle, pwm_a, pwm_b);
    } else if (angle < 0) {
        // 反向转动 - IN1=LOW, IN2=HIGH
        pwm_a = 0;                   // IN1 = LOW  
        pwm_b = current_speed_pwm_;  // IN2 = speed PWM
        ESP_LOGD(TAG, "反转: 角度=%.1f°, PWM_A=%d, PWM_B=%d", angle, pwm_a, pwm_b);
    } else {
        // 停止 - IN1=LOW, IN2=LOW
        pwm_a = 0;              // IN1 = LOW
        pwm_b = 0;              // IN2 = LOW
        ESP_LOGD(TAG, "停止: PWM_A=%d, PWM_B=%d", pwm_a, pwm_b);
    }
}

uint32_t Motion::GetSpeedDelay(motion_speed_t speed) {
    switch (speed) {
        case MOTION_SPEED_SLOW:
            return SPEED_SLOW_DELAY;
        case MOTION_SPEED_MEDIUM:
            return SPEED_MEDIUM_DELAY;
        case MOTION_SPEED_FAST:
            return SPEED_FAST_DELAY;
        default:
            return SPEED_MEDIUM_DELAY;
    }
}

uint16_t Motion::GetSpeedPwm(motion_speed_t speed) {
    uint16_t pwm_value;
    
    switch (speed) {
        case MOTION_SPEED_SLOW:
            pwm_value = MOTOR_SPEED_SLOW_PWM;
            break;
        case MOTION_SPEED_MEDIUM:
            pwm_value = MOTOR_SPEED_MEDIUM_PWM;
            break;
        case MOTION_SPEED_FAST:
            pwm_value = MOTOR_SPEED_FAST_PWM;
            break;
        default:
            pwm_value = MOTOR_SPEED_MEDIUM_PWM;
            break;
    }
    
    return pwm_value;
}

void Motion::SetMotorSpeed(int8_t direction, uint16_t speed_pwm) {
    if (!pca9685_) return;
    
    // 限制PWM值范围
    if (speed_pwm > PWM_MAX_VALUE) speed_pwm = PWM_MAX_VALUE;
    if (speed_pwm > 0 && speed_pwm < MOTOR_MIN_SPEED_PWM) {
        speed_pwm = MOTOR_MIN_SPEED_PWM; // 确保足够的启动扭矩
    }
    
    current_speed_pwm_ = speed_pwm;
    
    if (direction > 0) {
        // 正向转动
        pca9685_->SetPwm(channel_a_, speed_pwm);
        pca9685_->SetPwm(channel_b_, 0);
        motor_enabled_ = true;
        ESP_LOGD(TAG, "正向转动: 速度PWM=%d", speed_pwm);
    } else if (direction < 0) {
        // 反向转动
        pca9685_->SetPwm(channel_a_, 0);
        pca9685_->SetPwm(channel_b_, speed_pwm);
        motor_enabled_ = true;
        ESP_LOGD(TAG, "反向转动: 速度PWM=%d", speed_pwm);
    } else {
        // 停止
        pca9685_->TurnOff(channel_a_);
        pca9685_->TurnOff(channel_b_);
        motor_enabled_ = false;
        current_speed_pwm_ = 0;
        ESP_LOGD(TAG, "马达停止");
    }
}

uint32_t Motion::CalculateRotationTime(float angle_diff, motion_speed_t speed) {
    // 基于242转/分钟减速马达的精确计算
    // 242转/分钟 = 4.03转/秒 = 1450.8度/秒 (在满速PWM=4095时)
    
    // 获取当前速度的PWM值和对应的速度比例
    uint16_t speed_pwm = GetSpeedPwm(speed);
    float speed_ratio = (float)speed_pwm / MOTOR_MAX_SPEED_PWM;  // PWM占空比
    
    // 计算当前速度下的度/秒
    float degrees_per_second = MOTOR_DEGREES_PER_SECOND * speed_ratio;
    
    // 计算转动时间 (ms)
    uint32_t rotation_time = (uint32_t)((std::abs(angle_diff) / degrees_per_second) * 1000.0f);
    
    // 设置合理的时间范围 - 直流马达必须使用精确计算的时间
    if (rotation_time < 5) rotation_time = 5;        // 最小5ms，仅防止系统延迟
    if (rotation_time > 5000) rotation_time = 5000;  // 最大5秒
    
    return rotation_time;
}
