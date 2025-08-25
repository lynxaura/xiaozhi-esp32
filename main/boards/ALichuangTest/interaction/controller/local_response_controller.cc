#include "local_response_controller.h"
#include "display/display.h"
#include <esp_log.h>
#include <cJSON.h>

#define TAG "LocalResponseController"

LocalResponseController::LocalResponseController(
    Motion* motion_skill,
    Vibration* vibration_skill,
    EventEngine* event_engine,
    std::function<Display*()> get_display_func,
    std::function<std::string()> get_current_emotion_func,
    std::function<void(const std::string&)> set_current_emotion_func
) : motion_skill_(motion_skill),
    vibration_skill_(vibration_skill),
    event_engine_(event_engine),
    get_display_func_(get_display_func),
    get_current_emotion_func_(get_current_emotion_func),
    set_current_emotion_func_(set_current_emotion_func) {
}

LocalResponseController::~LocalResponseController() {
}

bool LocalResponseController::Initialize() {
    ESP_LOGI(TAG, "Initializing MCP local response tools...");
    
    try {
        // 注册所有工具
        RegisterMotionTools();
        RegisterVibrationTools();
        RegisterDisplayTools();
        RegisterComplexExpressionTools();
        RegisterStatusTools();
        
        ESP_LOGI(TAG, "✅ All MCP tools registered successfully");
        
        // 记录已注册的工具数量
        ESP_LOGI(TAG, "MCP Local Response System Ready - Available Tools:");
        ESP_LOGI(TAG, "  - Body Motion Control (3 tools)");
        ESP_LOGI(TAG, "  - Haptic Feedback (2 tools)");
        ESP_LOGI(TAG, "  - Display Animation (2 tools)");
        ESP_LOGI(TAG, "  - Complex Expression (2 tools)");
        ESP_LOGI(TAG, "  - Status Query (3 tools)");
        ESP_LOGI(TAG, "  Total: 12 tools available for LLM interaction");
        
        return true;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Failed to initialize MCP tools: %s", e.what());
        return false;
    }
}

