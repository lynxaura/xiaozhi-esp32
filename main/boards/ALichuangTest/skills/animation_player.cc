#include "animation_player.h"
#include "animation_loader.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cstring>
#include <algorithm>
#include <cJSON.h>

// 需要包含的其他头文件
#include "../interaction/core/emotion_engine.h"
#include "../interaction/core/event_engine.h"
#include "../../application.h"
#include "../../device_state.h"
#include "../sddata_pro.h"

#define TAG "AnimationPlayer"

AnimationPlayer::AnimationPlayer() {
    // 创建LVGL互斥锁
    lvgl_mutex_ = xSemaphoreCreateMutex();
    if (!lvgl_mutex_) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
    }

    // 创建加载器
    loader_ = new AnimationLoader();
}

AnimationPlayer::~AnimationPlayer() {
    Destroy();

    if (loader_) {
        delete loader_;
        loader_ = nullptr;
    }

    if (lvgl_mutex_) {
        vSemaphoreDelete(lvgl_mutex_);
        lvgl_mutex_ = nullptr;
    }
}

AnimationPlayer& AnimationPlayer::GetInstance() {
    static AnimationPlayer instance;
    return instance;
}

bool AnimationPlayer::Initialize(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_handle_t panel,
                                int width, int height,
                                int offset_x, int offset_y,
                                bool mirror_x, bool mirror_y, bool swap_xy) {
    // 保存LCD参数
    panel_io_ = panel_io;
    panel_ = panel;
    width_ = width;
    height_ = height;
    offset_x_ = offset_x;
    offset_y_ = offset_y;
    mirror_x_ = mirror_x;
    mirror_y_ = mirror_y;
    swap_xy_ = swap_xy;

    // 初始化加载器
    if (!loader_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize animation loader");
        return false;
    }

    // 初始化LVGL（参考AnimaDisplay实现）
    if (!SetupLVGL()) {
        ESP_LOGE(TAG, "Failed to setup LVGL");
        return false;
    }

    // 加载全局配置
    LoadGlobalConfig();

    ESP_LOGI(TAG, "AnimationPlayer initialized successfully");
    return true;
}

bool AnimationPlayer::SetupLVGL() {
    // LVGL已经在SpiLcdDisplay中初始化，我们直接获取当前显示器
    display_ = lv_display_get_default();
    if (!display_) {
        ESP_LOGE(TAG, "No default LVGL display found");
        return false;
    }

    ESP_LOGI(TAG, "Using existing LVGL display");
    return true;
}

void AnimationPlayer::Destroy() {
    // 停止动画播放
    Stop();

    // 销毁画布
    DestroyCanvas();

    // 清理LVGL显示
    if (display_) {
        lv_display_delete(display_);
        display_ = nullptr;
    }
}

void AnimationPlayer::Play(EventType event) {
    // 检查是否可以中断当前播放
    std::string event_key;
    AnimationConfig config;

    // 判断事件类型并构建路径
    if (IsEmergencyEvent(event)) {
        event_key = "emergency/" + GetEventName(event);
        PlayEmergency(event);
    } else if (IsInteractionEvent(event)) {
        EmotionQuadrant quadrant = GetCurrentQuadrant();
        event_key = "interaction/" + GetEventName(event) + GetQuadrantSuffix(quadrant);
        PlayInteraction(event);
    } else if (IsSystemEvent(event)) {
        event_key = "system/" + GetEventName(event);
        // PlaySystem需要转换为SystemEventType，这里简化处理
        ESP_LOGW(TAG, "System event handling not fully implemented in Play()");
    } else {
        ESP_LOGW(TAG, "Unknown event type: %d", static_cast<int>(event));
    }
}

void AnimationPlayer::PlayEmergency(EventType event) {
    std::string animation_path = BuildAnimationPath(event);
    std::string event_key = "emergency/" + GetEventName(event);

    AnimationConfig config = LoadAnimationConfig(event_key);

    // 紧急事件总是可以中断
    PlayAnimation(animation_path, config);
}

void AnimationPlayer::PlayInteraction(EventType event) {
    EmotionQuadrant quadrant = GetCurrentQuadrant();
    std::string animation_path = BuildInteractionPath(event, quadrant);
    std::string event_key = "interaction/" + GetEventName(event) + GetQuadrantSuffix(quadrant);

    AnimationConfig config = LoadAnimationConfig(event_key);

    // 检查是否可以中断当前状态
    if (!CanInterrupt(config)) {
        ESP_LOGI(TAG, "Cannot interrupt current state for event: %s", event_key.c_str());
        return;
    }

    PlayAnimation(animation_path, config);
}

