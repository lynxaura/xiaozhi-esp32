#include "local_response_controller.h"
#include <esp_log.h>
#include <cstring>

static const char* TAG = "LocalResponse";

// ==================== ResponseComponent 实现 ====================

void ResponseComponent::Execute(const ExecutionContext& context) const {
    switch (type) {
        case ComponentType::VIBRATION:
            if (context.vibration_skill) {
                context.vibration_skill->Play(data.vibration_pattern);
                ESP_LOGI(TAG, "Executed vibration pattern: %d", static_cast<int>(data.vibration_pattern));
            }
            break;
        case ComponentType::MOTION:
            if (context.motion_skill) {
                context.motion_skill->Perform(data.motion_id);
                ESP_LOGI(TAG, "Executed motion: %d", static_cast<int>(data.motion_id));
            }
            break;
        case ComponentType::EMOTION:
            if (context.display) {
                context.display->SetEmotion(data.emotion.emotion_name);
                ESP_LOGI(TAG, "Set emotion: %s for %u ms", data.emotion.emotion_name, data.emotion.duration_ms);
            }
            break;
    }
}

bool ResponseComponent::CanExecute(DeviceState state) const {
    // 简化的状态检查
    return state != kDeviceStateFatalError && state != kDeviceStateUpgrading;
}

uint32_t ResponseComponent::GetDurationMs() const {
    switch (type) {
        case ComponentType::VIBRATION:
            return 500; // 振动默认时长
        case ComponentType::MOTION:
            return 1000; // 动作默认时长
        case ComponentType::EMOTION:
            return data.emotion.duration_ms;
        default:
            return 0;
    }
}

const char* ResponseComponent::GetTypeName() const {
    switch (type) {
        case ComponentType::VIBRATION: return "Vibration";
        case ComponentType::MOTION: return "Motion";
        case ComponentType::EMOTION: return "Emotion";
        default: return "Unknown";
    }
}

// ResponseTemplate 实现
ResponseTemplate::ResponseTemplate(const char* name, EventType event, int priority) 
    : name(name), trigger_event(event), priority(priority) {
}

void ResponseTemplate::AddBaseComponent(const ResponseComponent& component) {
    if (base_component_count < MAX_BASE_COMPONENTS) {
        base_components[base_component_count++] = component;
    } else {
        ESP_LOGW(TAG, "Base components full for template: %s", name ? name : "(null)");
    }
}

void ResponseTemplate::AddQuadrantComponent(EmotionQuadrant quadrant, const ResponseComponent& component) {
    int idx = static_cast<int>(quadrant);
    if (idx >= 0 && idx < 4 && quadrant_variants[idx].count < MAX_QUADRANT_COMPONENTS) {
        quadrant_variants[idx].components[quadrant_variants[idx].count++] = component;
    } else {
        ESP_LOGW(TAG, "Quadrant components full for template: %s, quadrant=%d", name ? name : "(null)", idx);
    }
}

void ResponseTemplate::GetComponents(EmotionQuadrant quadrant,
                                     ResponseComponent** out, size_t* out_count) const {
    size_t k = 0;
    // 先拷贝基础组件
    for (size_t i = 0; i < base_component_count; ++i) {
        out[k++] = const_cast<ResponseComponent*>(&base_components[i]);
    }
    // 再拷贝象限组件
    const int qidx = static_cast<int>(quadrant);
    if (qidx >= 0 && qidx < 4) {
        const auto& qv = quadrant_variants[qidx];
        for (size_t j = 0; j < qv.count; ++j) {
            out[k++] = const_cast<ResponseComponent*>(&qv.components[j]);
        }
    }
    *out_count = k;
}

// ==================== LocalResponseController 实现 ====================

LocalResponseController::LocalResponseController(
    Motion* motion_skill,
    Vibration* vibration_skill,
    Display* display)
    : motion_skill_(motion_skill)
    , vibration_skill_(vibration_skill)
    , display_(display)
    , initialized_(false) {
}

LocalResponseController::~LocalResponseController() {
}