void LocalResponseController::RegisterMotionTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 基础身体动作控制
    mcp_server.AddTool("self.body.basic_motion", 
        "控制身体做基础动作。可用动作：\n"
        "happy_wiggle: 开心摇摆\n"
        "shake_head: 摇头表示不同意\n" 
        "nuzzle_forward: 向前蹭表示亲昵\n"
        "relax_completely: 完全放松\n"
        "excited_jiggle: 兴奋抖动\n"
        "stop: 停止当前动作", 
        PropertyList({
            Property("action", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return BasicMotionTool(properties);
        });
    
    // 情绪表达动作
    mcp_server.AddTool("self.body.emotion_motion",
        "根据情绪表达相应的身体动作。支持情绪：\n"
        "happy: 开心时的动作\n"
        "angry: 愤怒时的动作\n"
        "shy: 害羞时的动作\n"
        "curious: 好奇时的动作\n"
        "nervous: 紧张时的动作\n"
        "excited: 兴奋时的动作",
        PropertyList({
            Property("emotion", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return EmotionMotionTool(properties);
        });
    
    // 精确角度控制
    mcp_server.AddTool("self.body.angle_control",
        "控制身体转到精确角度。参数说明：\n"
        "angle: 目标角度，范围-90到90度\n"
        "speed: 转动速度 (slow/medium/fast)",
        PropertyList({
            Property("angle", kPropertyTypeInteger, -90, 90),
            Property("speed", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return AngleControlTool(properties);
        });
}

void LocalResponseController::RegisterVibrationTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 基础振动控制
    mcp_server.AddTool("self.haptic.basic_vibration",
        "控制振动马达产生触觉反馈。可用模式：\n"
        "short_buzz: 短促确认振动 - 轻抚头部的清脆反馈\n"
        "purr_short: 短促的咕噜声 - 轻抚头部的温和反馈\n"
        "purr_pattern: 持续的咕噜咕噜声 - 按住头部的舒适感\n"
        "gentle_heartbeat: 温暖平稳的心跳 - 按住头部/被拥抱的安全感\n"
        "struggle_pattern: 表达挣扎的不规律振动 - 按住头部/被倒置时的不适\n"
        "sharp_buzz: 尖锐提醒振动 - 轻触身体时的打扰感\n"
        "tremble_pattern: 表达害怕的颤抖 - 被拿起时不开心的反应\n"
        "giggle_pattern: 模拟笑到发抖的欢快振动 - 被挠痒痒的快乐\n"
        "heartbeat_strong: 表达力量和信念的强心跳 - 掌心约定的坚定\n"
        "erratic_strong: 表达眩晕的混乱强振动 - 被剧烈晃动的dizzy感\n"
        "stop: 停止振动",
        PropertyList({
            Property("pattern", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return BasicVibrationTool(properties);
        });
    
    // 情绪振动反馈
    mcp_server.AddTool("self.haptic.emotion_vibration", 
        "根据情绪产生相应的振动反馈。支持情绪：\n"
        "happy: 开心的振动\n"
        "excited: 兴奋的振动\n"
        "comfort: 舒适的振动\n"
        "alert: 警觉的振动\n"
        "sad: 悲伤的振动\n"
        "scared: 害怕的振动",
        PropertyList({
            Property("emotion", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return EmotionVibrationTool(properties);
        });
}

void LocalResponseController::RegisterDisplayTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 情绪动画控制
    mcp_server.AddTool("self.display.show_emotion",
        "在屏幕上显示情绪动画。支持情绪：\n"
        "neutral: 中性表情\n"
        "happy: 开心表情\n" 
        "angry: 愤怒表情\n"
        "sad: 悲伤表情\n"
        "surprised: 惊讶表情\n"
        "laughing: 大笑表情\n"
        "thinking: 思考表情",
        PropertyList({
            Property("emotion", kPropertyTypeString),
            Property("duration", kPropertyTypeInteger, 0, 30000) // 可选，持续时间(ms)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return ShowEmotionTool(properties);
        });
    
    // 动画播放控制
    mcp_server.AddTool("self.display.animation_control",
        "控制屏幕动画播放。支持操作：\n"
        "start: 开始播放当前情绪动画\n"
        "stop: 停止动画播放\n"
        "set_speed: 设置动画播放速度",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("speed", kPropertyTypeInteger, 10, 500) // 可选，播放间隔(ms)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return AnimationControlTool(properties);
        });
}

void LocalResponseController::RegisterComplexExpressionTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 综合情绪表达
    mcp_server.AddTool("self.express.emotion",
        "综合表达情绪，同时控制身体动作、振动反馈和屏幕动画。支持情绪：\n"
        "joy: 喜悦 - 开心摇摆 + 笑声振动 + 开心动画\n"
        "excitement: 兴奋 - 激动抖动 + 强烈振动 + 兴奋动画\n"
        "sadness: 悲伤 - 低垂动作 + 心跳振动 + 悲伤动画\n"
        "anger: 愤怒 - 摇头 + 尖锐振动 + 愤怒动画\n"
        "surprise: 惊讶 - 快速转向 + 短促振动 + 惊讶动画\n"
        "affection: 亲昵 - 向前蹭 + 咕噜振动 + 开心动画",
        PropertyList({
            Property("emotion", kPropertyTypeString),
            Property("intensity", kPropertyTypeInteger, 1, 5) // 强度等级
        }), [this](const PropertyList& properties) -> ReturnValue {
            return ComplexEmotionTool(properties);
        });
    
    // 交互响应动作
    mcp_server.AddTool("self.interact.respond",
        "执行交互响应动作。支持响应类型：\n"
        "agree: 同意 - 点头动作 + 确认振动\n"
        "disagree: 不同意 - 摇头动作 + 否定振动\n"
        "acknowledge: 理解确认 - 轻微摇摆 + 短促振动\n"
        "celebrate: 庆祝 - 兴奋动作 + 欢快振动\n"
        "comfort: 安慰 - 轻柔动作 + 心跳振动",
        PropertyList({
            Property("response_type", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return InteractiveResponseTool(properties);
        });
}

void LocalResponseController::RegisterStatusTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 动作状态查询
    mcp_server.AddTool("self.status.motion", "查询身体动作系统状态", PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            return MotionStatusTool(properties);
        });
    
    // 事件系统状态查询
    mcp_server.AddTool("self.status.events", "查询事件系统状态，包括触摸、运动检测和事件统计", PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            return EventsStatusTool(properties);
        });
    
    // 系统状态查询
    mcp_server.AddTool("self.status.system", "查询设备系统状态", PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            return SystemStatusTool(properties);
        });
}

// 基础身体动作控制工具实现
ReturnValue LocalResponseController::BasicMotionTool(const PropertyList& properties) {
    const std::string& action = properties["action"].value<std::string>();
    
    if (!motion_skill_) {
        return ReturnValue("Motion system not available");
    }
    
    if (action == "happy_wiggle") {
        motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
    } else if (action == "shake_head") {
        motion_skill_->Perform(MOTION_SHAKE_HEAD);
    } else if (action == "nuzzle_forward") {
        motion_skill_->Perform(MOTION_NUZZLE_FORWARD);
    } else if (action == "relax_completely") {
        motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
    } else if (action == "excited_jiggle") {
        motion_skill_->Perform(MOTION_EXCITED_JIGGLE);
    } else if (action == "stop") {
        motion_skill_->Stop();
    } else {
        return ReturnValue("Unknown action: " + action);
    }
    
    ESP_LOGI(TAG, "Body motion executed: %s", action.c_str());
    return ReturnValue("Action " + action + " executed successfully");
}

// 情绪身体动作控制工具实现
ReturnValue LocalResponseController::EmotionMotionTool(const PropertyList& properties) {
    const std::string& emotion = properties["emotion"].value<std::string>();
    
    if (!motion_skill_) {
        return ReturnValue("Motion system not available");
    }
    
    motion_id_t motion_id = GetMotionIdForEmotion(emotion);
    if (motion_id == MOTION_RELAX_COMPLETELY && emotion != "relax") {
        return ReturnValue("Unknown emotion: " + emotion);
    }
    
    motion_skill_->Perform(motion_id);
    ESP_LOGI(TAG, "Emotion motion executed: %s", emotion.c_str());
    return ReturnValue("Emotion " + emotion + " expressed successfully");
}

// 精确角度控制工具实现
ReturnValue LocalResponseController::AngleControlTool(const PropertyList& properties) {
    if (!motion_skill_) {
        return ReturnValue("Motion system not available");
    }
    
    int angle_int = properties["angle"].value<int>();
    float angle = static_cast<float>(angle_int);
    std::string speed_str = properties["speed"].value<std::string>();
    
    motion_speed_t speed = ParseMotionSpeed(speed_str);
    
    motion_skill_->SetAngle(angle, speed);
    
    ESP_LOGI(TAG, "Angle control: %.1f degrees at %s speed", angle, speed_str.c_str());
    return ReturnValue("Moved to " + std::to_string(angle_int) + " degrees");
}

// 基础振动控制工具实现
ReturnValue LocalResponseController::BasicVibrationTool(const PropertyList& properties) {
    const std::string& pattern = properties["pattern"].value<std::string>();
    
    if (!vibration_skill_) {
        return ReturnValue("Vibration system not available");
    }
    
    if (pattern == "stop") {
        vibration_skill_->Stop();
        return ReturnValue("Vibration stopped");
    }
    
    vibration_id_t vibration_id = ParseVibrationPattern(pattern);
    if (vibration_id == VIBRATION_MAX) {
        return ReturnValue("Unknown vibration pattern: " + pattern);
    }
    
    vibration_skill_->Play(vibration_id);
    ESP_LOGI(TAG, "Vibration executed: %s", pattern.c_str());
    return ReturnValue("Vibration pattern " + pattern + " started");
}

// 情绪振动反馈工具实现
ReturnValue LocalResponseController::EmotionVibrationTool(const PropertyList& properties) {
    const std::string& emotion = properties["emotion"].value<std::string>();
    
    if (!vibration_skill_) {
        return ReturnValue("Vibration system not available");
    }
    
    vibration_skill_->PlayForEmotion(emotion);
    ESP_LOGI(TAG, "Emotion vibration: %s", emotion.c_str());
    return ReturnValue("Emotion vibration " + emotion + " triggered");
}

// 显示情绪动画工具实现
ReturnValue LocalResponseController::ShowEmotionTool(const PropertyList& properties) {
    const std::string& emotion = properties["emotion"].value<std::string>();
    
    auto display = get_display_func_();
    if (!display) {
        return ReturnValue("Display system not available");
    }
    
    // 设置情绪，触发动画变更
    set_current_emotion_func_(emotion);
    display->SetEmotion(emotion.c_str());
    
    // TODO: 可选的持续时间控制
    // 由于PropertyList没有find方法，暂时跳过duration参数处理
    // 需要在MCP框架中添加对可选参数的支持
    
    ESP_LOGI(TAG, "Emotion animation: %s", emotion.c_str());
    return ReturnValue("Emotion " + emotion + " displayed");
}

// 动画播放控制工具实现
ReturnValue LocalResponseController::AnimationControlTool(const PropertyList& properties) {
    const std::string& action = properties["action"].value<std::string>();
    
    if (action == "start") {
        // 开始播放动画（实际上AnimaDisplay一直在播放）
        ESP_LOGI(TAG, "Animation playback started");
        return ReturnValue("Animation started");
    } else if (action == "stop") {
        // 停止到neutral状态
        set_current_emotion_func_("neutral");
        ESP_LOGI(TAG, "Animation playback stopped");
        return ReturnValue("Animation stopped");
    } else if (action == "set_speed") {
        // TODO: 处理可选的speed参数
        // 由于PropertyList没有find方法，暂时使用默认速度
        ESP_LOGI(TAG, "Animation speed control not yet implemented");
        return ReturnValue("Animation speed control not yet implemented");
    } else {
        return ReturnValue("Unknown action: " + action);
    }
}

// 复合情绪表达工具实现
ReturnValue LocalResponseController::ComplexEmotionTool(const PropertyList& properties) {
    const std::string& emotion = properties["emotion"].value<std::string>();
    // TODO: 处理可选参数intensity，默认值为3
    int intensity = 3; // 默认强度
    
    // 根据情绪执行组合动作
    if (emotion == "joy") {
        // 身体动作
        if (motion_skill_) motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
        // 振动反馈
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_GIGGLE_PATTERN);
        // 屏幕动画
        set_current_emotion_func_("happy");
    } else if (emotion == "excitement") {
        if (motion_skill_) motion_skill_->Perform(MOTION_EXCITED_JIGGLE);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_ERRATIC_STRONG);
        set_current_emotion_func_("happy");
    } else if (emotion == "sadness") {
        if (motion_skill_) motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_GENTLE_HEARTBEAT);
        set_current_emotion_func_("sad");
    } else if (emotion == "anger") {
        if (motion_skill_) motion_skill_->Perform(MOTION_SHAKE_HEAD);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_STRUGGLE_PATTERN);
        set_current_emotion_func_("angry");
    } else if (emotion == "surprise") {
        if (motion_skill_) motion_skill_->Perform(MOTION_QUICK_TURN_LEFT);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHORT_BUZZ);
        set_current_emotion_func_("surprised");
    } else if (emotion == "affection") {
        if (motion_skill_) motion_skill_->Perform(MOTION_NUZZLE_FORWARD);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_HEARTBEAT_STRONG);
        set_current_emotion_func_("happy");
    } else {
        return ReturnValue("Unknown emotion: " + emotion);
    }
    
    ESP_LOGI(TAG, "Complex emotion expression: %s (intensity: %d)", 
            emotion.c_str(), intensity);
    return ReturnValue("Emotion " + emotion + " expressed comprehensively");
}