void AnimationPlayer::PlayStateExpression(DeviceState state) {
    EmotionQuadrant quadrant = GetCurrentQuadrant();
    std::string animation_path = BuildStatePath(state, quadrant);
    std::string event_key = "state_expression/" + GetStateName(state) + "/" +
                           GetStateName(state) + GetQuadrantSuffix(quadrant);

    AnimationConfig config = LoadAnimationConfig(event_key);

    // 状态表达通常不中断其他动画
    if (IsPlaying() && !CanInterrupt(config)) {
        ESP_LOGD(TAG, "State expression cannot interrupt current animation");
        return;
    }

    PlayAnimation(animation_path, config);
}

// void AnimationPlayer::PlaySystem(SystemEventType event) {
//     std::string animation_path = BuildSystemPath(event);
//     std::string event_key = "system/" + GetSystemEventName(event);
//
//     AnimationConfig config = LoadAnimationConfig(event_key);
//
//     // 系统事件通常有最高优先级
//     PlayAnimation(animation_path, config);
// }

void AnimationPlayer::PlayAnimation(const std::string& animation_path, const AnimationConfig& config) {
    // 停止当前动画
    Stop();

    // 检查动画是否存在
    if (!loader_->CheckAnimationExists(animation_path)) {
        ESP_LOGW(TAG, "Animation not found: %s", animation_path.c_str());
        return;
    }

    // 保存当前配置
    current_config_ = config;
    current_animation_path_ = animation_path;

    // 重置控制标志
    stop_requested_ = false;
    is_playing_ = true;

    ESP_LOGI(TAG, "Starting animation: %s (fps=%d, frames=%d, count=%d)",
             animation_path.c_str(), config.fps, config.frames, config.count);

    // 创建动画任务
    BaseType_t result = xTaskCreate(
        AnimationTask,
        "animation_task",
        8192,  // 栈大小
        this,
        5,     // 优先级
        &animation_task_
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create animation task");
        is_playing_ = false;
    }
}

void AnimationPlayer::Stop() {
    if (!IsPlaying()) {
        return;
    }

    ESP_LOGI(TAG, "Stopping animation");

    // 设置停止标志
    stop_requested_ = true;

    // 等待任务结束
    if (animation_task_ != nullptr) {
        // 等待最多2秒
        uint32_t timeout = 2000;
        uint32_t elapsed = 0;
        while (IsPlaying() && elapsed < timeout) {
            vTaskDelay(pdMS_TO_TICKS(10));
            elapsed += 10;
        }

        // 如果任务仍在运行，强制删除
        if (IsPlaying()) {
            vTaskDelete(animation_task_);
            ESP_LOGW(TAG, "Force deleted animation task");
        }

        animation_task_ = nullptr;
    }

    is_playing_ = false;
    ESP_LOGI(TAG, "Animation stopped");
}

void AnimationPlayer::CreateCanvas() {
    if (!Lock(30000)) {
        ESP_LOGE(TAG, "Failed to lock for canvas creation");
        return;
    }

    if (canvas_ != nullptr) {
        ESP_LOGI(TAG, "Canvas already exists");
        Unlock();
        return;
    }

    // 创建画布所需的缓冲区（参考AnimaDisplay实现）
    size_t buf_size = width_ * height_ * 2;  // RGB565: 2 bytes per pixel

    // 分配内存，优先使用PSRAM
    canvas_buffer_ = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (canvas_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        Unlock();
        return;
    }

    // 获取活动屏幕
    lv_obj_t* screen = lv_screen_active();

    // 创建画布对象
    canvas_ = lv_canvas_create(screen);
    if (canvas_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create canvas");
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
        Unlock();
        return;
    }

    // 初始化画布
    lv_canvas_set_buffer(canvas_, canvas_buffer_, width_, height_, LV_COLOR_FORMAT_RGB565);

    // 设置画布位置为全屏
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_set_size(canvas_, width_, height_);

    // 设置画布为透明
    lv_canvas_fill_bg(canvas_, lv_color_make(0, 0, 0), LV_OPA_TRANSP);

    // 设置画布为顶层
    lv_obj_move_foreground(canvas_);

    ESP_LOGI(TAG, "Canvas created successfully");
    Unlock();
}

void AnimationPlayer::DestroyCanvas() {
    if (!Lock(30000)) {
        ESP_LOGE(TAG, "Failed to lock for canvas destruction");
        return;
    }

    if (canvas_ != nullptr) {
        lv_obj_del(canvas_);
        canvas_ = nullptr;
    }

    if (canvas_buffer_ != nullptr) {
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
    }

    ESP_LOGI(TAG, "Canvas destroyed");
    Unlock();
}