bool LocalResponseController::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing Local Response Controller...");
    
    // 加载默认配置
    if (!LoadDefaultConfig()) {
        ESP_LOGE(TAG, "Failed to load default configuration");
        return false;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "✅ Local Response Controller initialized successfully");
    ESP_LOGI(TAG, "Loaded %zu response templates", template_count_);
    
    return true;
}

void LocalResponseController::ProcessEvent(const Event& event) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Controller not initialized, ignoring event");
        return;
    }

    ResponseTemplate* tmpl = FindTemplate(event.type);
    if (!tmpl) {
        ESP_LOGD(TAG, "No response template found for event type: %d", static_cast<int>(event.type));
        return;
    }

    ExecutionContext context = CreateContext(event);

    // 收集要执行的组件（基础5 + 象限3 = 最多8个）
    constexpr size_t kMaxComponents = 8;
    ResponseComponent* components[kMaxComponents];
    size_t comp_cnt = 0;
    tmpl->GetComponents(context.current_quadrant, components, &comp_cnt);

    if (comp_cnt == 0) {
        ESP_LOGD(TAG, "No components to execute for template: %s", tmpl->name ? tmpl->name : "(null)");
        return;
    }

    ESP_LOGI(TAG, "🎯 Processing event %d with template '%s' in quadrant %d",
             static_cast<int>(event.type),
             tmpl->name ? tmpl->name : "(null)",
             static_cast<int>(context.current_quadrant));

    ExecuteComponents(components, comp_cnt, context);
}

bool LocalResponseController::LoadDefaultConfig() {
    ESP_LOGI(TAG, "Loading default response configuration...");
    
    try {
        CreateDefaultTemplates();
        return true;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception while creating default templates: %s", e.what());
        return false;
    }
}

ResponseTemplate* LocalResponseController::FindTemplate(EventType event_type) {
    for (size_t i = 0; i < template_count_; i++) {
        if (templates_[i].trigger_event == event_type) {
            return &templates_[i];
        }
    }
    return nullptr;
}

ExecutionContext LocalResponseController::CreateContext(const Event& event) const {
    ExecutionContext context;
    
    // 设置事件信息
    context.event = event;
    context.device_state = Application::GetInstance().GetDeviceState();
    
    // 获取情感状态
    auto& emotion_engine = EmotionEngine::GetInstance();
    context.current_quadrant = emotion_engine.GetQuadrant();
    context.current_valence = emotion_engine.GetValence();
    context.current_arousal = emotion_engine.GetArousal();
    
    // 设置硬件接口
    context.motion_skill = motion_skill_;
    context.vibration_skill = vibration_skill_;
    context.display = display_;
    
    return context;
}

void LocalResponseController::ExecuteComponents(ResponseComponent** components,
                                                size_t count,
                                                const ExecutionContext& context) {
    ESP_LOGI(TAG, "Executing %zu response components...", count);
    uint32_t total_duration = 0;

    for (size_t i = 0; i < count; ++i) {
        ResponseComponent* comp = components[i];
        if (comp && comp->CanExecute(context.device_state)) {
            comp->Execute(context);
            total_duration = std::max(total_duration, comp->GetDurationMs());
            ESP_LOGD(TAG, "  - %s component executed (duration: %u ms)",
                     comp->GetTypeName(), comp->GetDurationMs());
        } else {
            ESP_LOGD(TAG, "  - component skipped (state not allowed)");
        }
    }
    ESP_LOGI(TAG, "Response execution completed (total duration: ~%u ms)", total_duration);
}


void LocalResponseController::CreateDefaultTemplates() {
    template_count_ = 0;
    
    // 创建紧急模板和象限模板（现在使用内存优化版本）
    AddEmergencyTemplates();
    AddQuadrantTemplates(); // 现在可以安全地包含所有模板
    
    ESP_LOGI(TAG, "Created %zu default response templates", template_count_);
}