// 交互响应动作工具实现
ReturnValue LocalResponseController::InteractiveResponseTool(const PropertyList& properties) {
    const std::string& response_type = properties["response_type"].value<std::string>();
    
    if (response_type == "agree") {
        if (motion_skill_) motion_skill_->Perform(MOTION_NUZZLE_FORWARD);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHORT_BUZZ);
    } else if (response_type == "disagree") {
        if (motion_skill_) motion_skill_->Perform(MOTION_SHAKE_HEAD);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHARP_BUZZ);
    } else if (response_type == "acknowledge") {
        if (motion_skill_) motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHORT_BUZZ);
    } else if (response_type == "celebrate") {
        if (motion_skill_) motion_skill_->Perform(MOTION_EXCITED_JIGGLE);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_GIGGLE_PATTERN);
        set_current_emotion_func_("happy");
    } else if (response_type == "comfort") {
        if (motion_skill_) motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
        if (vibration_skill_) vibration_skill_->Play(VIBRATION_PURR_PATTERN);
        set_current_emotion_func_("neutral");
    } else {
        return ReturnValue("Unknown response type: " + response_type);
    }
    
    ESP_LOGI(TAG, "Interaction response: %s", response_type.c_str());
    return ReturnValue("Response " + response_type + " executed");
}

