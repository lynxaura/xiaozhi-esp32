#include "mcp_response_controller.h"
#include "display/display.h"
#include <esp_log.h>
#include <cJSON.h>

#define TAG "McpResponseController"

McpResponseController::McpResponseController(
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

McpResponseController::~McpResponseController() {
}

bool McpResponseController::Initialize() {
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
        ESP_LOGI(TAG, "MCP Response System Ready - Available Tools:");
        ESP_LOGI(TAG, "  - Body Motion Control (5 tools)");
        ESP_LOGI(TAG, "    * basic_motion: 基础动作(6种)");
        ESP_LOGI(TAG, "    * explore_motion: 探索动作(6种)");
        ESP_LOGI(TAG, "    * reaction_motion: 反应动作(7种)");
        ESP_LOGI(TAG, "    * performance_motion: 表演动作(4种)");
        ESP_LOGI(TAG, "    * angle_control: 精确角度控制");
        ESP_LOGI(TAG, "  - Haptic Feedback (1 tool)");
        ESP_LOGI(TAG, "  - Display Animation (2 tools)");
        ESP_LOGI(TAG, "  - Emotion Expression (8 tools)");
        ESP_LOGI(TAG, "  - Status Query (3 tools)");
        ESP_LOGI(TAG, "  Total: 19 tools with 23+ motion patterns available");
        
        return true;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Failed to initialize MCP tools: %s", e.what());
        return false;
    }
}