void AnimationPlayer::DrawImageOnCanvas(int x, int y, int width, int height, const uint8_t* img_data) {
    if (!Lock(30000)) {
        ESP_LOGE(TAG, "Failed to lock display for drawing");
        return;
    }

    // 确保有画布
    if (canvas_ == nullptr) {
        ESP_LOGE(TAG, "Canvas not created");
        Unlock();
        return;
    }

    // 创建图像描述器（参考AnimaDisplay实现）
    const lv_image_dsc_t img_dsc = {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = (uint32_t)width,
            .h = (uint32_t)height,
            .stride = (uint32_t)(width * 2),  // RGB565: 2 bytes per pixel
            .reserved_2 = 0,
        },
        .data_size = (uint32_t)(width * height * 2),  // RGB565: 2 bytes per pixel
        .data = img_data,
        .reserved = NULL
    };

    // 使用图层绘制图像到画布上
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    lv_draw_image_dsc_t draw_dsc;
    lv_draw_image_dsc_init(&draw_dsc);
    draw_dsc.src = &img_dsc;

    lv_area_t area;
    area.x1 = x;
    area.y1 = y;
    area.x2 = x + width - 1;
    area.y2 = y + height - 1;

    lv_draw_image(&layer, &draw_dsc, &area);
    lv_canvas_finish_layer(canvas_, &layer);

    // 确保画布在最上层
    lv_obj_move_foreground(canvas_);

    Unlock();
}

bool AnimationPlayer::Lock(int timeout_ms) {
    if (!lvgl_mutex_) {
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mutex_, timeout_ticks) == pdTRUE;
}

void AnimationPlayer::Unlock() {
    if (lvgl_mutex_) {
        xSemaphoreGive(lvgl_mutex_);
    }
}

EmotionQuadrant AnimationPlayer::GetCurrentQuadrant() {
    return EmotionEngine::GetInstance().GetQuadrant();
}

DeviceState AnimationPlayer::GetCurrentDeviceState() {
    return Application::GetInstance().GetDeviceState();
}

std::string AnimationPlayer::BuildAnimationPath(EventType event) {
    if (IsEmergencyEvent(event)) {
        return "/sdcard/emergency/" + GetEventName(event) + "/animation";
    } else if (IsInteractionEvent(event)) {
        EmotionQuadrant quadrant = GetCurrentQuadrant();
        return BuildInteractionPath(event, quadrant);
    } else {
        ESP_LOGW(TAG, "Unknown event type in BuildAnimationPath: %d", static_cast<int>(event));
        return "";
    }
}

std::string AnimationPlayer::BuildInteractionPath(EventType event, EmotionQuadrant quadrant) {
    return "/sdcard/interaction/" + GetEventName(event) + GetQuadrantSuffix(quadrant) + "/animation";
}

std::string AnimationPlayer::BuildStatePath(DeviceState state, EmotionQuadrant quadrant) {
    std::string state_name = GetStateName(state);
    return "/sdcard/state_expression/" + state_name + "/" + state_name + GetQuadrantSuffix(quadrant) + "/animation";
}

// std::string AnimationPlayer::BuildSystemPath(SystemEventType event) {
//     return "/sdcard/system/" + GetSystemEventName(event) + "/animation";
// }

