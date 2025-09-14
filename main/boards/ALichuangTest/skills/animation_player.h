#ifndef ANIMATION_PLAYER_H
#define ANIMATION_PLAYER_H

#include <string>
#include <atomic>
#include <vector>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <lvgl.h>
#include <esp_lcd_types.h>

// Include necessary enums and classes
#include "../../device_state.h"
#include "../interaction/core/event_engine.h"
#include "../interaction/core/emotion_engine.h"

// Forward declarations
class AnimationLoader;
// enum SystemEventType;  // 暂时注释，未来实现

/**
 * @brief 动画播放器 - 统一的动画播放解决方案
 *
 * 整合了原AnimaDisplay的画布功能，提供简单的事件驱动动画播放
 * 支持自动VA象限判断和路径映射
 */
class AnimationPlayer {
public:
    /**
     * @brief 获取单例实例
     */
    static AnimationPlayer& GetInstance();

    /**
     * @brief 初始化动画播放器
     * @param panel_io LCD面板IO句柄
     * @param panel LCD面板句柄
     * @param width 屏幕宽度
     * @param height 屏幕高度
     * @param offset_x X偏移
     * @param offset_y Y偏移
     * @param mirror_x X轴镜像
     * @param mirror_y Y轴镜像
     * @param swap_xy 交换XY轴
     * @return true 初始化成功, false 初始化失败
     */
    bool Initialize(esp_lcd_panel_io_handle_t panel_io,
                   esp_lcd_panel_handle_t panel,
                   int width, int height,
                   int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);

    /**
     * @brief 销毁动画播放器，清理资源
     */
    void Destroy();

    // === 核心播放接口 ===

    /**
     * @brief 播放事件动画（自动判断类型和象限）
     * @param event 事件类型
     */
    void Play(EventType event);

    /**
     * @brief 播放紧急事件动画
     * @param event 紧急事件类型
     */
    void PlayEmergency(EventType event);

    /**
     * @brief 播放交互事件动画（自动获取当前象限）
     * @param event 交互事件类型
     */
    void PlayInteraction(EventType event);

    /**
     * @brief 播放状态表达动画（自动获取当前象限）
     * @param state 设备状态
     */
    void PlayStateExpression(DeviceState state);

    /**
     * @brief 播放系统事件动画
     * @param event 系统事件类型
     */
    // void PlaySystem(SystemEventType event);  // 暂时注释，未来实现

    /**
     * @brief 停止当前动画
     */
    void Stop();

    /**
     * @brief 检查是否正在播放动画
     * @return true 正在播放, false 未播放
     */
    bool IsPlaying() const { return is_playing_.load(); }

    // === 画布管理（从AnimaDisplay迁移） ===

    /**
     * @brief 创建LVGL画布
     */
    void CreateCanvas();

    /**
     * @brief 销毁LVGL画布
     */
    void DestroyCanvas();

    /**
     * @brief 检查画布是否存在
     * @return true 画布存在, false 画布不存在
     */
    bool HasCanvas() const { return canvas_ != nullptr; }

    /**
     * @brief 在画布上绘制图像（从AnimaDisplay迁移）
     * @param x X坐标
     * @param y Y坐标
     * @param width 图像宽度
     * @param height 图像高度
     * @param img_data 图像数据（RGB565格式）
     */
    void DrawImageOnCanvas(int x, int y, int width, int height, const uint8_t* img_data);

    /**
     * @brief 设置默认帧率
     * @param fps 帧率
     */
    void SetDefaultFPS(int fps) { default_fps_ = fps; }

    /**
     * @brief 获取默认帧率
     * @return 当前默认帧率
     */
    int GetDefaultFPS() const { return default_fps_; }

private:
    // 私有构造函数（单例模式）
    AnimationPlayer();
    ~AnimationPlayer();
    AnimationPlayer(const AnimationPlayer&) = delete;
    AnimationPlayer& operator=(const AnimationPlayer&) = delete;

    // === 动画配置结构 ===
    struct AnimationConfig {
        int fps = 24;                          // 帧率
        int frames = 0;                        // 动画帧数
        int count = 1;                         // 播放次数，-1表示循环
        std::vector<std::string> can_interrupt; // 可中断的状态列表
        bool loop = false;                     // 是否循环播放
    };