// 动作状态查询工具实现
ReturnValue LocalResponseController::MotionStatusTool(const PropertyList& properties) {
    if (!motion_skill_) {
        return ReturnValue("Motion system not available");
    }
    
    bool is_busy = motion_skill_->IsBusy();
    
    cJSON* status = cJSON_CreateObject();
    cJSON_AddBoolToObject(status, "is_busy", is_busy);
    cJSON_AddStringToObject(status, "status", is_busy ? "moving" : "idle");
    
    char* json_str = cJSON_Print(status);
    std::string result(json_str);
    cJSON_free(json_str);
    cJSON_Delete(status);
    
    return ReturnValue(result);
}

// 事件系统状态查询工具实现
ReturnValue LocalResponseController::EventsStatusTool(const PropertyList& properties) {
    if (!event_engine_) {
        return ReturnValue("Event engine not available");
    }
    
    cJSON* status = cJSON_CreateObject();
    
    // 实时状态查询
    cJSON_AddBoolToObject(status, "left_touched", event_engine_->IsLeftTouched());
    cJSON_AddBoolToObject(status, "right_touched", event_engine_->IsRightTouched());
    cJSON_AddBoolToObject(status, "picked_up", event_engine_->IsPickedUp());
    cJSON_AddBoolToObject(status, "upside_down", event_engine_->IsUpsideDown());
    
    // 事件处理统计
    cJSON* event_stats = cJSON_CreateObject();
    
    // 获取各类事件的处理统计
    auto touch_stats = event_engine_->GetEventStats(EventType::TOUCH_TAP);
    cJSON* touch_stats_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(touch_stats_obj, "received_count", touch_stats.received_count);
    cJSON_AddNumberToObject(touch_stats_obj, "processed_count", touch_stats.processed_count);
    cJSON_AddNumberToObject(touch_stats_obj, "dropped_count", touch_stats.dropped_count);
    cJSON_AddNumberToObject(touch_stats_obj, "merged_count", touch_stats.merged_count);
    cJSON_AddNumberToObject(touch_stats_obj, "last_process_time", touch_stats.last_process_time / 1000); // 转换为ms
    cJSON_AddItemToObject(event_stats, "touch_tap", touch_stats_obj);
    
    auto motion_stats = event_engine_->GetEventStats(EventType::MOTION_SHAKE);
    cJSON* motion_stats_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(motion_stats_obj, "received_count", motion_stats.received_count);
    cJSON_AddNumberToObject(motion_stats_obj, "processed_count", motion_stats.processed_count);
    cJSON_AddNumberToObject(motion_stats_obj, "dropped_count", motion_stats.dropped_count);
    cJSON_AddNumberToObject(motion_stats_obj, "merged_count", motion_stats.merged_count);
    cJSON_AddNumberToObject(motion_stats_obj, "last_process_time", motion_stats.last_process_time / 1000);
    cJSON_AddItemToObject(event_stats, "motion_shake", motion_stats_obj);
    
    cJSON_AddItemToObject(status, "event_statistics", event_stats);
    
    char* json_str = cJSON_Print(status);
    std::string result(json_str);
    cJSON_free(json_str);
    cJSON_Delete(status);
    
    return ReturnValue(result);
}