AnimationPlayer::AnimationConfig AnimationPlayer::LoadAnimationConfig(const std::string& event_key) {
    // 检查缓存
    auto it = config_cache_.find(event_key);
    if (it != config_cache_.end()) {
        return it->second;
    }

    AnimationConfig config;
    config.fps = default_fps_;  // 默认帧率

    // 从response_config.json加载配置
    SDdata_Pro* sdcard = GetSDHandle();
    if (sdcard == nullptr) {
        ESP_LOGW(TAG, "SD card not available, using default config");
        config_cache_[event_key] = config;
        return config;
    }

    FILE* f = fopen("/sdcard/response_config.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "response_config.json not found, using default config");
        config_cache_[event_key] = config;
        return config;
    }

    // 读取文件
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json_data = (char*)malloc(file_size + 1);
    if (!json_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for config file");
        fclose(f);
        config_cache_[event_key] = config;
        return config;
    }

    fread(json_data, 1, file_size, f);
    json_data[file_size] = '\0';
    fclose(f);

    // 解析JSON
    cJSON* root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse response_config.json");
        free(json_data);
        config_cache_[event_key] = config;
        return config;
    }

    cJSON* events = cJSON_GetObjectItem(root, "events");
    if (events) {
        cJSON* event_config = cJSON_GetObjectItem(events, event_key.c_str());
        if (event_config) {
            // 解析can_interrupt
            cJSON* can_interrupt = cJSON_GetObjectItem(event_config, "can_interrupt");
            if (can_interrupt && cJSON_IsArray(can_interrupt)) {
                config.can_interrupt.clear();
                cJSON* item = nullptr;
                cJSON_ArrayForEach(item, can_interrupt) {
                    if (cJSON_IsString(item)) {
                        config.can_interrupt.push_back(cJSON_GetStringValue(item));
                    }
                }
            }

            // 解析animation配置
            cJSON* animation = cJSON_GetObjectItem(event_config, "animation");
            if (animation) {
                cJSON* frames = cJSON_GetObjectItem(animation, "frames");
                if (frames && cJSON_IsNumber(frames)) {
                    config.frames = cJSON_GetNumberValue(frames);
                }

                cJSON* count = cJSON_GetObjectItem(animation, "count");
                if (count && cJSON_IsNumber(count)) {
                    config.count = cJSON_GetNumberValue(count);
                    config.loop = (config.count == -1);
                }
            }
        }
    }

    cJSON_Delete(root);
    free(json_data);

    // 如果frames为0，尝试自动检测
    if (config.frames == 0) {
        // 构建动画路径并检测帧数
        // 这里需要根据event_key构建实际的动画路径
        // 为了简化，暂时设置默认值
        config.frames = 6;  // 默认6帧
    }

    // 缓存配置
    config_cache_[event_key] = config;

    ESP_LOGI(TAG, "Loaded config for %s: fps=%d, frames=%d, count=%d",
             event_key.c_str(), config.fps, config.frames, config.count);

    return config;
}