void McpResponseController::RegisterMotionTools() {
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
        
    // 探索和转头动作
    mcp_server.AddTool("self.body.explore_motion",
        "控制身体做探索和转头动作。可用动作：\n"
        "curious_peek_left: 好奇地向左探头\n"
        "curious_peek_right: 好奇地向右探头\n"
        "quick_turn_left: 快速转向左侧\n"
        "quick_turn_right: 快速转向右侧\n"
        "slow_turn_left: 慢悠悠地看向左侧\n"
        "slow_turn_right: 慢悠悠地看向右侧",
        PropertyList({
            Property("action", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return ExploreMotionTool(properties);
        });
        
    // 反应和情感动作
    mcp_server.AddTool("self.body.reaction_motion",
        "控制身体做反应性动作。可用动作：\n"
        "dodge_subtle: 微妙躲闪\n"
        "dodge_slowly: 缓慢躲开\n"
        "dodge_opposite_left: 向右躲避(被左侧触摸)\n"
        "dodge_opposite_right: 向左躲避(被右侧触摸)\n"
        "tense_up: 紧张绷紧\n"
        "body_shiver: 身体抖动(冷或被打扰)\n"
        "struggle_twist: 慌乱挣扎的扭动",
        PropertyList({
            Property("action", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return ReactionMotionTool(properties);
        });
        
    // 复杂表演动作
    mcp_server.AddTool("self.body.performance_motion",
        "控制身体做复杂表演动作。可用动作：\n"
        "tickle_twist_dance: 被挠痒痒的扭动舞蹈\n"
        "annoyed_twist_to_happy: 从烦躁扭动过渡到开心\n"
        "unwilling_turn_back: 不情愿地回到中心\n"
        "relax_to_center: 放松地回到中心",
        PropertyList({
            Property("action", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            return PerformanceMotionTool(properties);
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

void McpResponseController::RegisterVibrationTools() {
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
    
}

void McpResponseController::RegisterDisplayTools() {
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

void McpResponseController::RegisterComplexExpressionTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 平静情绪表达
    mcp_server.AddTool("self.express.calm",
        "表达平静安详的情绪，包含：\n"
        "- 身体：放松姿态或轻微摇摆\n" 
        "- 振动：温和心跳模式\n"
        "- 动画：中性表情或平静动画",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return CalmExpressionTool(properties);
        });

    // 开心情绪表达
    mcp_server.AddTool("self.express.happy",
        "表达开心愉悦的情绪，包含：\n"
        "- 身体：开心摇摆动作\n"
        "- 振动：欢快的笑声振动\n"
        "- 动画：开心表情动画",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return HappyExpressionTool(properties);
        });

    // 悲伤情绪表达
    mcp_server.AddTool("self.express.sad",
        "表达悲伤失落的情绪，包含：\n"
        "- 身体：低垂或收缩姿态\n"
        "- 振动：缓慢沉重的心跳\n"
        "- 动画：悲伤表情动画",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return SadExpressionTool(properties);
        });

    // 愤怒情绪表达
    mcp_server.AddTool("self.express.angry",
        "表达生气愤怒的情绪，包含：\n"
        "- 身体：摇头或挣扎动作\n"
        "- 振动：尖锐强烈振动\n"
        "- 动画：愤怒表情动画",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return AngryExpressionTool(properties);
        });

    // 害怕情绪表达
    mcp_server.AddTool("self.express.scared",
        "表达害怕恐惧的情绪，包含：\n"
        "- 身体：紧张或颤抖动作\n"
        "- 振动：颤抖不安的振动\n"
        "- 动画：惊恐表情动画",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return ScaredExpressionTool(properties);
        });

    // 好奇情绪表达
    mcp_server.AddTool("self.express.curious",
        "表达好奇探索的情绪，包含：\n"
        "- 身体：探索性左右转头\n"
        "- 振动：轻快的探索振动\n"
        "- 动画：好奇思考表情",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return CuriousExpressionTool(properties);
        });

    // 害羞情绪表达
    mcp_server.AddTool("self.express.shy",
        "表达害羞腼腆的情绪，包含：\n"
        "- 身体：微妙躲避或回缩\n"
        "- 振动：羞涩的轻颤\n"
        "- 动画：害羞表情动画",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return ShyExpressionTool(properties);
        });

    // 满足情绪表达
    mcp_server.AddTool("self.express.content",
        "表达满足舒适的情绪，包含：\n"
        "- 身体：舒适的轻摇或静止\n"
        "- 振动：满足的咕噜声\n"
        "- 动画：满足愉快的表情",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return ContentExpressionTool(properties);
        });
}

void McpResponseController::RegisterStatusTools() {
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
ReturnValue McpResponseController::BasicMotionTool(const PropertyList& properties) {
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


// 探索动作控制工具实现
ReturnValue McpResponseController::ExploreMotionTool(const PropertyList& properties) {
    const std::string& action = properties["action"].value<std::string>();
    
    if (!motion_skill_) {
        return ReturnValue("Motion system not available");
    }
    
    if (action == "curious_peek_left") {
        motion_skill_->Perform(MOTION_CURIOUS_PEEK_LEFT);
    } else if (action == "curious_peek_right") {
        motion_skill_->Perform(MOTION_CURIOUS_PEEK_RIGHT);
    } else if (action == "quick_turn_left") {
        motion_skill_->Perform(MOTION_QUICK_TURN_LEFT);
    } else if (action == "quick_turn_right") {
        motion_skill_->Perform(MOTION_QUICK_TURN_RIGHT);
    } else if (action == "slow_turn_left") {
        motion_skill_->Perform(MOTION_SLOW_TURN_LEFT);
    } else if (action == "slow_turn_right") {
        motion_skill_->Perform(MOTION_SLOW_TURN_RIGHT);
    } else {
        return ReturnValue("Unknown explore action: " + action);
    }
    
    ESP_LOGI(TAG, "Explore motion executed: %s", action.c_str());
    return ReturnValue("Explore action " + action + " executed successfully");
}

// 反应动作控制工具实现
ReturnValue McpResponseController::ReactionMotionTool(const PropertyList& properties) {
    const std::string& action = properties["action"].value<std::string>();
    
    if (!motion_skill_) {
        return ReturnValue("Motion system not available");
    }
    
    if (action == "dodge_subtle") {
        motion_skill_->Perform(MOTION_DODGE_SUBTLE);
    } else if (action == "dodge_slowly") {
        motion_skill_->Perform(MOTION_DODGE_SLOWLY);
    } else if (action == "dodge_opposite_left") {
        motion_skill_->Perform(MOTION_DODGE_OPPOSITE_LEFT);
    } else if (action == "dodge_opposite_right") {
        motion_skill_->Perform(MOTION_DODGE_OPPOSITE_RIGHT);
    } else if (action == "tense_up") {
        motion_skill_->Perform(MOTION_TENSE_UP);
    } else if (action == "body_shiver") {
        motion_skill_->Perform(MOTION_BODY_SHIVER);
    } else if (action == "struggle_twist") {
        motion_skill_->Perform(MOTION_STRUGGLE_TWIST);
    } else {
        return ReturnValue("Unknown reaction action: " + action);
    }
    
    ESP_LOGI(TAG, "Reaction motion executed: %s", action.c_str());
    return ReturnValue("Reaction action " + action + " executed successfully");
}

// 表演动作控制工具实现
ReturnValue McpResponseController::PerformanceMotionTool(const PropertyList& properties) {
    const std::string& action = properties["action"].value<std::string>();
    
    if (!motion_skill_) {
        return ReturnValue("Motion system not available");
    }
    
    if (action == "tickle_twist_dance") {
        motion_skill_->Perform(MOTION_TICKLE_TWIST_DANCE);
    } else if (action == "annoyed_twist_to_happy") {
        motion_skill_->Perform(MOTION_ANNOYED_TWIST_TO_HAPPY);
    } else if (action == "unwilling_turn_back") {
        motion_skill_->Perform(MOTION_UNWILLING_TURN_BACK);
    } else if (action == "relax_to_center") {
        motion_skill_->Perform(MOTION_RELAX_TO_CENTER);
    } else {
        return ReturnValue("Unknown performance action: " + action);
    }
    
    ESP_LOGI(TAG, "Performance motion executed: %s", action.c_str());
    return ReturnValue("Performance action " + action + " executed successfully");
}

// 精确角度控制工具实现
ReturnValue McpResponseController::AngleControlTool(const PropertyList& properties) {
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
ReturnValue McpResponseController::BasicVibrationTool(const PropertyList& properties) {
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


// 显示情绪动画工具实现
ReturnValue McpResponseController::ShowEmotionTool(const PropertyList& properties) {
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
ReturnValue McpResponseController::AnimationControlTool(const PropertyList& properties) {
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

// 8种情绪表达工具实现

// 平静情绪表达工具
ReturnValue McpResponseController::CalmExpressionTool(const PropertyList& properties) {
    // 身体动作：放松或轻微摇摆
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
    }
    
    // 振动反馈：温和心跳
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_GENTLE_HEARTBEAT);
    }
    
    // 屏幕动画：中性表情
    set_current_emotion_func_("neutral");
    
    ESP_LOGI(TAG, "Calm emotion expressed comprehensively");
    return ReturnValue("Calm emotion expressed successfully");
}

// 开心情绪表达工具
ReturnValue McpResponseController::HappyExpressionTool(const PropertyList& properties) {
    // 身体动作：开心摇摆
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
    }
    
    // 振动反馈：欢快笑声振动
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_GIGGLE_PATTERN);
    }
    
    // 屏幕动画：开心表情
    set_current_emotion_func_("happy");
    
    ESP_LOGI(TAG, "Happy emotion expressed comprehensively");
    return ReturnValue("Happy emotion expressed successfully");
}

// 悲伤情绪表达工具
ReturnValue McpResponseController::SadExpressionTool(const PropertyList& properties) {
    // 身体动作：低垂姿态
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
    }
    
    // 振动反馈：缓慢沉重心跳
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_GENTLE_HEARTBEAT);
    }
    
    // 屏幕动画：悲伤表情
    set_current_emotion_func_("sad");
    
    ESP_LOGI(TAG, "Sad emotion expressed comprehensively");
    return ReturnValue("Sad emotion expressed successfully");
}

