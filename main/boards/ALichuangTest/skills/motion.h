#ifndef SKILLS_MOTION_H_
#define SKILLS_MOTION_H_

#include "../pca9685.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_err.h>
#include <stdint.h>

// 所有预设的、声明式的动作ID
typedef enum {
    MOTION_HAPPY_WIGGLE,
    MOTION_SHAKE_HEAD,
    MOTION_DODGE_SUBTLE,
    MOTION_NUZZLE_FORWARD,
    MOTION_TENSE_UP,
    MOTION_DODGE_SLOWLY,
    MOTION_QUICK_TURN_LEFT,
    MOTION_QUICK_TURN_RIGHT,
    MOTION_CURIOUS_PEEK_LEFT,
    MOTION_CURIOUS_PEEK_RIGHT,
    MOTION_SLOW_TURN_LEFT,
    MOTION_SLOW_TURN_RIGHT,
    MOTION_DODGE_OPPOSITE_LEFT,
    MOTION_DODGE_OPPOSITE_RIGHT,
    MOTION_BODY_SHIVER,
    MOTION_EXCITED_JIGGLE,
    MOTION_RELAX_COMPLETELY,
    MOTION_TICKLE_TWIST_DANCE,
    MOTION_ANNOYED_TWIST_TO_HAPPY,
    MOTION_STRUGGLE_TWIST,
    MOTION_UNWILLING_TURN_BACK,
    MOTION_RELAX_TO_CENTER,
} motion_id_t;

// 用于命令式控制的速度枚举
typedef enum {
    MOTION_SPEED_SLOW,
    MOTION_SPEED_MEDIUM,
    MOTION_SPEED_FAST,
} motion_speed_t;

/**
 * @brief 直流马达动作控制类
 *        使用PCA9685控制DRV883X驱动板来驱动直流马达
 *        支持预设动作序列和精确角度控制
 */
class Motion {
public:
    /**
     * @brief 构造函数
     * @param pca9685 PCA9685控制器实例指针
     * @param channel_a DRV883X的IN1引脚连接的PCA9685通道 (默认1)
     * @param channel_b DRV883X的IN2引脚连接的PCA9685通道 (默认2)
     */
    Motion(Pca9685* pca9685, uint8_t channel_a = 1, uint8_t channel_b = 2);

    /**
     * @brief 析构函数
     */
    ~Motion();

    /**
     * @brief 初始化身体动作技能模块
     *        内部会创建并启动一个后台FreeRTOS任务来处理所有动作
     * @return ESP_OK 如果成功，其他错误码如果失败
     */
    esp_err_t Initialize();

    /**
     * @brief 【声明式接口】执行一个预设的、复杂的动作序列
     *        此函数会立即返回 (非阻塞)。动作将在后台任务中执行。
     *        如果后台任务正在执行上一个动作，此新动作会覆盖它。
     * 
     * @param id 要执行的动作ID
     */
    void Perform(motion_id_t id);

    /**
     * @brief 【命令式接口】控制身体转到一个精确的静态角度
     *        此函数会立即返回 (非阻塞)。马达将在后台任务中平滑地转动到目标角度。
     * 
     * @param angle 目标角度 (-90.0 to 90.0)
     * @param speed 转动速度
     */
    void SetAngle(float angle, motion_speed_t speed);

    /**
     * @brief 查询身体当前是否正在执行一个动作
     * 
     * @return true 如果后台任务正忙, false 如果已空闲
     */
    bool IsBusy();

    /**
     * @brief 立即停止当前所有身体动作，并保持在当前位置
     */
    void Stop();

    /**
     * @brief 启动后台任务（如果需要外部控制任务生命周期）
     * @return ESP_OK 如果成功启动任务
     */
    esp_err_t StartTask();

    /**
     * @brief 停止后台任务（如果需要外部控制任务生命周期）
     */
    void StopTask();

private:
    // 硬件控制相关
    Pca9685* pca9685_;              // PCA9685控制器
    uint8_t channel_a_;             // DRV8837 IN1通道
    uint8_t channel_b_;             // DRV8837 IN2通道
    
    // 任务控制相关
    TaskHandle_t motion_task_handle_;   // 后台任务句柄
    QueueHandle_t command_queue_;       // 命令队列
    bool task_running_;                 // 任务运行状态
    bool is_busy_;                      // 是否正在执行动作
    
    // 当前状态
    float current_angle_;               // 当前角度位置
    float target_angle_;                // 目标角度位置
    bool motor_enabled_;                // 马达使能状态
    uint16_t current_speed_pwm_;        // 当前速度PWM值
    
    // 内部命令结构
    typedef enum {
        CMD_PERFORM_MOTION,
        CMD_SET_ANGLE,
        CMD_STOP,
        CMD_SHUTDOWN
    } command_type_t;
    
    typedef struct {
        command_type_t type;
        union {
            motion_id_t motion_id;
            struct {
                float angle;
                motion_speed_t speed;
            } angle_cmd;
        } data;
    } motion_command_t;

    /**
     * @brief 后台任务函数
     * @param arg Motion实例指针
     */
    static void MotionTaskFunction(void* arg);

    /**
     * @brief 控制马达转动到指定角度
     * @param target_angle 目标角度 (-90 到 +90)
     * @param speed 转动速度
     */
    void MotorTurnToAngle(float target_angle, motion_speed_t speed);

    /**
     * @brief 执行预设动作序列
     * @param motion_id 动作ID
     */
    void ExecuteMotionSequence(motion_id_t motion_id);

    /**
     * @brief 设置马达PWM控制值
     * @param pwm_a 通道A的PWM值 (0-4095)
     * @param pwm_b 通道B的PWM值 (0-4095)
     */
    void SetMotorPwm(uint16_t pwm_a, uint16_t pwm_b);
    
    /**
     * @brief 设置马达速度和方向
     * @param direction 转动方向 (1=正转, -1=反转, 0=停止)
     * @param speed_pwm 速度PWM值 (0-4095)
     */
    void SetMotorSpeed(int8_t direction, uint16_t speed_pwm);

    /**
     * @brief 停止马达
     */
    void StopMotor();

    /**
     * @brief 角度到PWM值的转换
     * @param angle 角度 (-90 到 +90)
     * @param pwm_a 输出通道A的PWM值
     * @param pwm_b 输出通道B的PWM值
     */
    void AngleToPwm(float angle, uint16_t& pwm_a, uint16_t& pwm_b);

    /**
     * @brief 获取速度对应的延时时间
     * @param speed 速度枚举
     * @return 延时时间(ms)
     */
    uint32_t GetSpeedDelay(motion_speed_t speed);
    
    /**
     * @brief 获取速度对应的PWM值
     * @param speed 速度枚举
     * @return PWM值 (0-4095)
     */
    uint16_t GetSpeedPwm(motion_speed_t speed);
    
    /**
     * @brief 计算目标角度需要的转动时间
     * @param angle_diff 角度差值 (度)
     * @param speed 速度枚举
     * @return 转动时间(ms)
     */
    uint32_t CalculateRotationTime(float angle_diff, motion_speed_t speed);
    
    /**
     * @brief 运行速度对比测试 - 展示快速和慢速的差异
     */
    void RunSpeedTest();
};

#endif // SKILLS_MOTION_H_