// 系统状态查询工具实现
ReturnValue LocalResponseController::SystemStatusTool(const PropertyList& properties) {
    cJSON* status = cJSON_CreateObject();
    
    // 子系统可用性
    cJSON_AddBoolToObject(status, "motion_available", motion_skill_ != nullptr);
    cJSON_AddBoolToObject(status, "vibration_available", vibration_skill_ != nullptr);
    cJSON_AddBoolToObject(status, "display_available", get_display_func_() != nullptr);
    cJSON_AddBoolToObject(status, "event_engine_available", event_engine_ != nullptr);
    
    // 当前状态
    std::string current_emotion = get_current_emotion_func_();
    cJSON_AddStringToObject(status, "current_emotion", current_emotion.c_str());
    
    // 动作状态
    if (motion_skill_) {
        cJSON_AddBoolToObject(status, "motion_busy", motion_skill_->IsBusy());
    }
    
    // 触摸状态
    if (event_engine_) {
        cJSON_AddBoolToObject(status, "left_touched", event_engine_->IsLeftTouched());
        cJSON_AddBoolToObject(status, "right_touched", event_engine_->IsRightTouched());
        cJSON_AddBoolToObject(status, "picked_up", event_engine_->IsPickedUp());
        cJSON_AddBoolToObject(status, "upside_down", event_engine_->IsUpsideDown());
    }
    
    char* json_str = cJSON_Print(status);
    std::string result(json_str);
    cJSON_free(json_str);
    cJSON_Delete(status);
    
    return ReturnValue(result);
}