// 愤怒情绪表达工具
ReturnValue McpResponseController::AngryExpressionTool(const PropertyList& properties) {
    // 身体动作：摇头或挣扎
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_SHAKE_HEAD);
    }
    
    // 振动反馈：尖锐强烈振动
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_STRUGGLE_PATTERN);
    }
    
    // 屏幕动画：愤怒表情
    set_current_emotion_func_("angry");
    
    ESP_LOGI(TAG, "Angry emotion expressed comprehensively");
    return ReturnValue("Angry emotion expressed successfully");
}

// 害怕情绪表达工具
ReturnValue McpResponseController::ScaredExpressionTool(const PropertyList& properties) {
    // 身体动作：紧张颤抖
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_TENSE_UP);
    }
    
    // 振动反馈：颤抖不安振动
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_TREMBLE_PATTERN);
    }
    
    // 屏幕动画：惊恐表情
    set_current_emotion_func_("surprised");
    
    ESP_LOGI(TAG, "Scared emotion expressed comprehensively");
    return ReturnValue("Scared emotion expressed successfully");
}

// 好奇情绪表达工具
ReturnValue McpResponseController::CuriousExpressionTool(const PropertyList& properties) {
    // 身体动作：探索性转头
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_CURIOUS_PEEK_LEFT);
    }
    
    // 振动反馈：轻快探索振动
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_SHORT_BUZZ);
    }
    
    // 屏幕动画：思考表情
    set_current_emotion_func_("thinking");
    
    ESP_LOGI(TAG, "Curious emotion expressed comprehensively");
    return ReturnValue("Curious emotion expressed successfully");
}