void LocalResponseController::AddEmergencyTemplates() {
    // 紧急事件：自由落体
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("free_fall_emergency", EventType::MOTION_FREE_FALL, 1);
        
        // 所有象限都执行相同的紧急响应（使用静态创建）
        tmpl.AddBaseComponent(ResponseComponent::CreateVibration(VIBRATION_ERRATIC_STRONG));
        tmpl.AddBaseComponent(ResponseComponent::CreateMotion(MOTION_STRUGGLE_TWIST));
        tmpl.AddBaseComponent(ResponseComponent::CreateEmotion("surprised", 1000));
    }
    
    // 紧急事件：剧烈摇晃
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("violent_shake_emergency", EventType::MOTION_SHAKE_VIOLENTLY, 1);
        
        tmpl.AddBaseComponent(ResponseComponent::CreateVibration(VIBRATION_ERRATIC_STRONG));
        tmpl.AddBaseComponent(ResponseComponent::CreateMotion(MOTION_BODY_SHIVER));
        tmpl.AddBaseComponent(ResponseComponent::CreateEmotion("surprised", 800));
    }
    
    // 紧急事件：设备翻转
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("flip_emergency", EventType::MOTION_FLIP, 1);
        
        tmpl.AddBaseComponent(ResponseComponent::CreateVibration(VIBRATION_SHARP_BUZZ));
        tmpl.AddBaseComponent(ResponseComponent::CreateMotion(MOTION_QUICK_TURN_LEFT));
        tmpl.AddBaseComponent(ResponseComponent::CreateEmotion("surprised", 600));
    }
    
    // 紧急事件：设备倒置
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("upside_down_emergency", EventType::MOTION_UPSIDE_DOWN, 1);
        
        tmpl.AddBaseComponent(ResponseComponent::CreateVibration(VIBRATION_STRUGGLE_PATTERN));
        tmpl.AddBaseComponent(ResponseComponent::CreateMotion(MOTION_UNWILLING_TURN_BACK));
        tmpl.AddBaseComponent(ResponseComponent::CreateEmotion("angry", 1500));
    }
}

void LocalResponseController::AddQuadrantTemplates() {
    // TOUCH_TAP - 触摸点击的象限响应
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("touch_tap_quadrant", EventType::TOUCH_TAP, 2);
        
        // Q1 (积极高激活) - 兴奋状态：俏皮反应
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_SHORT_BUZZ));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_HAPPY_WIGGLE));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("happy", 400));
        
        // Q2 (消极高激活) - 恐惧/压力状态：受惊反应
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_SHARP_BUZZ));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_BODY_SHIVER));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("surprised", 300));
        
        // Q3 (消极低激活) - 悲伤/无聊状态：微弱反应
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_PURR_SHORT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_SLOW_TURN_LEFT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("sad", 600));
        
        // Q4 (积极低激活) - 满足/平静状态：舒适反应
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GENTLE_HEARTBEAT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_NUZZLE_FORWARD));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("neutral", 500));
    }
    
    // MOTION_SHAKE - 摇晃的象限响应
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("motion_shake_quadrant", EventType::MOTION_SHAKE, 2);
        
        // Valence > 0 (积极象限) - 欢快配合
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GIGGLE_PATTERN));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_EXCITED_JIGGLE));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("happy", 800));
        
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_PURR_PATTERN));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_HAPPY_WIGGLE));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("happy", 600));
        
        // Valence < 0 (消极象限) - 不耐烦/抗拒
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_TREMBLE_PATTERN));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_ANNOYED_TWIST_TO_HAPPY));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("angry", 800));
        
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_SHORT_BUZZ));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_DODGE_SLOWLY));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("sad", 600));
    }
    
    // MOTION_PICKUP - 被拿起的象限响应
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("motion_pickup_quadrant", EventType::MOTION_PICKUP, 2);
        
        // Valence > 0 (积极象限) - 期待反应
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GIGGLE_PATTERN));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_EXCITED_JIGGLE));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("happy", 600));
        
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_PURR_SHORT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_CURIOUS_PEEK_LEFT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("neutral", 500));
        
        // Valence < 0 (消极象限) - 警觉/蜷缩
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_TREMBLE_PATTERN));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_TENSE_UP));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("surprised", 400));
        
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_SHORT_BUZZ));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_DODGE_OPPOSITE_LEFT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("sad", 500));
    }
    
    // TOUCH_CRADLED - 摇篮模式：通常导向Q4平静状态
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("touch_cradled_quadrant", EventType::TOUCH_CRADLED, 2);
        
        // 所有象限都会逐渐导向平静，但初始反应不同
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GENTLE_HEARTBEAT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_RELAX_TO_CENTER));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("neutral", 2000));
        
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GENTLE_HEARTBEAT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_RELAX_COMPLETELY));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("happy", 3000));
        
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GENTLE_HEARTBEAT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_RELAX_TO_CENTER));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("neutral", 2500));
        
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GENTLE_HEARTBEAT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_RELAX_COMPLETELY));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("neutral", 3000));
    }
    
    // TOUCH_TICKLED - 挠痒模式：通常导向Q1兴奋状态
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("touch_tickled_quadrant", EventType::TOUCH_TICKLED, 2);
        
        // 基础强烈反应（所有象限都会有强烈反应，但情感表达不同）
        tmpl.AddBaseComponent(ResponseComponent::CreateVibration(VIBRATION_GIGGLE_PATTERN));
        tmpl.AddBaseComponent(ResponseComponent::CreateMotion(MOTION_TICKLE_TWIST_DANCE));
        
        // 象限特定情感表达
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("laughing", 1500));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("happy", 1200));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("surprised", 1000));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("neutral", 800));
    }
    
    // TOUCH_LONG_PRESS - 长按的象限响应
    {
        ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate("touch_long_press_quadrant", EventType::TOUCH_LONG_PRESS, 2);
        
        // Q1 (积极高激活) - 亲密开心
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_PURR_PATTERN));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_HAPPY_WIGGLE));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("happy", 1000));
        
        // Q2 (消极高激活) - 不安挣扎
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_STRUGGLE_PATTERN));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateMotion(MOTION_DODGE_SUBTLE));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_HIGH_AROUSAL, ResponseComponent::CreateEmotion("angry", 800));
        
        // Q3 (消极低激活) - 消极忍受
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_SHORT_BUZZ));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_SLOW_TURN_RIGHT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::NEGATIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("sad", 1200));
        
        // Q4 (积极低激活) - 享受抚摸
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateVibration(VIBRATION_GENTLE_HEARTBEAT));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateMotion(MOTION_RELAX_COMPLETELY));
        tmpl.AddQuadrantComponent(EmotionQuadrant::POSITIVE_LOW_AROUSAL, ResponseComponent::CreateEmotion("neutral", 1500));
    }
}