bool AnimationPlayer::LoadGlobalConfig() {
    FILE* f = fopen("/sdcard/manifest.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "manifest.json not found, using default settings");
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json_data = (char*)malloc(file_size + 1);
    if (!json_data) {
        fclose(f);
        return false;
    }

    fread(json_data, 1, file_size, f);
    json_data[file_size] = '\0';
    fclose(f);

    cJSON* root = cJSON_Parse(json_data);
    if (!root) {
        free(json_data);
        return false;
    }

    cJSON* animation_specs = cJSON_GetObjectItem(root, "animation_specs");
    if (animation_specs) {
        cJSON* fps = cJSON_GetObjectItem(animation_specs, "fps");
        if (fps && cJSON_IsNumber(fps)) {
            default_fps_ = cJSON_GetNumberValue(fps);
            ESP_LOGI(TAG, "Set default FPS to %d from manifest.json", default_fps_);
        }
    }

    cJSON_Delete(root);
    free(json_data);
    return true;
}

bool AnimationPlayer::CanInterrupt(const AnimationConfig& config) {
    DeviceState current_state = GetCurrentDeviceState();
    std::string state_name = GetStateName(current_state);

    // 检查can_interrupt列表
    for (const auto& interrupt_state : config.can_interrupt) {
        if (interrupt_state == "all" || interrupt_state == state_name) {
            return true;
        }
    }

    return false;
}

// 动画播放任务（参考ImageSlideshowTask实现）
void AnimationPlayer::AnimationTask(void* arg) {
    AnimationPlayer* player = static_cast<AnimationPlayer*>(arg);
    player->AnimationLoop();
}

void AnimationPlayer::AnimationLoop() {
    ESP_LOGI(TAG, "Animation task started");

    // 确保画布存在
    if (!HasCanvas()) {
        CreateCanvas();
    }

    // 分配转换缓冲区（RGB565字节序转换）
    uint16_t* convertedData = new(std::nothrow) uint16_t[width_ * height_];
    if (!convertedData) {
        ESP_LOGE(TAG, "Failed to allocate conversion buffer");
        is_playing_ = false;
        vTaskDelete(NULL);
        return;
    }

    const AnimationConfig& config = current_config_;
    const std::string& animPath = current_animation_path_;

    // 计算帧间隔
    TickType_t frameInterval = pdMS_TO_TICKS(1000 / config.fps);

    ESP_LOGI(TAG, "Starting animation loop: path=%s, fps=%d, frames=%d, count=%d",
             animPath.c_str(), config.fps, config.frames, config.count);

    // 播放循环
    int playCount = 0;
    while (!stop_requested_ && (config.count == -1 || playCount < config.count)) {

        // 播放所有帧
        for (int frame = 1; frame <= config.frames && !stop_requested_; frame++) {
            // 构建帧文件路径
            char framePath[256];
            snprintf(framePath, sizeof(framePath), "%s/%03d.bin", animPath.c_str(), frame);

            // 加载帧数据
            uint8_t* frameData = (uint8_t*)heap_caps_malloc(153600, MALLOC_CAP_SPIRAM);
            if (!frameData) {
                ESP_LOGW(TAG, "Failed to allocate frame buffer for frame %d", frame);
                continue;
            }

            if (!loader_->LoadFrame(framePath, frameData)) {
                ESP_LOGD(TAG, "Failed to load frame: %s", framePath);
                heap_caps_free(frameData);
                continue;
            }

            // RGB565字节序转换（参考ImageSlideshowTask）
            for (int i = 0; i < width_ * height_; i++) {
                uint16_t pixel = ((uint16_t*)frameData)[i];
                convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
            }

            // 绘制到屏幕
            DrawImageOnCanvas(0, 0, width_, height_, (const uint8_t*)convertedData);

            heap_caps_free(frameData);

            // 帧间隔延时
            vTaskDelay(frameInterval);
        }

        playCount++;

        // 如果不是循环播放，播放完一次就结束
        if (!config.loop && playCount >= config.count) {
            break;
        }
    }

    // 清理资源
    delete[] convertedData;

    ESP_LOGI(TAG, "Animation task completed");
    is_playing_ = false;

    // 任务自删除
    animation_task_ = nullptr;
    vTaskDelete(NULL);
}

// 事件类型判断函数（需要根据实际的EventType枚举实现）
bool AnimationPlayer::IsEmergencyEvent(EventType event) {
    // 根据实际的EventType枚举定义实现
    switch (event) {
        case EventType::MOTION_FREE_FALL:
        case EventType::MOTION_SHAKE_VIOLENTLY:
        case EventType::MOTION_FLIP:
        case EventType::MOTION_UPSIDE_DOWN:
            return true;
        default:
            return false;
    }
}

bool AnimationPlayer::IsInteractionEvent(EventType event) {
    switch (event) {
        case EventType::MOTION_SHAKE:
        case EventType::MOTION_PICKUP:
        case EventType::TOUCH_TAP:
        case EventType::TOUCH_LONG_PRESS:
        case EventType::TOUCH_CRADLED:
        case EventType::TOUCH_TICKLED:
            return true;
        default:
            return false;
    }
}

bool AnimationPlayer::IsSystemEvent(EventType event) {
    // 系统事件可能需要单独的枚举类型
    // 这里暂时返回false，待SystemEventType完善后实现
    return false;
}

// 获取事件名称（需要根据实际的EventType枚举实现）
std::string AnimationPlayer::GetEventName(EventType event) {
    switch (event) {
        case EventType::MOTION_FREE_FALL: return "motion_free_fall";
        case EventType::MOTION_SHAKE_VIOLENTLY: return "motion_shake_violently";
        case EventType::MOTION_FLIP: return "motion_flip";
        case EventType::MOTION_UPSIDE_DOWN: return "motion_upside_down";
        case EventType::MOTION_SHAKE: return "motion_shake";
        case EventType::MOTION_PICKUP: return "motion_pickup";
        case EventType::TOUCH_TAP: return "touch_tap";
        case EventType::TOUCH_LONG_PRESS: return "touch_long_press";
        case EventType::TOUCH_CRADLED: return "touch_cradled";
        case EventType::TOUCH_TICKLED: return "touch_tickled";
        default: return "unknown";
    }
}

// std::string AnimationPlayer::GetSystemEventName(SystemEventType event) {
//     // 待SystemEventType定义完善后实现
//     return "unknown_system_event";
// }

std::string AnimationPlayer::GetStateName(DeviceState state) {
    switch (state) {
        case kDeviceStateIdle: return "idle";
        case kDeviceStateListening: return "listening";
        case kDeviceStateSpeaking: return "speaking";
        default: return "unknown";
    }
}

std::string AnimationPlayer::GetQuadrantSuffix(EmotionQuadrant quadrant) {
    switch (quadrant) {
        case EmotionQuadrant::POSITIVE_HIGH_AROUSAL: return "_q1";
        case EmotionQuadrant::NEGATIVE_HIGH_AROUSAL: return "_q2";
        case EmotionQuadrant::NEGATIVE_LOW_AROUSAL: return "_q3";
        case EmotionQuadrant::POSITIVE_LOW_AROUSAL: return "_q4";
        default: return "_q1";  // 默认Q1
    }
}