// 辅助方法实现
motion_id_t LocalResponseController::GetMotionIdForEmotion(const std::string& emotion) {
    if (emotion == "happy") {
        return MOTION_HAPPY_WIGGLE;
    } else if (emotion == "angry") {
        return MOTION_SHAKE_HEAD;
    } else if (emotion == "shy") {
        return MOTION_DODGE_SUBTLE;
    } else if (emotion == "curious") {
        return MOTION_CURIOUS_PEEK_LEFT; // 或根据随机选择左右
    } else if (emotion == "nervous") {
        return MOTION_TENSE_UP;
    } else if (emotion == "excited") {
        return MOTION_EXCITED_JIGGLE;
    } else {
        return MOTION_RELAX_COMPLETELY; // 默认动作
    }
}

vibration_id_t LocalResponseController::GetVibrationIdForEmotion(const std::string& emotion) {
    if (emotion == "happy" || emotion == "joy") {
        return VIBRATION_GIGGLE_PATTERN;
    } else if (emotion == "excited" || emotion == "excitement") {
        return VIBRATION_ERRATIC_STRONG;
    } else if (emotion == "comfort" || emotion == "content") {
        return VIBRATION_PURR_PATTERN;
    } else if (emotion == "alert" || emotion == "surprised") {
        return VIBRATION_SHARP_BUZZ;
    } else if (emotion == "sad" || emotion == "sadness") {
        return VIBRATION_GENTLE_HEARTBEAT;
    } else if (emotion == "scared" || emotion == "fear") {
        return VIBRATION_TREMBLE_PATTERN;
    } else if (emotion == "angry" || emotion == "frustrated") {
        return VIBRATION_STRUGGLE_PATTERN;
    } else if (emotion == "affection" || emotion == "love") {
        return VIBRATION_HEARTBEAT_STRONG;
    } else if (emotion == "playful" || emotion == "tickled") {
        return VIBRATION_GIGGLE_PATTERN;
    } else if (emotion == "calm" || emotion == "relaxed") {
        return VIBRATION_PURR_SHORT;
    } else {
        return VIBRATION_SHORT_BUZZ; // 默认振动
    }
}

motion_speed_t LocalResponseController::ParseMotionSpeed(const std::string& speed_str) {
    if (speed_str == "slow") return MOTION_SPEED_SLOW;
    else if (speed_str == "fast") return MOTION_SPEED_FAST;
    else return MOTION_SPEED_MEDIUM; // 默认中等速度
}

vibration_id_t LocalResponseController::ParseVibrationPattern(const std::string& pattern) {
    if (pattern == "short_buzz") {
        return VIBRATION_SHORT_BUZZ;
    } else if (pattern == "purr_short") {
        return VIBRATION_PURR_SHORT;
    } else if (pattern == "purr" || pattern == "purr_pattern") {
        return VIBRATION_PURR_PATTERN;
    } else if (pattern == "gentle_heartbeat" || pattern == "heartbeat") {
        return VIBRATION_GENTLE_HEARTBEAT;
    } else if (pattern == "struggle_pattern" || pattern == "struggle") {
        return VIBRATION_STRUGGLE_PATTERN;
    } else if (pattern == "sharp_buzz") {
        return VIBRATION_SHARP_BUZZ;
    } else if (pattern == "tremble_pattern" || pattern == "tremble") {
        return VIBRATION_TREMBLE_PATTERN;
    } else if (pattern == "giggle_pattern" || pattern == "giggle") {
        return VIBRATION_GIGGLE_PATTERN;
    } else if (pattern == "heartbeat_strong" || pattern == "strong_heartbeat") {
        return VIBRATION_HEARTBEAT_STRONG;
    } else if (pattern == "erratic_strong" || pattern == "erratic") {
        return VIBRATION_ERRATIC_STRONG;
    } else {
        return VIBRATION_MAX; // 无效模式标识
    }
}