    // === LVGL相关（从AnimaDisplay迁移） ===
    lv_display_t* display_ = nullptr;
    lv_obj_t* canvas_ = nullptr;
    void* canvas_buffer_ = nullptr;
    SemaphoreHandle_t lvgl_mutex_ = nullptr;

    // LCD相关
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int offset_x_ = 0;
    int offset_y_ = 0;
    bool mirror_x_ = false;
    bool mirror_y_ = false;
    bool swap_xy_ = false;

    // === 动画播放相关 ===
    TaskHandle_t animation_task_ = nullptr;
    AnimationLoader* loader_ = nullptr;

    // 配置和状态
    int default_fps_ = 24;
    AnimationConfig current_config_;
    std::string current_animation_path_;

    // 播放状态
    std::atomic<bool> is_playing_{false};
    std::atomic<bool> stop_requested_{false};

    // 配置缓存
    std::map<std::string, AnimationConfig> config_cache_;

    // === 内部方法 ===

    /**
     * @brief LVGL访问锁
     * @param timeout_ms 超时时间（毫秒）
     * @return true 获取锁成功, false 获取锁失败
     */
    bool Lock(int timeout_ms = 0);

    /**
     * @brief 释放LVGL访问锁
     */
    void Unlock();

    /**
     * @brief 初始化LVGL显示系统
     * @return true 初始化成功, false 初始化失败
     */
    bool SetupLVGL();

    /**
     * @brief 自动获取当前VA情感象限
     * @return 当前情感象限
     */
    EmotionQuadrant GetCurrentQuadrant();

    /**
     * @brief 获取当前设备状态
     * @return 当前设备状态
     */
    DeviceState GetCurrentDeviceState();

    /**
     * @brief 构建动画路径
     * @param event 事件类型
     * @return 动画目录路径
     */
    std::string BuildAnimationPath(EventType event);

    /**
     * @brief 构建交互事件动画路径
     * @param event 事件类型
     * @param quadrant 情感象限
     * @return 动画目录路径
     */
    std::string BuildInteractionPath(EventType event, EmotionQuadrant quadrant);

    /**
     * @brief 构建状态表达动画路径
     * @param state 设备状态
     * @param quadrant 情感象限
     * @return 动画目录路径
     */
    std::string BuildStatePath(DeviceState state, EmotionQuadrant quadrant);

    /**
     * @brief 构建系统事件动画路径
     * @param event 系统事件类型
     * @return 动画目录路径
     */
    // std::string BuildSystemPath(SystemEventType event);  // 暂时注释，未来实现

    /**
     * @brief 检查是否可以中断当前状态
     * @param config 动画配置
     * @return true 可以中断, false 不可中断
     */
    bool CanInterrupt(const AnimationConfig& config);

    /**
     * @brief 加载动画配置
     * @param event_key 事件键（如 "emergency/motion_free_fall"）
     * @return 动画配置
     */
    AnimationConfig LoadAnimationConfig(const std::string& event_key);

    /**
     * @brief 从manifest.json加载全局配置
     * @return true 加载成功, false 加载失败
     */
    bool LoadGlobalConfig();

    /**
     * @brief 内部播放动画
     * @param animation_path 动画路径
     * @param config 动画配置
     */
    void PlayAnimation(const std::string& animation_path, const AnimationConfig& config);

    /**
     * @brief 判断事件类型
     */
    bool IsEmergencyEvent(EventType event);
    bool IsInteractionEvent(EventType event);
    bool IsSystemEvent(EventType event);

    /**
     * @brief 获取事件名称
     * @param event 事件类型
     * @return 事件名称字符串
     */
    std::string GetEventName(EventType event);
    // std::string GetSystemEventName(SystemEventType event);  // 暂时注释，未来实现
    std::string GetStateName(DeviceState state);
    std::string GetQuadrantSuffix(EmotionQuadrant quadrant);

    /**
     * @brief 动画播放任务
     * @param arg AnimationPlayer实例指针
     */
    static void AnimationTask(void* arg);

    /**
     * @brief 执行动画播放循环
     */
    void AnimationLoop();
};

#endif // ANIMATION_PLAYER_H