// 害羞情绪表达工具
ReturnValue McpResponseController::ShyExpressionTool(const PropertyList& properties) {
    // 身体动作：微妙躲避
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_DODGE_SUBTLE);
    }
    
    // 振动反馈：羞涩轻颤
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_PURR_SHORT);
    }
    
    // 屏幕动画：害羞表情（使用neutral作为基础）
    set_current_emotion_func_("neutral");
    
    ESP_LOGI(TAG, "Shy emotion expressed comprehensively");
    return ReturnValue("Shy emotion expressed successfully");
}

// 满足情绪表达工具
ReturnValue McpResponseController::ContentExpressionTool(const PropertyList& properties) {
    // 身体动作：舒适轻摇
    if (motion_skill_) {
        motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
    }
    
    // 振动反馈：满足咕噜声
    if (vibration_skill_) {
        vibration_skill_->Play(VIBRATION_PURR_PATTERN);
    }
    
    // 屏幕动画：满足表情
    set_current_emotion_func_("happy");
    
    ESP_LOGI(TAG, "Content emotion expressed comprehensively");
    return ReturnValue("Content emotion expressed successfully");
}

// 动作状态查询工具实现
ReturnValue McpResponseController::MotionStatusTool(const PropertyList& properties) {
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
ReturnValue McpResponseController::EventsStatusTool(const PropertyList& properties) {
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
ReturnValue McpResponseController::SystemStatusTool(const PropertyList& properties) {
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
motion_id_t McpResponseController::GetMotionIdForEmotion(const std::string& emotion) {
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

vibration_id_t McpResponseController::GetVibrationIdForEmotion(const std::string& emotion) {
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

motion_speed_t McpResponseController::ParseMotionSpeed(const std::string& speed_str) {
    if (speed_str == "slow") return MOTION_SPEED_SLOW;
    else if (speed_str == "fast") return MOTION_SPEED_FAST;
    else return MOTION_SPEED_MEDIUM; // 默认中等速度
}

vibration_id_t McpResponseController::ParseVibrationPattern(const std::string& pattern) {
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