// 调试接口实现
void LocalResponseController::ListTemplates() const {
    ESP_LOGI(TAG, "=== Response Templates (%zu) ===", template_count_);
    for (size_t i = 0; i < template_count_; ++i) {
        const auto& t = templates_[i];
        size_t qv_cnt = 0;
        for (int q = 0; q < 4; ++q) qv_cnt += t.quadrant_variants[q].count;

        ESP_LOGI(TAG, "- %s (Event: %d, Priority: %d, Base: %zu, Quadrant comps: %zu)",
                 t.name ? t.name : "(null)",
                 static_cast<int>(t.trigger_event),
                 t.priority,
                 t.base_component_count,
                 qv_cnt);
    }
}

void LocalResponseController::TestResponse(EventType event_type, EmotionQuadrant quadrant) {
    ESP_LOGI(TAG, "🧪 Testing response for event %d in quadrant %d", 
             static_cast<int>(event_type), static_cast<int>(quadrant));
    
    Event test_event;
    test_event.type = event_type;
    test_event.timestamp_us = esp_timer_get_time();
    
    // 临时设置情感状态用于测试
    auto& emotion_engine = EmotionEngine::GetInstance();
    float test_v = 0.0f, test_a = 0.0f;
    switch (quadrant) {
        case EmotionQuadrant::POSITIVE_HIGH_AROUSAL: test_v = 0.5f; test_a = 0.5f; break;
        case EmotionQuadrant::POSITIVE_LOW_AROUSAL: test_v = 0.5f; test_a = -0.5f; break;
        case EmotionQuadrant::NEGATIVE_HIGH_AROUSAL: test_v = -0.5f; test_a = 0.5f; break;
        case EmotionQuadrant::NEGATIVE_LOW_AROUSAL: test_v = -0.5f; test_a = -0.5f; break;
    }
    emotion_engine.SetState(test_v, test_a);
    
    ProcessEvent(test_event);
}